#pragma once
// What both kits' scenes share, with no graphics API (part of rimgui_rt_core): the sun and sky, which objects a draw
// takes, and the scene's logic written once over each kit's Object and Material types (rimgui/rt/scene.h for OpenGL,
// rimgui/rt/vk/scene.h for Vulkan), which have the same fields and differ only in what their meshes and maps are.
#include <rimgui/rt/math.h>
#include <rimgui/rt/model_data.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace rimgui::rt {

struct Sun {
	float azimuth_degrees = 35, elevation_degrees = 45;
	vec3 color{1, 0.96f, 0.9f};
	float intensity = 3;
	vec3 sky{0.25f, 0.45f, 0.85f}, horizon{0.62f, 0.68f, 0.78f}, ground{0.22f, 0.19f, 0.16f};
	float ambient = 0.25f; // How strongly the sky (or the environment map) lights what the sun does not.
	vec3 direction() const; // Towards the sun, unit length.
};

// Which objects a draw call takes.
enum class DrawFilter {
	All,         // Opaque and masked first, then blended ones back to front with alpha blending on.
	Opaque,      // Opaque and masked only: for G-buffers, depth and shadow passes.
	Transparent, // Blended only, back to front, blending left to the caller.
};

// Position, then rotation (y, x, z, in degrees), then scale, after the file's own `base` transform.
mat4 object_transform(vec3 position, vec3 rotation_degrees, vec3 scale, const mat4 &base);

namespace scene_core {

// The bits of MaterialMaps a material has maps for, less those switched off.
template <typename MaterialT>
int maps(const MaterialT &m)
{
	int bits = 0;
	if (m.albedo_map) bits |= map_albedo;
	if (m.normal_map) bits |= map_normal;
	if (m.metal_rough_map) bits |= map_metal_rough;
	if (m.occlusion_map) bits |= map_occlusion;
	if (m.emissive_map) bits |= map_emissive;
	return bits & m.maps_enabled;
}

// A sphere round every visible object; a unit sphere at the origin when there is none.
template <typename ObjectT>
void bounds(const std::vector<ObjectT> &objects, vec3 &centre, float &radius)
{
	vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	bool any = false;
	for (const auto &o : objects) {
		if (!o.visible || !o.mesh) {
			continue;
		}
		const mat4 m = o.model();
		const vec3 a = o.mesh->bounds_min(), b = o.mesh->bounds_max();
		for (int i = 0; i < 8; ++i) {
			const vec4 p = m * vec4{(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z, 1};
			lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
			hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
			any = true;
		}
	}
	if (!any) {
		centre = {0, 0, 0};
		radius = 1;
		return;
	}
	centre = (lo + hi) * 0.5f;
	radius = std::max(length(hi - lo) * 0.5f, 0.1f);
}

// A number that changes whenever anything that changes the picture does.
template <typename ObjectT>
std::size_t fingerprint(const std::vector<ObjectT> &objects, const Sun &sun)
{
	std::size_t h = 1469598103934665603ull;
	const auto mix = [&h](const void *data, std::size_t n) {
		const auto *p = static_cast<const unsigned char *>(data);
		for (std::size_t i = 0; i < n; ++i) {
			h = (h ^ p[i]) * 1099511628211ull;
		}
	};
	const auto f = [&](float v) { mix(&v, sizeof v); };
	const auto v3 = [&](vec3 v) { f(v.x); f(v.y); f(v.z); };
	for (const auto &o : objects) {
		const std::uintptr_t mesh = reinterpret_cast<std::uintptr_t>(o.mesh.get());
		mix(&mesh, sizeof mesh);
		v3(o.position);
		v3(o.rotation_degrees);
		v3(o.scale);
		const auto &m = o.material;
		v3(m.albedo);
		f(m.alpha);
		f(m.roughness);
		f(m.metallic);
		v3(m.emissive);
		f(m.normal_scale);
		f(m.occlusion_strength);
		f(m.alpha_cutoff);
		const int ints[]{int(m.alpha_mode), m.maps(), m.double_sided ? 1 : 0, o.visible ? 1 : 0};
		mix(ints, sizeof ints);
	}
	f(sun.azimuth_degrees);
	f(sun.elevation_degrees);
	v3(sun.color);
	f(sun.intensity);
	v3(sun.sky);
	v3(sun.horizon);
	v3(sun.ground);
	f(sun.ambient);
	return h;
}

// The visible objects a filter takes, in drawing order: opaque and masked in scene order, then blended ones back to
// front from `camera_position`. `blended_from` is where the blended ones start.
template <typename ObjectT>
std::vector<const ObjectT *> draw_order(const std::vector<ObjectT> &objects, DrawFilter filter, vec3 camera_position, std::size_t *blended_from = nullptr)
{
	std::vector<const ObjectT *> out;
	if (filter != DrawFilter::Transparent) {
		for (const auto &o : objects) {
			if (o.visible && o.mesh && o.material.alpha_mode != AlphaMode::Blend) {
				out.push_back(&o);
			}
		}
	}
	const std::size_t first_blended = out.size();
	if (filter != DrawFilter::Opaque) {
		for (const auto &o : objects) {
			if (o.visible && o.mesh && o.material.alpha_mode == AlphaMode::Blend) {
				out.push_back(&o);
			}
		}
		std::sort(out.begin() + std::ptrdiff_t(first_blended), out.end(), [&](const ObjectT *a, const ObjectT *b) {
			return length(a->position - camera_position) > length(b->position - camera_position);
		});
	}
	if (blended_from) {
		*blended_from = first_blended;
	}
	return out;
}

// A model file's data as a kit's objects: each mesh and each image made once however many parts share it, through
// `make_mesh(const MeshData &, name)` and `make_texture(const Image &, bool srgb)`.
template <typename ObjectT, typename MakeMesh, typename MakeTexture>
std::vector<ObjectT> objects_from(const ModelData &data, MakeMesh &&make_mesh, MakeTexture &&make_texture)
{
	using MeshPtr = decltype(make_mesh(std::declval<const MeshData &>(), std::declval<const std::string &>()));
	using TexturePtr = decltype(make_texture(std::declval<const Image &>(), false));
	std::map<const MeshData *, MeshPtr> meshes;
	std::map<std::pair<const Image *, bool>, TexturePtr> textures;
	const auto texture = [&](const std::shared_ptr<const Image> &image, bool srgb) -> TexturePtr {
		if (!image) {
			return nullptr;
		}
		auto &slot = textures[{image.get(), srgb}];
		if (!slot) {
			slot = make_texture(*image, srgb);
		}
		return slot;
	};
	std::vector<ObjectT> out;
	for (const PartData &part : data.parts) {
		ObjectT o;
		o.name = part.name;
		auto &mesh = meshes[part.mesh.get()];
		if (!mesh) {
			mesh = make_mesh(*part.mesh, part.name);
		}
		o.mesh = mesh;
		o.base = part.base;
		const MaterialData &m = part.material;
		auto &mat = o.material;
		mat.albedo = m.albedo;
		mat.alpha = m.alpha;
		mat.roughness = m.roughness;
		mat.metallic = m.metallic;
		mat.emissive = m.emissive;
		mat.normal_scale = m.normal_scale;
		mat.occlusion_strength = m.occlusion_strength;
		mat.alpha_mode = m.alpha_mode;
		mat.alpha_cutoff = m.alpha_cutoff;
		mat.double_sided = m.double_sided;
		mat.albedo_map = texture(m.albedo_map, true);
		mat.normal_map = texture(m.normal_map, false);
		mat.metal_rough_map = texture(m.metal_rough_map, false);
		mat.occlusion_map = texture(m.occlusion_map, false);
		mat.emissive_map = texture(m.emissive_map, true);
		o.source = data.source;
		out.push_back(std::move(o));
	}
	return out;
}

} // namespace scene_core

} // namespace rimgui::rt
