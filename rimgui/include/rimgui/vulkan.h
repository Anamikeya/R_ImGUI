#pragma once
// The application shell on Vulkan (built when the Vulkan SDK is installed): what code drawing with Vulkan needs from it.
//
//   rimgui::AppConfig config;
//   config.backend = rimgui::Backend::Vulkan;       // or --vulkan on the command line
//   rimgui::run(config, [] {
//       const auto &vk = *rimgui::vulkan::context();
//       vkCmdDispatch(vk.command_buffer, ...);        // record into this frame's command buffer
//       ...ImGui windows...
//   });
//
// The shell owns the instance, the device (one queue for graphics, compute, transfer and presenting), the swapchain and
// the frames in flight. Each frame it waits for that frame's slot, acquires an image, and begins `command_buffer`
// before calling the frame function; after it, it records ImGui into the swapchain image and submits. Every Vulkan
// call goes through Volk, so extension functions (ray tracing, mesh shaders) are plain calls.
#include <volk.h>

#include <cstdint>
#include <functional>
#include <string>

namespace rimgui::vulkan {

// What the device was created with beyond Vulkan 1.3's core (dynamic rendering, synchronization2, maintenance4),
// Vulkan 1.2's (buffer device addresses, descriptor indexing, scalar layouts, timeline semaphores, host query reset)
// and 1.1's (shader draw parameters), which are required.
struct Features {
	bool acceleration_structure = false; // VK_KHR_acceleration_structure
	bool ray_tracing_pipeline = false;   // VK_KHR_ray_tracing_pipeline
	bool ray_query = false;              // VK_KHR_ray_query
	bool mesh_shader = false;            // VK_EXT_mesh_shader (with task shaders)
	bool validation = false;             // VK_LAYER_KHRONOS_validation is on
};

struct Context {
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t api_version = 0;
	std::string device_name;
	VkPhysicalDeviceProperties properties{};
	Features features;
	VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
	uint32_t frames_in_flight = 2;
	// This frame: its slot (0 to frames_in_flight - 1), its number since the start, and its command buffer, begun
	// before the frame function and submitted after it.
	uint32_t frame_slot = 0;
	uint64_t frame_number = 0;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
};

// The running shell's Vulkan objects; null when it is not running on Vulkan.
const Context *context();

// Runs `destroy` once the GPU can no longer be using what it frees: after the frames in flight have finished, or at
// the end. For images and buffers dropped while frames that use them may still be running.
void defer(std::function<void()> destroy);

// Records `record` into a command buffer of its own, submits it and waits: for uploads and work done once.
void submit_now(const std::function<void(VkCommandBuffer)> &record);

// Validation and driver messages: severity 0 verbose, 1 info, 2 warning, 3 error. Without a handler, warnings and
// errors go to standard error.
void on_message(std::function<void(int severity, const std::string &text)> handler);
// Validation errors so far.
int validation_errors();

// A readable name for a VkResult ("VK_ERROR_DEVICE_LOST").
const char *result_name(VkResult result);

} // namespace rimgui::vulkan
