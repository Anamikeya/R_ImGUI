#pragma once
// Pipelines from shader files that rebuild when saved: GLSL (shaderc) or HLSL (DXC, Windows), compiled to SPIR-V for
// Vulkan 1.3 with `#include`, errors placed by file and line, and the last good pipeline kept while a file is broken.
//
//   ShaderLibrary shaders({"D:/MyLab/shaders"});
//   GraphicsDesc d;
//   d.vertex = "mesh.vert";  d.fragment = "pbr.frag";
//   d.colors = {VK_FORMAT_R16G16B16A16_SFLOAT};  d.depth = VK_FORMAT_D32_SFLOAT;
//   Pipeline &pbr = shaders.graphics("pbr", d);
//   ...each frame: shaders.poll();  if (pbr.ready()) { pbr.bind(cb, constants); vkCmdDraw(...); }
//
// A file name may name an entry point after a colon: "post.hlsl:Bloom" (default "main"). HLSL is chosen by the
// ".hlsl" extension; everything else is GLSL. Every pipeline uses the kit's one layout (gpu.h): vertices are read from
// buffers by address (vertex pulling), so there is no vertex input state.
#include <rimgui/rt/vk/gpu.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rimgui::rt::vk {

enum class Stage { Vertex, Fragment, Compute, Task, Mesh, RayGen, Miss, ClosestHit, AnyHit, Intersection, Callable };

struct Compiled {
	std::vector<uint32_t> spirv;
	std::vector<std::filesystem::path> files; // Every file read, includes too.
	std::string error;                        // Empty when it compiled; else the compiler's messages.
};
// One stage from a file found in `folders` (or beside an including file).
Compiled compile(const std::string &file, Stage stage, const std::vector<std::filesystem::path> &folders);

enum class Blend { None, Alpha, Additive, Premultiplied };

struct GraphicsDesc {
	std::string vertex;             // A vertex shader, or:
	std::string task, mesh;         // a mesh shader with an optional task shader (VK_EXT_mesh_shader).
	std::string fragment;           // Empty: depth only.
	std::vector<VkFormat> colors;   // The render target's formats.
	VkFormat depth = VK_FORMAT_UNDEFINED;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkCullModeFlags cull = VK_CULL_MODE_BACK_BIT; // Counter-clockwise faces the viewer, as in OpenGL.
	bool depth_test = true, depth_write = true;
	VkCompareOp depth_compare = VK_COMPARE_OP_LESS;
	Blend blend = Blend::None;
	bool wireframe = false;
	float depth_bias_constant = 0, depth_bias_slope = 0; // For shadow maps.
};

struct RayTracingDesc {
	std::string raygen;
	std::vector<std::string> miss;
	struct HitGroup {
		std::string closest_hit, any_hit, intersection; // Intersection empty: triangles.
	};
	std::vector<HitGroup> hit_groups;
	std::vector<std::string> callables;
	uint32_t max_recursion = 1;
};

class Pipeline {
public:
	~Pipeline();
	Pipeline(const Pipeline &) = delete;
	Pipeline &operator=(const Pipeline &) = delete;

	// False until it has built once.
	bool ready() const { return pipeline_ != VK_NULL_HANDLE; }
	VkPipeline handle() const { return pipeline_; }
	VkPipelineBindPoint bind_point() const { return bind_point_; }
	const std::string &name() const { return name_; }
	// The latest build's errors, by file and line; empty when the current files built.
	const std::string &error() const { return error_; }
	int version() const { return version_; }
	// Graphics pipelines: the culling bind() sets (culling is dynamic state: vkCmdSetCullMode changes it).
	VkCullModeFlags cull() const { return graphics_.cull; }

	// Binds the pipeline and the kit's descriptor set, and pushes `constants`.
	void bind(VkCommandBuffer cb, const void *constants = nullptr, uint32_t size = 0) const;
	template <typename T>
	void bind(VkCommandBuffer cb, const T &constants) const
	{
		static_assert(sizeof(T) <= 256, "push constants hold 256 bytes");
		bind(cb, &constants, uint32_t(sizeof(T)));
	}
	// Ray tracing: traces width x height x depth rays through the shader binding table.
	void trace(VkCommandBuffer cb, uint32_t width, uint32_t height, uint32_t depth = 1) const;

private:
	friend class ShaderLibrary;
	Pipeline() = default;
	std::string name_;
	enum class Kind { Graphics, Compute, RayTracing } kind_ = Kind::Graphics;
	GraphicsDesc graphics_;
	std::string compute_;
	RayTracingDesc ray_tracing_;
	VkPipeline pipeline_ = VK_NULL_HANDLE;
	VkPipelineBindPoint bind_point_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
	std::vector<std::filesystem::path> files_;
	std::string error_;
	int version_ = 0;
	std::shared_ptr<Buffer> sbt_;
	VkStridedDeviceAddressRegionKHR raygen_{}, miss_{}, hit_{}, callable_{};
};

class ShaderLibrary {
public:
	explicit ShaderLibrary(std::vector<std::filesystem::path> folders = {});
	~ShaderLibrary();
	void add_folder(std::filesystem::path folder);
	const std::vector<std::filesystem::path> &folders() const { return folders_; }

	// Built now, and again whenever one of its files changes. The same name returns the same pipeline.
	Pipeline &graphics(const std::string &name, const GraphicsDesc &desc);
	Pipeline &compute(const std::string &name, const std::string &file);
	Pipeline &ray_tracing(const std::string &name, const RayTracingDesc &desc);

	// Checks the files' times (four times a second at most, unless `now`) and rebuilds what changed.
	bool poll(bool now = false);
	void reload_all();
	void on_message(std::function<void(bool ok, const std::string &text)> sink) { sink_ = std::move(sink); }
	const std::vector<std::unique_ptr<Pipeline>> &pipelines() const { return pipelines_; }

private:
	bool build(Pipeline &p);
	std::vector<std::filesystem::path> folders_;
	std::vector<std::unique_ptr<Pipeline>> pipelines_;
	std::vector<std::pair<std::string, std::filesystem::file_time_type>> times_;
	double last_poll_ = 0;
	std::function<void(bool, const std::string &)> sink_;
};

} // namespace rimgui::rt::vk
