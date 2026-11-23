#include <rimgui/rt/model_data.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>

namespace rimgui::rt {

namespace fs = std::filesystem;

namespace {

std::string lower_extension(const fs::path &path)
{
	std::string e = path.extension().string();
	std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return e;
}

void grow(vec3 &lo, vec3 &hi, vec3 p)
{
	lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
	hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
}

// Smooth normals where a primitive has none: each vertex the area-weighted sum of its triangles' normals.
void make_normals(MeshData &d)
{
	for (auto &v : d.vertices) {
		v.normal = {0, 0, 0};
	}
	for (std::size_t i = 0; i + 2 < d.indices.size(); i += 3) {
		Vertex &a = d.vertices[d.indices[i]], &b = d.vertices[d.indices[i + 1]], &c = d.vertices[d.indices[i + 2]];
		const vec3 n = cross(b.position - a.position, c.position - a.position);
		a.normal += n;
		b.normal += n;
		c.normal += n;
	}
	for (auto &v : d.vertices) {
		v.normal = normalize(v.normal);
	}
}

struct GltfLoader {
	fs::path folder;
	cgltf_data *data = nullptr;
	ModelData model;
	std::map<const cgltf_image *, std::shared_ptr<const Image>> images;
	std::map<const cgltf_primitive *, std::shared_ptr<const MeshData>> meshes;

	std::shared_ptr<const Image> texture(const cgltf_texture_view &view)
	{
		if (!view.texture || !view.texture->image) {
			return nullptr;
		}
		const cgltf_image *image = view.texture->image;
		if (const auto found = images.find(image); found != images.end()) {
			return found->second;
		}
		std::string error;
		std::shared_ptr<const Image> t;
		if (image->buffer_view) {
			const auto *bytes = static_cast<const unsigned char *>(image->buffer_view->buffer->data) + image->buffer_view->offset;
			t = decode_image_from_memory(bytes, image->buffer_view->size, image->name ? image->name : "embedded image", &error);
		} else if (image->uri && std::strncmp(image->uri, "data:", 5) == 0) {
			const char *comma = std::strchr(image->uri, ',');
			void *decoded = nullptr;
			// The decoded size exactly: cgltf decodes as many bytes as it is told, reading the text it needs for them.
			cgltf_size size = 0;
			if (comma) {
				const std::size_t length = std::strlen(comma + 1);
				const std::size_t padding = length >= 2 && comma[length] == '=' ? (comma[length - 1] == '=' ? 2 : 1) : 0;
				size = length / 4 * 3 - padding;
			}
			cgltf_options options{};
			if (comma && cgltf_load_buffer_base64(&options, size, comma + 1, &decoded) == cgltf_result_success) {
				t = decode_image_from_memory(static_cast<const unsigned char *>(decoded), size, "data URI image", &error);
				free(decoded);
			}
		} else if (image->uri) {
			std::string uri = image->uri;
			cgltf_decode_uri(uri.data());
			uri.resize(std::strlen(uri.c_str()));
			t = decode_image(folder / fs::path(std::u8string(uri.begin(), uri.end())), &error);
		}
		if (!t) {
			model.warnings.push_back(error.empty() ? std::string("an image could not be read") : error);
		}
		images[image] = t;
		return t;
	}

	MaterialData material(const cgltf_material *m)
	{
		MaterialData out;
		if (!m) {
			return out;
		}
		if (m->has_pbr_metallic_roughness) {
			const auto &p = m->pbr_metallic_roughness;
			out.albedo = {p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2]};
			out.alpha = p.base_color_factor[3];
			out.metallic = p.metallic_factor;
			out.roughness = p.roughness_factor;
			out.albedo_map = texture(p.base_color_texture);
			out.metal_rough_map = texture(p.metallic_roughness_texture);
		}
		out.normal_map = texture(m->normal_texture);
		out.normal_scale = m->normal_texture.texture ? m->normal_texture.scale : 1.0f;
		out.occlusion_map = texture(m->occlusion_texture);
		out.occlusion_strength = m->occlusion_texture.texture ? m->occlusion_texture.scale : 1.0f;
		out.emissive_map = texture(m->emissive_texture);
		float strength = 1;
		if (m->has_emissive_strength) {
			strength = m->emissive_strength.emissive_strength;
		}
		out.emissive = vec3{m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2]} * strength;
		out.alpha_mode = m->alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask : m->alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
		out.alpha_cutoff = m->alpha_cutoff;
		out.double_sided = m->double_sided;
		return out;
	}

	std::shared_ptr<const MeshData> mesh(const cgltf_primitive &p, const std::string &name)
	{
		if (const auto found = meshes.find(&p); found != meshes.end()) {
			return found->second;
		}
		if (p.type != cgltf_primitive_type_triangles) {
			model.warnings.push_back(name + ": a primitive that is not triangles was skipped");
			return meshes[&p] = nullptr;
		}
		const cgltf_accessor *positions = nullptr, *normals = nullptr, *uvs = nullptr;
		for (cgltf_size i = 0; i < p.attributes_count; ++i) {
			const auto &a = p.attributes[i];
			if (a.type == cgltf_attribute_type_position) positions = a.data;
			else if (a.type == cgltf_attribute_type_normal) normals = a.data;
			else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uvs = a.data;
		}
		if (!positions || positions->count == 0) {
			model.warnings.push_back(name + ": a primitive with no positions was skipped");
			return meshes[&p] = nullptr;
		}
		if (p.has_draco_mesh_compression) {
			model.warnings.push_back(name + ": Draco-compressed, which this loader does not decode; skipped");
			return meshes[&p] = nullptr;
		}
		if (!positions->buffer_view || positions->is_sparse ||
			(positions->buffer_view->has_meshopt_compression && !positions->buffer_view->data)) {
			model.warnings.push_back(name + ": its positions are sparse or compressed, which this loader does not read; skipped");
			return meshes[&p] = nullptr;
		}
		MeshData d;
		d.vertices.resize(positions->count);
		for (cgltf_size i = 0; i < positions->count; ++i) {
			Vertex &v = d.vertices[i];
			cgltf_accessor_read_float(positions, i, v.position.data(), 3);
			if (normals) cgltf_accessor_read_float(normals, i, v.normal.data(), 3);
			if (uvs) cgltf_accessor_read_float(uvs, i, &v.uv.x, 2);
		}
		if (p.indices) {
			d.indices.resize(p.indices->count);
			for (cgltf_size i = 0; i < p.indices->count; ++i) {
				d.indices[i] = unsigned(cgltf_accessor_read_index(p.indices, i));
			}
		} else {
			d.indices.resize(positions->count);
			for (cgltf_size i = 0; i < positions->count; ++i) {
				d.indices[i] = unsigned(i);
			}
		}
		d.indices.resize(d.indices.size() / 3 * 3);
		for (const unsigned index : d.indices) {
			if (index >= d.vertices.size()) {
				model.warnings.push_back(name + ": a primitive with an index out of range was skipped");
				return meshes[&p] = nullptr;
			}
		}
		if (!normals) {
			make_normals(d);
		}
		d.compute_bounds();
		return meshes[&p] = std::make_shared<const MeshData>(std::move(d));
	}

	void node(const cgltf_node *n)
	{
		if (n->mesh) {
			float world[16];
			cgltf_node_transform_world(n, world);
			mat4 base;
			std::copy(world, world + 16, base.m);
			for (cgltf_size i = 0; i < n->mesh->primitives_count; ++i) {
				const cgltf_primitive &p = n->mesh->primitives[i];
				std::string name = n->name ? n->name : n->mesh->name ? n->mesh->name : "part";
				if (n->mesh->primitives_count > 1) {
					name += " " + std::to_string(i + 1);
				}
				auto m = mesh(p, name);
				if (!m) {
					continue;
				}
				PartData o;
				o.name = name;
				o.mesh = m;
				o.base = base;
				o.material = material(p.material);
				model.parts.push_back(std::move(o));
				const vec3 a = m->bounds_min, b = m->bounds_max;
				for (int c = 0; c < 8; ++c) {
					const vec4 q = base * vec4{(c & 1) ? b.x : a.x, (c & 2) ? b.y : a.y, (c & 4) ? b.z : a.z, 1};
					grow(model.bounds_min, model.bounds_max, {q.x, q.y, q.z});
				}
			}
		}
		for (cgltf_size i = 0; i < n->children_count; ++i) {
			node(n->children[i]);
		}
	}
};

} // namespace

bool is_model_file(const fs::path &path)
{
	const std::string e = lower_extension(path);
	return e == ".gltf" || e == ".glb" || e == ".obj";
}

ModelData read_model(const fs::path &path, std::string *error)
{
	const std::string extension = lower_extension(path);
	if (extension == ".obj") {
		ModelData model;
		model.source = path.string();
		std::string problem;
		auto data = std::make_shared<MeshData>(load_obj(path, &problem));
		if (data->indices.empty()) {
			if (error) *error = problem;
			return model;
		}
		PartData o;
		o.name = path.stem().string();
		o.mesh = data;
		model.bounds_min = data->bounds_min;
		model.bounds_max = data->bounds_max;
		model.parts.push_back(std::move(o));
		return model;
	}
	if (extension != ".gltf" && extension != ".glb") {
		if (error) *error = path.string() + ": not a model file this reads (.gltf, .glb, .obj)";
		return {};
	}
	GltfLoader loader;
	loader.folder = path.parent_path();
	loader.model.bounds_min = {1e30f, 1e30f, 1e30f};
	loader.model.bounds_max = {-1e30f, -1e30f, -1e30f};
	cgltf_options options{};
	const std::string file = path.string();
	cgltf_result r = cgltf_parse_file(&options, file.c_str(), &loader.data);
	if (r == cgltf_result_success) r = cgltf_load_buffers(&options, loader.data, file.c_str());
	if (r == cgltf_result_success) r = cgltf_validate(loader.data);
	if (r != cgltf_result_success) {
		if (error) *error = path.string() + ": not a glTF file this can read (cgltf error " + std::to_string(int(r)) + ")";
		cgltf_free(loader.data);
		return {};
	}
	// Extensions a file says it cannot be read without: refused by name when this loader does not do them, so a
	// compressed mesh never loads as nothing.
	static const char *const understood[]{"KHR_materials_emissive_strength", "KHR_mesh_quantization", "KHR_texture_transform",
		"KHR_materials_ior", "KHR_materials_specular", "KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_clearcoat",
		"KHR_materials_sheen", "KHR_materials_unlit", "KHR_lights_punctual", "KHR_texture_basisu"};
	for (cgltf_size i = 0; i < loader.data->extensions_required_count; ++i) {
		const std::string name = loader.data->extensions_required[i];
		const bool known = std::any_of(std::begin(understood), std::end(understood), [&](const char *u) { return name == u; });
		if (!known || name == "KHR_texture_basisu") {
			if (error) {
				*error = path.string() + ": needs " + name + ", which this loader does not decode" +
						 (name == "KHR_draco_mesh_compression" || name == "EXT_meshopt_compression"
								 ? std::string(" (compressed meshes): export it without mesh compression, for example from Blender's glTF exporter with compression off")
								 : std::string());
			}
			cgltf_free(loader.data);
			return {};
		}
	}
	for (cgltf_size i = 0; i < loader.data->extensions_used_count; ++i) {
		const std::string name = loader.data->extensions_used[i];
		if (name == "KHR_texture_transform") {
			loader.model.warnings.push_back("KHR_texture_transform is used and not applied: textures with an offset, scale or rotation may be misplaced");
		} else if (name.rfind("KHR_materials_", 0) == 0 && name != "KHR_materials_emissive_strength") {
			loader.model.warnings.push_back(name + " is used; the lab's materials are metal-roughness only, so that part is left out");
		}
	}
	const cgltf_scene *scene = loader.data->scene ? loader.data->scene : loader.data->scenes_count ? &loader.data->scenes[0] : nullptr;
	if (scene) {
		for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
			loader.node(scene->nodes[i]);
		}
	} else {
		for (cgltf_size i = 0; i < loader.data->nodes_count; ++i) {
			if (!loader.data->nodes[i].parent) {
				loader.node(&loader.data->nodes[i]);
			}
		}
	}
	cgltf_free(loader.data);
	if (loader.model.parts.empty()) {
		if (error) *error = path.string() + ": no triangle meshes in it";
		loader.model.bounds_min = loader.model.bounds_max = {0, 0, 0};
	}
	loader.model.source = path.string();
	return std::move(loader.model);
}

} // namespace rimgui::rt
