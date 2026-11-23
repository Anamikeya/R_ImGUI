// The application shell on Vulkan: instance, device, swapchain, frames in flight and ImGui's Vulkan backend.
#include "app_internal.h"

#include <rimgui/vulkan.h>

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace rimgui::vulkan {

namespace {

Context *current = nullptr;
std::function<void(int, const std::string &)> message_handler;
int errors_reported = 0;

struct Deferred {
	uint64_t after_frame;
	std::function<void()> destroy;
};
std::deque<Deferred> deferred;

} // namespace

const Context *context()
{
	return current;
}

void defer(std::function<void()> destroy)
{
	if (!current) {
		destroy();
		return;
	}
	deferred.push_back({current->frame_number + current->frames_in_flight, std::move(destroy)});
}

void on_message(std::function<void(int, const std::string &)> handler)
{
	message_handler = std::move(handler);
}

int validation_errors()
{
	return errors_reported;
}

const char *result_name(VkResult r)
{
	switch (r) {
	case VK_SUCCESS: return "VK_SUCCESS";
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	default: return "VkResult (another value)";
	}
}

} // namespace rimgui::vulkan

namespace rimgui::detail {

namespace {

using namespace rimgui::vulkan;

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
	const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
	const int level = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT     ? 3
					  : severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? 2
					  : severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    ? 1
																					: 0;
	// The kit keeps every image in VK_IMAGE_LAYOUT_GENERAL by design (no layout tracking; on current desktop GPUs it
	// costs nothing measurable), so the layers' "should be TRANSFER_*_OPTIMAL" performance hint is not reported.
	if (data && data->messageIdNumber == int32_t(0x4dae5635)) {
		return VK_FALSE;
	}
	if (level == 3) {
		++errors_reported;
	}
	const std::string text = data && data->pMessage ? data->pMessage : "(no message)";
	if (message_handler) {
		message_handler(level, text);
	} else if (level >= 2) {
		std::cerr << (level == 3 ? "Vulkan error: " : "Vulkan warning: ") << text << '\n';
	}
	return VK_FALSE;
}

void check(VkResult r, const char *what)
{
	if (r != VK_SUCCESS) {
		throw std::runtime_error(std::string(what) + " failed: " + result_name(r));
	}
}

bool has_extension(const std::vector<VkExtensionProperties> &all, const char *name)
{
	return std::any_of(all.begin(), all.end(), [&](const VkExtensionProperties &e) { return std::strcmp(e.extensionName, name) == 0; });
}

class VulkanRenderer final : public Renderer {
public:
	void window_hints(const AppConfig &, bool) override
	{
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	}

	std::string start(GLFWwindow *window, const AppConfig &, bool hidden, bool validation) override
	{
		window_ = window;
		hidden_ = hidden;
		ImGui_ImplGlfw_InitForVulkan(window_, true);
		glfw_ = true;
		try {
			create_instance(validation);
			check(glfwCreateWindowSurface(context_.instance, window, nullptr, &surface_), "glfwCreateWindowSurface");
			create_device();
			create_frames();
			create_swapchain();
			start_imgui();
		} catch (const std::exception &e) {
			return std::string("Vulkan could not start: ") + e.what();
		}
		current = &context_;
		return {};
	}

	bool begin_frame() override
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(window_, &width, &height);
		if (width == 0 || height == 0) {
			return false;
		}
		if (rebuild_swapchain_ || uint32_t(width) != extent_.width || uint32_t(height) != extent_.height) {
			create_swapchain();
		}
		Frame &f = frames_[context_.frame_slot];
		vkWaitForFences(context_.device, 1, &f.fence, VK_TRUE, UINT64_MAX);
		// What was dropped long enough ago can go now: every frame that might have used it has finished.
		while (!deferred.empty() && deferred.front().after_frame <= context_.frame_number) {
			deferred.front().destroy();
			deferred.pop_front();
		}
		const VkResult acquired = vkAcquireNextImageKHR(context_.device, swapchain_, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &image_index_);
		if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
			rebuild_swapchain_ = true;
			return false;
		}
		if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
			check(acquired, "vkAcquireNextImageKHR");
		}
		vkResetFences(context_.device, 1, &f.fence);
		vkResetCommandPool(context_.device, f.pool, 0);
		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(f.commands, &begin);
		context_.command_buffer = f.commands;
		ImGui_ImplVulkan_NewFrame();
		return true;
	}

	bool end_frame(const ImVec4 &clear, const std::string &screenshot) override
	{
		Frame &f = frames_[context_.frame_slot];
		VkCommandBuffer cb = f.commands;
		const VkImage image = images_[image_index_];
		barrier(cb, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
		VkRenderingAttachmentInfo colour{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
		colour.imageView = views_[image_index_];
		colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colour.clearValue.color = {{clear.x, clear.y, clear.z, clear.w}};
		VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
		rendering.renderArea = {{0, 0}, extent_};
		rendering.layerCount = 1;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &colour;
		vkCmdBeginRendering(cb, &rendering);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
		vkCmdEndRendering(cb);

		VkBuffer readback = VK_NULL_HANDLE;
		VkDeviceMemory readback_memory = VK_NULL_HANDLE;
		if (!screenshot.empty()) {
			barrier(cb, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
			make_readback(VkDeviceSize(extent_.width) * extent_.height * 4, readback, readback_memory);
			VkBufferImageCopy copy{};
			copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			copy.imageExtent = {extent_.width, extent_.height, 1};
			vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
			barrier(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COPY_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
		} else {
			barrier(cb, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
		}
		vkEndCommandBuffer(cb);

		VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
		wait.semaphore = f.acquired;
		wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
		signal.semaphore = rendered_[image_index_];
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
		command.commandBuffer = cb;
		VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
		submit.waitSemaphoreInfoCount = 1;
		submit.pWaitSemaphoreInfos = &wait;
		submit.commandBufferInfoCount = 1;
		submit.pCommandBufferInfos = &command;
		submit.signalSemaphoreInfoCount = 1;
		submit.pSignalSemaphoreInfos = &signal;
		check(vkQueueSubmit2(context_.queue, 1, &submit, f.fence), "vkQueueSubmit2");

		bool ok = true;
		if (readback) {
			vkWaitForFences(context_.device, 1, &f.fence, VK_TRUE, UINT64_MAX);
			ok = save_readback(screenshot, readback_memory);
			vkDestroyBuffer(context_.device, readback, nullptr);
			vkFreeMemory(context_.device, readback_memory, nullptr);
		}

		VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &rendered_[image_index_];
		present.swapchainCount = 1;
		present.pSwapchains = &swapchain_;
		present.pImageIndices = &image_index_;
		const VkResult shown = vkQueuePresentKHR(context_.queue, &present);
		if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR) {
			rebuild_swapchain_ = true;
		} else if (shown != VK_SUCCESS) {
			check(shown, "vkQueuePresentKHR");
		}
		context_.command_buffer = VK_NULL_HANDLE;
		context_.frame_slot = (context_.frame_slot + 1) % context_.frames_in_flight;
		++context_.frame_number;
		return ok;
	}

	void idle() override
	{
		if (context_.device) {
			vkDeviceWaitIdle(context_.device);
		}
	}

	void stop() override
	{
		if (context_.device) {
			vkDeviceWaitIdle(context_.device);
			while (!deferred.empty()) {
				deferred.front().destroy();
				deferred.pop_front();
			}
		}
		if (imgui_) {
			ImGui_ImplVulkan_Shutdown();
		}
		if (glfw_) {
			ImGui_ImplGlfw_Shutdown();
		}
		current = nullptr;
		if (context_.device) {
			destroy_swapchain();
			for (Frame &f : frames_) {
				vkDestroyFence(context_.device, f.fence, nullptr);
				vkDestroySemaphore(context_.device, f.acquired, nullptr);
				vkDestroyCommandPool(context_.device, f.pool, nullptr);
			}
			vkDestroyCommandPool(context_.device, once_pool_, nullptr);
			vkDestroyDevice(context_.device, nullptr);
		}
		if (surface_) {
			vkDestroySurfaceKHR(context_.instance, surface_, nullptr);
		}
		if (messenger_) {
			vkDestroyDebugUtilsMessengerEXT(context_.instance, messenger_, nullptr);
		}
		if (context_.instance) {
			vkDestroyInstance(context_.instance, nullptr);
		}
		context_ = {};
	}

	// For submit_now.
	void run_once(const std::function<void(VkCommandBuffer)> &record)
	{
		VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool = once_pool_;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		VkCommandBuffer cb = VK_NULL_HANDLE;
		check(vkAllocateCommandBuffers(context_.device, &allocate, &cb), "vkAllocateCommandBuffers");
		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cb, &begin);
		record(cb);
		vkEndCommandBuffer(cb);
		VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		VkFence fence = VK_NULL_HANDLE;
		vkCreateFence(context_.device, &fence_info, nullptr, &fence);
		VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
		command.commandBuffer = cb;
		VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
		submit.commandBufferInfoCount = 1;
		submit.pCommandBufferInfos = &command;
		check(vkQueueSubmit2(context_.queue, 1, &submit, fence), "vkQueueSubmit2");
		vkWaitForFences(context_.device, 1, &fence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(context_.device, fence, nullptr);
		vkFreeCommandBuffers(context_.device, once_pool_, 1, &cb);
	}

private:
	struct Frame {
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer commands = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
		VkSemaphore acquired = VK_NULL_HANDLE;
	};

	void create_instance(bool validation)
	{
		check(volkInitialize(), "Loading the Vulkan library (vulkan-1)");
		uint32_t count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> available(count);
		vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
		uint32_t glfw_count = 0;
		const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_count);
		if (!glfw_extensions) {
			throw std::runtime_error("this window system cannot present with Vulkan");
		}
		std::vector<const char *> extensions(glfw_extensions, glfw_extensions + glfw_count);
		const bool debug_utils = has_extension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (debug_utils) {
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
		std::vector<const char *> layers;
		if (validation) {
			uint32_t layer_count = 0;
			vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
			std::vector<VkLayerProperties> all_layers(layer_count);
			vkEnumerateInstanceLayerProperties(&layer_count, all_layers.data());
			const bool found = std::any_of(all_layers.begin(), all_layers.end(),
				[](const VkLayerProperties &l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
			if (found) {
				layers.push_back("VK_LAYER_KHRONOS_validation");
				context_.features.validation = true;
			} else {
				std::cerr << "Vulkan: the validation layers were asked for but are not installed (they come with the Vulkan SDK)\n";
			}
		}
		VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.pApplicationName = "R_ImGUI";
		app.pEngineName = "R_ImGUI";
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		info.pApplicationInfo = &app;
		info.enabledExtensionCount = uint32_t(extensions.size());
		info.ppEnabledExtensionNames = extensions.data();
		info.enabledLayerCount = uint32_t(layers.size());
		info.ppEnabledLayerNames = layers.data();
		VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
		messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
								VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		messenger.pfnUserCallback = debug_callback;
		if (debug_utils && context_.features.validation) {
			info.pNext = &messenger; // Messages from vkCreateInstance itself.
		}
		check(vkCreateInstance(&info, nullptr, &context_.instance), "vkCreateInstance (Vulkan 1.3 is needed)");
		volkLoadInstance(context_.instance);
		if (debug_utils && context_.features.validation) {
			check(vkCreateDebugUtilsMessengerEXT(context_.instance, &messenger, nullptr, &messenger_), "vkCreateDebugUtilsMessengerEXT");
		}
	}

	void create_device()
	{
		uint32_t count = 0;
		vkEnumeratePhysicalDevices(context_.instance, &count, nullptr);
		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(context_.instance, &count, devices.data());
		// A discrete GPU with Vulkan 1.3 and a queue that draws, computes and presents to this window; else any such GPU.
		int best_score = -1;
		for (VkPhysicalDevice d : devices) {
			VkPhysicalDeviceProperties p{};
			vkGetPhysicalDeviceProperties(d, &p);
			if (p.apiVersion < VK_API_VERSION_1_3) {
				continue;
			}
			uint32_t family_count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(d, &family_count, nullptr);
			std::vector<VkQueueFamilyProperties> families(family_count);
			vkGetPhysicalDeviceQueueFamilyProperties(d, &family_count, families.data());
			for (uint32_t i = 0; i < family_count; ++i) {
				VkBool32 presents = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &presents);
				const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
				if (presents && (families[i].queueFlags & needed) == needed) {
					const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
					if (score > best_score) {
						best_score = score;
						context_.physical_device = d;
						context_.queue_family = i;
						context_.properties = p;
					}
					break;
				}
			}
		}
		if (!context_.physical_device) {
			throw std::runtime_error("no GPU with Vulkan 1.3 that can draw to this window");
		}
		context_.device_name = context_.properties.deviceName;
		context_.api_version = context_.properties.apiVersion;

		uint32_t extension_count = 0;
		vkEnumerateDeviceExtensionProperties(context_.physical_device, nullptr, &extension_count, nullptr);
		std::vector<VkExtensionProperties> available(extension_count);
		vkEnumerateDeviceExtensionProperties(context_.physical_device, nullptr, &extension_count, available.data());

		// What the GPU has, asked for in one chain.
		VkPhysicalDeviceMeshShaderFeaturesEXT mesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
		VkPhysicalDeviceRayQueryFeaturesKHR ray_query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
		VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
		VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
		VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
		VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		features.pNext = &v11;
		v11.pNext = &v12;
		v12.pNext = &v13;
		void **tail = &v13.pNext;
		const bool has_acceleration = has_extension(available, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
									  has_extension(available, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
		const bool has_rt_pipeline = has_acceleration && has_extension(available, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
		const bool has_ray_query = has_acceleration && has_extension(available, VK_KHR_RAY_QUERY_EXTENSION_NAME);
		const bool has_mesh = has_extension(available, VK_EXT_MESH_SHADER_EXTENSION_NAME);
		if (has_acceleration) { *tail = &acceleration; tail = &acceleration.pNext; }
		if (has_rt_pipeline) { *tail = &rt_pipeline; tail = &rt_pipeline.pNext; }
		if (has_ray_query) { *tail = &ray_query; tail = &ray_query.pNext; }
		if (has_mesh) { *tail = &mesh; tail = &mesh.pNext; }
		vkGetPhysicalDeviceFeatures2(context_.physical_device, &features);

		const auto require = [](VkBool32 have, const char *name) {
			if (!have) {
				throw std::runtime_error(std::string("this GPU lacks ") + name + ", which the shell needs");
			}
		};
		require(v13.dynamicRendering, "dynamicRendering");
		require(v13.synchronization2, "synchronization2");
		require(v12.bufferDeviceAddress, "bufferDeviceAddress");
		require(v12.descriptorIndexing, "descriptorIndexing");
		require(v12.timelineSemaphore, "timelineSemaphore");

		// Everything the GPU has is turned on: what is optional stays as reported, the chain is reused to enable.
		features.features.robustBufferAccess = VK_FALSE; // Costs speed; validation finds out-of-bounds use instead.
		std::vector<const char *> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
		context_.features.acceleration_structure = has_acceleration && acceleration.accelerationStructure;
		context_.features.ray_tracing_pipeline = has_rt_pipeline && rt_pipeline.rayTracingPipeline && context_.features.acceleration_structure;
		context_.features.ray_query = has_ray_query && ray_query.rayQuery && context_.features.acceleration_structure;
		context_.features.mesh_shader = has_mesh && mesh.meshShader && mesh.taskShader;
		if (context_.features.acceleration_structure) {
			extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
			extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
			acceleration.accelerationStructureCaptureReplay = VK_FALSE;
			acceleration.accelerationStructureIndirectBuild = VK_FALSE;
			acceleration.accelerationStructureHostCommands = VK_FALSE;
		}
		if (context_.features.ray_tracing_pipeline) {
			extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
			rt_pipeline.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE;
			rt_pipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE;
		}
		if (context_.features.ray_query) {
			extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
		}
		if (context_.features.mesh_shader) {
			extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
			// Mesh shaders through multiview and shading rates are not used; leaving them on needs other features.
			mesh.multiviewMeshShader = VK_FALSE;
			mesh.primitiveFragmentShadingRateMeshShader = VK_FALSE;
		}
		// Unlink the chain entries whose extensions are off.
		void **link = &v13.pNext;
		*link = nullptr;
		if (context_.features.acceleration_structure) { *link = &acceleration; link = &acceleration.pNext; *link = nullptr; }
		if (context_.features.ray_tracing_pipeline) { *link = &rt_pipeline; link = &rt_pipeline.pNext; *link = nullptr; }
		if (context_.features.ray_query) { *link = &ray_query; link = &ray_query.pNext; *link = nullptr; }
		if (context_.features.mesh_shader) { *link = &mesh; link = &mesh.pNext; *link = nullptr; }

		const float priority = 1;
		VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		queue.queueFamilyIndex = context_.queue_family;
		queue.queueCount = 1;
		queue.pQueuePriorities = &priority;
		VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		info.pNext = &features;
		info.queueCreateInfoCount = 1;
		info.pQueueCreateInfos = &queue;
		info.enabledExtensionCount = uint32_t(extensions.size());
		info.ppEnabledExtensionNames = extensions.data();
		check(vkCreateDevice(context_.physical_device, &info, nullptr, &context_.device), "vkCreateDevice");
		volkLoadDevice(context_.device);
		vkGetDeviceQueue(context_.device, context_.queue_family, 0, &context_.queue);
	}

	void create_frames()
	{
		for (Frame &f : frames_) {
			VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
			pool.queueFamilyIndex = context_.queue_family;
			check(vkCreateCommandPool(context_.device, &pool, nullptr, &f.pool), "vkCreateCommandPool");
			VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
			allocate.commandPool = f.pool;
			allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocate.commandBufferCount = 1;
			check(vkAllocateCommandBuffers(context_.device, &allocate, &f.commands), "vkAllocateCommandBuffers");
			VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			check(vkCreateFence(context_.device, &fence, nullptr, &f.fence), "vkCreateFence");
			VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
			check(vkCreateSemaphore(context_.device, &semaphore, nullptr, &f.acquired), "vkCreateSemaphore");
		}
		VkCommandPoolCreateInfo once{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		once.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		once.queueFamilyIndex = context_.queue_family;
		check(vkCreateCommandPool(context_.device, &once, nullptr, &once_pool_), "vkCreateCommandPool");
		context_.frames_in_flight = uint32_t(frames_.size());
	}

	void destroy_swapchain()
	{
		for (VkImageView v : views_) vkDestroyImageView(context_.device, v, nullptr);
		for (VkSemaphore s : rendered_) vkDestroySemaphore(context_.device, s, nullptr);
		views_.clear();
		rendered_.clear();
		images_.clear();
		if (swapchain_) {
			vkDestroySwapchainKHR(context_.device, swapchain_, nullptr);
			swapchain_ = VK_NULL_HANDLE;
		}
	}

	void create_swapchain()
	{
		vkDeviceWaitIdle(context_.device);
		VkSurfaceCapabilitiesKHR caps{};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context_.physical_device, surface_, &caps);
		uint32_t format_count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(context_.physical_device, surface_, &format_count, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(context_.physical_device, surface_, &format_count, formats.data());
		// UNORM, not sRGB: ImGui's colours are sRGB values already, written as they are, as OpenGL's default does.
		VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
		for (const auto &f : formats) {
			if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				chosen = f;
				break;
			}
		}
		uint32_t mode_count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(context_.physical_device, surface_, &mode_count, nullptr);
		std::vector<VkPresentModeKHR> modes(mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(context_.physical_device, surface_, &mode_count, modes.data());
		// FIFO waits for the display (always there); a hidden window does not wait.
		VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
		if (hidden_ && std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
			mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
		int width = 0, height = 0;
		glfwGetFramebufferSize(window_, &width, &height);
		VkExtent2D extent = caps.currentExtent;
		if (extent.width == UINT32_MAX) {
			extent = {std::clamp(uint32_t(width), caps.minImageExtent.width, caps.maxImageExtent.width),
				std::clamp(uint32_t(height), caps.minImageExtent.height, caps.maxImageExtent.height)};
		}
		uint32_t image_count = std::max(caps.minImageCount + 1, 3u);
		if (caps.maxImageCount) {
			image_count = std::min(image_count, caps.maxImageCount);
		}
		const VkSwapchainKHR old = swapchain_;
		VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
		info.surface = surface_;
		info.minImageCount = image_count;
		info.imageFormat = chosen.format;
		info.imageColorSpace = chosen.colorSpace;
		info.imageExtent = extent;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = caps.currentTransform;
		info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		info.presentMode = mode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = old;
		VkSwapchainKHR made = VK_NULL_HANDLE;
		check(vkCreateSwapchainKHR(context_.device, &info, nullptr, &made), "vkCreateSwapchainKHR");
		destroy_swapchain(); // The old one, now retired.
		swapchain_ = made;
		extent_ = extent;
		const VkFormat previous_format = context_.swapchain_format;
		context_.swapchain_format = chosen.format;
		uint32_t count = 0;
		vkGetSwapchainImagesKHR(context_.device, swapchain_, &count, nullptr);
		images_.resize(count);
		vkGetSwapchainImagesKHR(context_.device, swapchain_, &count, images_.data());
		for (VkImage image : images_) {
			VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			view.image = image;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = chosen.format;
			view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			VkImageView v = VK_NULL_HANDLE;
			check(vkCreateImageView(context_.device, &view, nullptr, &v), "vkCreateImageView");
			views_.push_back(v);
			VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
			VkSemaphore s = VK_NULL_HANDLE;
			check(vkCreateSemaphore(context_.device, &semaphore, nullptr, &s), "vkCreateSemaphore");
			rendered_.push_back(s);
		}
		image_count_ = count;
		rebuild_swapchain_ = false;
		if (imgui_) {
			ImGui_ImplVulkan_SetMinImageCount(image_count);
			if (previous_format != chosen.format) {
				throw std::runtime_error("the window's image format changed");
			}
		}
	}

	void start_imgui()
	{
		ImGui_ImplVulkan_InitInfo info{};
		info.ApiVersion = VK_API_VERSION_1_3;
		info.Instance = context_.instance;
		info.PhysicalDevice = context_.physical_device;
		info.Device = context_.device;
		info.QueueFamily = context_.queue_family;
		info.Queue = context_.queue;
		info.DescriptorPoolSize = 512; // Fonts and the images the application shows (ImGui_ImplVulkan_AddTexture).
		info.MinImageCount = image_count_;
		info.ImageCount = image_count_;
		info.UseDynamicRendering = true;
		info.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
		info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &context_.swapchain_format;
		info.CheckVkResultFn = [](VkResult r) {
			if (r < 0) {
				std::cerr << "ImGui's Vulkan backend: " << result_name(r) << '\n';
			}
		};
		if (!ImGui_ImplVulkan_Init(&info)) {
			throw std::runtime_error("ImGui's Vulkan backend did not start");
		}
		imgui_ = true;
	}

	void barrier(VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
		VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
	{
		VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
		b.srcStageMask = src_stage;
		b.srcAccessMask = src_access;
		b.dstStageMask = dst_stage;
		b.dstAccessMask = dst_access;
		b.oldLayout = from;
		b.newLayout = to;
		b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &b;
		vkCmdPipelineBarrier2(cb, &dependency);
	}

	void make_readback(VkDeviceSize size, VkBuffer &buffer, VkDeviceMemory &memory)
	{
		VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size = size;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		check(vkCreateBuffer(context_.device, &info, nullptr, &buffer), "vkCreateBuffer");
		VkMemoryRequirements needs{};
		vkGetBufferMemoryRequirements(context_.device, buffer, &needs);
		VkPhysicalDeviceMemoryProperties props{};
		vkGetPhysicalDeviceMemoryProperties(context_.physical_device, &props);
		uint32_t type = UINT32_MAX;
		const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
			if ((needs.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
				type = i;
				break;
			}
		}
		VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocate.allocationSize = needs.size;
		allocate.memoryTypeIndex = type;
		check(vkAllocateMemory(context_.device, &allocate, nullptr, &memory), "vkAllocateMemory");
		vkBindBufferMemory(context_.device, buffer, memory, 0);
	}

	bool save_readback(const std::string &path, VkDeviceMemory memory)
	{
		void *mapped = nullptr;
		if (vkMapMemory(context_.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
			return false;
		}
		const auto *bgra = static_cast<const unsigned char *>(mapped);
		const bool swap = context_.swapchain_format == VK_FORMAT_B8G8R8A8_UNORM || context_.swapchain_format == VK_FORMAT_B8G8R8A8_SRGB;
		std::vector<unsigned char> rgb(std::size_t(extent_.width) * extent_.height * 3);
		for (std::size_t i = 0; i < std::size_t(extent_.width) * extent_.height; ++i) {
			rgb[i * 3] = bgra[i * 4 + (swap ? 2 : 0)];
			rgb[i * 3 + 1] = bgra[i * 4 + 1];
			rgb[i * 3 + 2] = bgra[i * 4 + (swap ? 0 : 2)];
		}
		vkUnmapMemory(context_.device, memory);
		return write_ppm(path, int(extent_.width), int(extent_.height), rgb.data());
	}

	GLFWwindow *window_ = nullptr;
	bool hidden_ = false, imgui_ = false, glfw_ = false, rebuild_swapchain_ = false;
	Context context_;
	VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
	VkSurfaceKHR surface_ = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
	VkExtent2D extent_{};
	uint32_t image_count_ = 0, image_index_ = 0;
	std::vector<VkImage> images_;
	std::vector<VkImageView> views_;
	std::vector<VkSemaphore> rendered_;
	std::array<Frame, 2> frames_;
	VkCommandPool once_pool_ = VK_NULL_HANDLE;
};

VulkanRenderer *running = nullptr;

} // namespace

std::unique_ptr<Renderer> make_vulkan_renderer()
{
	auto r = std::make_unique<VulkanRenderer>();
	running = r.get();
	return r;
}

} // namespace rimgui::detail

namespace rimgui::vulkan {

void submit_now(const std::function<void(VkCommandBuffer)> &record)
{
	if (!current || !detail::running) {
		throw std::runtime_error("rimgui::vulkan::submit_now: the shell is not running on Vulkan");
	}
	detail::running->run_once(record);
}

} // namespace rimgui::vulkan
