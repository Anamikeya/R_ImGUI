#include <rimgui/rt/scene.h>

#include <algorithm>
#include <cstring>
#include <functional>

namespace rimgui::rt {

int Material::maps() const
{
	int m = 0;
	if (albedo_map) m |= map_albedo;
	if (normal_map) m |= map_normal;
	if (metal_rough_map) m |= map_metal_rough;
	if (occlusion_map) m |= map_occlusion;
	if (emissive_map) m |= map_emissive;
	return m & maps_enabled;
}

mat4 Object::model() const
{
	const float d = pi / 180;
	return translate(position) * rotate(rotation_degrees.y * d, {0, 1, 0}) * rotate(rotation_degrees.x * d, {1, 0, 0}) *
		   rotate(rotation_degrees.z * d, {0, 0, 1}) * rt::scale(scale) * base;
}

vec3 Sun::direction() const
{
	const float a = azimuth_degrees * pi / 180, e = elevation_degrees * pi / 180;
	return normalize({std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)});
}

namespace {

void draw_object(Program &program, const Object &o)
{
	const mat4 model = o.model();
	const Material &m = o.material;
	program.set("u_model", model);
	program.set("u_normal_matrix", transpose(inverse(model)));
	program.set("u_albedo", m.albedo);
	program.set("u_alpha", m.alpha);
	program.set("u_roughness", m.roughness);
	program.set("u_metallic", m.metallic);
	program.set("u_emissive", m.emissive);
	program.set("u_normal_scale", m.normal_scale);
	program.set("u_occlusion_strength", m.occlusion_strength);
	program.set("u_alpha_mode", int(m.alpha_mode));
	program.set("u_alpha_cutoff", m.alpha_cutoff);
	const int maps = m.maps();
	program.set("u_maps", maps);
	const auto bind = [&](const char *name, int bit, const std::shared_ptr<Texture> &t, int unit) {
		if ((maps & bit) && t && program.location(name) >= 0) {
			program.texture(name, material_texture_unit + unit, t->id());
		}
	};
	bind("u_albedo_map", map_albedo, m.albedo_map, 0);
	bind("u_normal_map", map_normal, m.normal_map, 1);
	bind("u_metal_rough_map", map_metal_rough, m.metal_rough_map, 2);
	bind("u_occlusion_map", map_occlusion, m.occlusion_map, 3);
	bind("u_emissive_map", map_emissive, m.emissive_map, 4);
	if (m.double_sided) {
		glDisable(GL_CULL_FACE);
	}
	o.mesh->draw();
	if (m.double_sided) {
		glEnable(GL_CULL_FACE);
	}
}

} // namespace

void Scene::draw(Program &program, DrawFilter filter, vec3 camera_position) const
{
	GLboolean culling = GL_FALSE;
	glGetBooleanv(GL_CULL_FACE, &culling);
	if (filter != DrawFilter::Transparent) {
		for (const auto &o : objects) {
			if (o.visible && o.mesh && o.material.alpha_mode != AlphaMode::Blend) {
				draw_object(program, o);
			}
		}
	}
	if (filter != DrawFilter::Opaque) {
		std::vector<const Object *> blended;
		for (const auto &o : objects) {
			if (o.visible && o.mesh && o.material.alpha_mode == AlphaMode::Blend) {
				blended.push_back(&o);
			}
		}
		std::sort(blended.begin(), blended.end(), [&](const Object *a, const Object *b) {
			return length(a->position - camera_position) > length(b->position - camera_position);
		});
		if (!blended.empty() && filter == DrawFilter::All) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
		}
		for (const Object *o : blended) {
			draw_object(program, *o);
		}
		if (!blended.empty() && filter == DrawFilter::All) {
			glDisable(GL_BLEND);
			glDepthMask(GL_TRUE);
		}
	}
	if (culling) {
		glEnable(GL_CULL_FACE);
	} else {
		glDisable(GL_CULL_FACE);
	}
	glActiveTexture(GL_TEXTURE0);
}

void Scene::bounds(vec3 &centre, float &radius) const
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

void Scene::set_lighting(Program &program) const
{
	program.set("u_sun_direction", sun.direction());
	program.set("u_sun_color", sun.color * sun.intensity);
	program.set("u_sky", sun.sky);
	program.set("u_horizon", sun.horizon);
	program.set("u_ground", sun.ground);
	program.set("u_ambient", sun.ambient);
}

std::size_t Scene::fingerprint() const
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
		const Material &m = o.material;
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

} // namespace rimgui::rt
