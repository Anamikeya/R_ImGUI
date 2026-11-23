#pragma once
// What the Vulkan lab draws: the same objects, metal-roughness materials, sun and sky as the OpenGL kit's scene
// (rimgui/rt/scene.h), with meshes in GPU buffers and maps in the kit's bindless texture array.
//
// Shaders read a draw's object through its address in the push constants (rimgui_vk_scene.glsl):
//   layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; ...yours... } pc;
//   Vertex v = vertex(pc.object, gl_VertexIndex);     // vertex pulling: no vertex input
// so a technique's push constants start with DrawConstants (frame, object), and Scene::draw fills `object` per draw.
#include <rimgui/rt/model_data.h>
#include <rimgui/rt/scene_core.h>
#include <rimgui/rt/vk/gpu.h>
#include <rimgui/rt/vk/shader.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rimgui::rt::vk {

using rt::DrawFilter;
using rt::Sun;

class Mesh {
public:
	explicit Mesh(const MeshData &data, std::string name = {});
	Mesh(const Mesh &) = delete;
	Mesh &operator=(const Mesh &) = delete;
	const std::string &name() const { return name_; }
	vec3 bounds_min() const { return min_; }
	vec3 bounds_max() const { return max_; }
	std::size_t triangles() const { return index_count_ / 3; }
	uint32_t index_count() const { return index_count_; }
	uint32_t vertex_count() const { return vertex_count_; }
	// Vertices (rt::Vertex: position, normal, uv; 32 bytes) and 32-bit indices, three to a triangle,
	// counter-clockwise from outside. Usable as ray tracing build input too.
	const Buffer &vertices() const { return *vertices_; }
	const Buffer &indices() const { return *indices_; }
	// The CPU copy, for building meshlets and the like.
	const MeshData &data() const { return data_; }

private:
	std::shared_ptr<Buffer> vertices_, indices_;
	uint32_t index_count_ = 0, vertex_count_ = 0;
	vec3 min_, max_;
	std::string name_;
	MeshData data_;
};

using Texture = Image;

struct Material {
	vec3 albedo{0.8f, 0.8f, 0.8f};
	float alpha = 1;
	float roughness = 0.5f, metallic = 0;
	vec3 emissive{0, 0, 0};
	float normal_scale = 1, occlusion_strength = 1;
	AlphaMode alpha_mode = AlphaMode::Opaque;
	float alpha_cutoff = 0.5f;
	bool double_sided = false;
	std::shared_ptr<Image> albedo_map, normal_map, metal_rough_map, occlusion_map, emissive_map;
	// Maps can be switched off one by one in the Scene panel, to see what each does.
	int maps_enabled = 31;
	int maps() const { return scene_core::maps(*this); }
};

struct Object {
	std::string name;
	std::shared_ptr<Mesh> mesh;
	vec3 position{0, 0, 0}, rotation_degrees{0, 0, 0}, scale{1, 1, 1};
	mat4 base; // A model file's own node transform, applied first.
	Material material;
	bool visible = true;
	std::string source; // The file it came from, if any.
	mat4 model() const { return object_transform(position, rotation_degrees, scale, base); }
};

// One object as shaders see it (ObjectData in rimgui_vk_scene.glsl; scalar layout).
struct ObjectData {
	mat4 model, normal_matrix;
	vec3 albedo;
	float alpha;
	vec3 emissive;
	float roughness;
	float metallic, normal_scale, occlusion_strength, alpha_cutoff;
	int32_t alpha_mode, maps;
	uint32_t albedo_map, normal_map, metal_rough_map, occlusion_map, emissive_map;
	uint32_t index_count;
	VkDeviceAddress vertices, indices;
};
static_assert(sizeof(ObjectData) == 224, "ObjectData must match rimgui_vk_scene.glsl");

// The frame as shaders see it (FrameData in rimgui_vk_scene.glsl): the camera, the sun and sky, the environment's
// maps by bindless index, and every visible object's data.
struct FrameData {
	mat4 view, projection, view_projection, inverse_view_projection, inverse_projection;
	vec3 camera_position;
	float time;
	vec2 resolution;
	int32_t frame_index, accumulation;
	vec3 sun_direction;
	float ambient;
	vec3 sun_color; // Colour times intensity.
	int32_t has_environment;
	vec3 sky;
	float environment_intensity;
	vec3 horizon;
	float environment_rotation; // Radians about +y.
	vec3 ground;
	float prefiltered_levels;
	uint32_t environment, irradiance, prefiltered, brdf_lut; // Cube, cube, cube, 2D: bindless indices.
	int32_t environment_as_sky;
	uint32_t object_count;
	VkDeviceAddress objects; // ObjectData[object_count], in Scene::objects order (hidden ones too).
};
static_assert(sizeof(FrameData) == 464, "FrameData must match rimgui_vk_scene.glsl");

// The first 16 bytes of every scene pass's push constants.
struct DrawConstants {
	VkDeviceAddress frame = 0;  // FrameData
	VkDeviceAddress object = 0; // ObjectData; Scene::draw sets it for each object
};

struct Scene {
	std::vector<Object> objects;
	Sun sun;

	// Puts every object's data in this frame's memory (FrameData::objects); the lab does it before each technique.
	VkDeviceAddress upload() const;
	// The address of object `index` from the last upload().
	VkDeviceAddress object_address(std::size_t index) const { return uploaded_ + index * sizeof(ObjectData); }
	// Draws the visible objects the filter takes with `pipeline` (already bound or not: it binds it), inside a
	// rendering begun on a target with the pipeline's formats. `constants` starts with DrawConstants; `object` is set
	// for each draw. Double-sided materials are drawn without culling. Blending is the pipeline's: draw Opaque with an
	// opaque pipeline, then Transparent with a blending one.
	void draw(VkCommandBuffer cb, const Pipeline &pipeline, DrawFilter filter, vec3 camera_position, const void *constants,
		uint32_t size) const;
	template <typename T>
	void draw(VkCommandBuffer cb, const Pipeline &pipeline, DrawFilter filter, vec3 camera_position, const T &constants) const
	{
		static_assert(sizeof(T) >= sizeof(DrawConstants) && sizeof(T) <= 256, "constants start with DrawConstants");
		draw(cb, pipeline, filter, camera_position, &constants, uint32_t(sizeof(T)));
	}
	void bounds(vec3 &centre, float &radius) const { scene_core::bounds(objects, centre, radius); }
	std::size_t fingerprint() const { return scene_core::fingerprint(objects, sun); }

private:
	mutable VkDeviceAddress uploaded_ = 0;
};

struct Model {
	std::vector<Object> parts;
	vec3 bounds_min{0, 0, 0}, bounds_max{0, 0, 0};
	std::vector<std::string> warnings;
};

// A model file (rimgui/rt/model_data.h) on the GPU. Empty parts and `error` set when it cannot be read.
Model load_model(const std::filesystem::path &path, std::string *error = nullptr);
// An image file as a texture with mipmaps; null with `error` set when it cannot be read.
std::shared_ptr<Image> load_texture(const std::filesystem::path &path, bool srgb, std::string *error = nullptr);

} // namespace rimgui::rt::vk
