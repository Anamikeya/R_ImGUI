#pragma once
// The Vulkan research kit's core: memory, buffers, images, render targets and the one descriptor set every pipeline
// shares. For code running inside rimgui::run on Vulkan (rimgui/vulkan.h).
//
// Bindless by design, so passes need no descriptor sets of their own:
//   set 0, binding 0  every 2D texture         uniform texture2D   u_textures[]   (Image::sampled_index())
//          binding 1  every cubemap            uniform textureCube u_cubes[]      (Image::sampled_index())
//          binding 2  every storage image      uniform image2D     u_images[]     (Image::storage_index())
//          binding 3  the shared samplers      uniform sampler     u_samplers[]   (Sampler::...)
//          binding 4  the scene's ray tracing acceleration structure, when the GPU has ray tracing
//   push constants: 256 bytes, the same layout for every pipeline; pass buffers by address (Buffer::address()) and
//   textures by index in them. Gpu::upload() puts a pass's constants in this frame's memory and gives their address.
// Shaders include "rimgui_vk.glsl" (or "rimgui_vk.hlsl") for these declarations.
//
// Images live in VK_IMAGE_LAYOUT_GENERAL for their whole life, so the only synchronisation passes need is a barrier
// between a pass that writes and one that reads: barrier(cb). Finer barriers can be written by hand where it matters.
#include <rimgui/rt/image.h>
#include <rimgui/rt/math.h>
#include <rimgui/vulkan.h>

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct VmaAllocator_T;
struct VmaAllocation_T;

namespace rimgui::rt::vk {

// The samplers in binding 3, by index.
enum Sampler : uint32_t {
	sampler_linear_repeat = 0, // Trilinear, anisotropic, wrapping: material textures.
	sampler_linear_clamp = 1,  // Trilinear, clamped: render targets, lookup tables, cubemaps.
	sampler_nearest_clamp = 2, // Exact texels.
	sampler_shadow = 3,        // Linear depth comparison (less or equal): shadow maps with sampler2DShadow.
	sampler_count = 4
};

enum class Memory {
	Gpu,      // Device-local: render targets, meshes, anything the GPU reads often. Filled through upload().
	Upload,   // Host-visible, written by the CPU every frame (mapped).
	Readback, // Host-visible and cached, read by the CPU (mapped).
};

class Buffer {
public:
	// `usage` beyond what every buffer gets (storage, transfer source and destination, device address).
	static std::shared_ptr<Buffer> create(VkDeviceSize size, VkBufferUsageFlags usage, Memory memory, std::string name = {});
	~Buffer();
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;

	// Copies `size` bytes in at `offset`: straight into mapped memory, or through a staging buffer and a wait.
	void upload(const void *data, VkDeviceSize size, VkDeviceSize offset = 0);
	VkBuffer buffer() const { return buffer_; }
	VkDeviceAddress address() const { return address_; }
	VkDeviceSize size() const { return size_; }
	void *mapped() const { return mapped_; }
	const std::string &name() const { return name_; }

private:
	Buffer() = default;
	VkBuffer buffer_ = VK_NULL_HANDLE;
	VmaAllocation_T *allocation_ = nullptr;
	VkDeviceSize size_ = 0;
	VkDeviceAddress address_ = 0;
	void *mapped_ = nullptr;
	std::string name_;
};

struct ImageDesc {
	uint32_t width = 1, height = 1;
	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	uint32_t mips = 1;           // 0: the full chain down to 1x1.
	bool cube = false;           // Six layers, viewed as a cubemap.
	bool storage = false;        // Writable from shaders (also listed in binding 2).
	bool attachment = false;     // Drawn into: colour, or depth for depth formats.
	std::string name;
};

class Image {
public:
	static std::shared_ptr<Image> create(const ImageDesc &desc);
	// A decoded image (rimgui/rt/image.h) with its full mip chain; `srgb` for colour images that are not HDR.
	static std::shared_ptr<Image> from_image(const rt::Image &image, bool srgb);
	~Image();
	Image(const Image &) = delete;
	Image &operator=(const Image &) = delete;

	VkImage image() const { return image_; }
	VkImageView view() const { return view_; }
	VkFormat format() const { return desc_.format; }
	uint32_t width() const { return desc_.width; }
	uint32_t height() const { return desc_.height; }
	uint32_t mips() const { return desc_.mips; }
	bool is_depth() const;
	const ImageDesc &desc() const { return desc_; }
	// Its place in binding 0 (2D) or 1 (cube); in binding 2 when it is a storage image (else UINT32_MAX).
	uint32_t sampled_index() const { return sampled_; }
	uint32_t storage_index() const { return storage_; }
	// A view of one mip level (one face too, for cubemaps: `layer`), for drawing into or writing as storage.
	VkImageView level_view(uint32_t mip, uint32_t layer = 0);
	// The storage index of one mip level (and one face of a cubemap), for compute passes that write levels or faces
	// one by one.
	uint32_t level_storage_index(uint32_t mip, uint32_t layer = 0);
	// For ImGui::Image: sampled linearly and clamped. Made on first use.
	ImTextureID imgui_texture();
	// Fills the mips below the first by blitting each from the one above.
	void generate_mips(VkCommandBuffer cb);

private:
	Image() = default;
	ImageDesc desc_;
	VkImage image_ = VK_NULL_HANDLE;
	VkImageView view_ = VK_NULL_HANDLE;
	VmaAllocation_T *allocation_ = nullptr;
	uint32_t sampled_ = UINT32_MAX, storage_ = UINT32_MAX;
	std::vector<VkImageView> level_views_;
	std::vector<uint32_t> level_storage_;
	uint32_t layers() const { return desc_.cube ? 6u : 1u; }
	VkDescriptorSet imgui_ = VK_NULL_HANDLE;
};

// Colour attachments and an optional depth attachment, resized on demand; drawn into with dynamic rendering.
class RenderTarget {
public:
	RenderTarget(std::vector<VkFormat> colors, VkFormat depth, std::string name = {});
	// Remakes the images for this size; true when it did.
	bool resize(uint32_t width, uint32_t height);
	// Begins drawing into it, clearing to `clear` (each colour attachment) and depth to 1 when asked, viewport and
	// scissor set to its size. `load` keeps what is there instead.
	void begin(VkCommandBuffer cb, const float clear[4] = nullptr, bool clear_depth = true, bool load = false) const;
	void end(VkCommandBuffer cb) const;
	const std::shared_ptr<Image> &color(std::size_t index = 0) const { return colors_[index]; }
	const std::shared_ptr<Image> &depth() const { return depth_; }
	std::size_t color_count() const { return colors_.size(); }
	const std::vector<VkFormat> &color_formats() const { return color_formats_; }
	VkFormat depth_format() const { return depth_format_; }
	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }

private:
	std::vector<VkFormat> color_formats_;
	VkFormat depth_format_;
	std::string name_;
	std::vector<std::shared_ptr<Image>> colors_;
	std::shared_ptr<Image> depth_;
	uint32_t width_ = 0, height_ = 0;
};

// Everything written before is visible to everything after: between passes.
void barrier(VkCommandBuffer cb);

class Gpu {
public:
	// Made once the shell runs on Vulkan (in on_start); null before.
	static Gpu *get();
	static void start();
	static void stop();

	VkDevice device() const;
	VmaAllocator_T *allocator() const { return allocator_; }
	VkPipelineLayout pipeline_layout() const { return pipeline_layout_; }
	VkDescriptorSet descriptor_set() const { return set_; }
	VkDescriptorSetLayout descriptor_set_layout() const { return set_layout_; }
	VkSampler sampler(Sampler s) const { return samplers_[s]; }

	// Copies `size` bytes into this frame's upload memory and gives their device address, 256-byte aligned. Valid for
	// this frame's commands only.
	VkDeviceAddress upload(const void *data, std::size_t size);
	template <typename T>
	VkDeviceAddress upload(const T &value)
	{
		return upload(&value, sizeof value);
	}
	// Binds the shared descriptor set and pushes `constants` (at most 256 bytes) for `bind_point`.
	void bind(VkCommandBuffer cb, VkPipelineBindPoint bind_point, const void *constants = nullptr, uint32_t size = 0) const;
	template <typename T>
	void bind(VkCommandBuffer cb, VkPipelineBindPoint bind_point, const T &constants) const
	{
		static_assert(sizeof(T) <= 256, "push constants hold 256 bytes");
		bind(cb, bind_point, &constants, uint32_t(sizeof(T)));
	}
	// The scene's acceleration structure in binding 4 (ray tracing GPUs).
	void set_acceleration_structure(VkAccelerationStructureKHR tlas);

	// Slots in the shared bindings; freed after the frames in flight that may use them.
	uint32_t add_sampled(VkImageView view, bool cube);
	uint32_t add_storage(VkImageView view);
	void remove_sampled(uint32_t index, bool cube);
	void remove_storage(uint32_t index);

	// Names a Vulkan object for debuggers and validation messages (VK_EXT_debug_utils, when there).
	void name(VkObjectType type, uint64_t handle, const std::string &name) const;

	static constexpr uint32_t max_textures = 8192, max_cubes = 512, max_storage = 2048;

private:
	Gpu() = default;
	void begin_frame_if_new();
	VmaAllocator_T *allocator_ = nullptr;
	VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
	VkDescriptorPool pool_ = VK_NULL_HANDLE;
	VkDescriptorSet set_ = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
	VkSampler samplers_[sampler_count]{};
	std::vector<uint32_t> free_textures_, free_cubes_, free_storage_;
	uint32_t next_texture_ = 0, next_cube_ = 0, next_storage_ = 0;
	struct Ring {
		std::shared_ptr<Buffer> buffer;
		VkDeviceSize used = 0;
	};
	std::vector<Ring> rings_;
	uint64_t ring_frame_ = UINT64_MAX;
	bool ray_tracing_ = false;
};

// Bytes per pixel of a format the kit makes images in (0 for others).
uint32_t format_size(VkFormat format);

} // namespace rimgui::rt::vk
