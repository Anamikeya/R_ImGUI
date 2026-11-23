#include <rimgui/rt/vk/gpu.h>

#include <imgui_impl_vulkan.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace rimgui::rt::vk {

namespace {

Gpu *instance = nullptr;

const vulkan::Context &ctx()
{
	const auto *c = vulkan::context();
	if (!c) {
		throw std::runtime_error("the Vulkan kit is used while the shell is not running on Vulkan");
	}
	return *c;
}

void check(VkResult r, const char *what)
{
	if (r != VK_SUCCESS) {
		throw std::runtime_error(std::string(what) + " failed: " + vulkan::result_name(r));
	}
}

bool depth_format(VkFormat f)
{
	return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_X8_D24_UNORM_PACK32 || f == VK_FORMAT_D24_UNORM_S8_UINT ||
		   f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkImageAspectFlags aspect_of(VkFormat f)
{
	return depth_format(f) ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
}

uint32_t full_chain(uint32_t width, uint32_t height)
{
	uint32_t levels = 1;
	while ((std::max(width, height) >> levels) > 0) {
		++levels;
	}
	return levels;
}

} // namespace

uint32_t format_size(VkFormat f)
{
	switch (f) {
	case VK_FORMAT_R8_UNORM: return 1;
	case VK_FORMAT_R8G8_UNORM: return 2;
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_D32_SFLOAT: return 4;
	case VK_FORMAT_R16_SFLOAT: return 2;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R32G32_SFLOAT: return 8;
	case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
	default: return 0;
	}
}

// ---- Barriers --------------------------------------------------------------------------------------------------------

void barrier(VkCommandBuffer cb)
{
	VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
	b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
	d.memoryBarrierCount = 1;
	d.pMemoryBarriers = &b;
	vkCmdPipelineBarrier2(cb, &d);
}

// ---- Gpu -------------------------------------------------------------------------------------------------------------

Gpu *Gpu::get()
{
	return instance;
}

VkDevice Gpu::device() const
{
	return ctx().device;
}

void Gpu::start()
{
	if (instance) {
		return;
	}
	const auto &c = ctx();
	auto *g = new Gpu();
	VmaVulkanFunctions functions{};
	functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	VmaAllocatorCreateInfo allocator{};
	allocator.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocator.physicalDevice = c.physical_device;
	allocator.device = c.device;
	allocator.instance = c.instance;
	allocator.vulkanApiVersion = VK_API_VERSION_1_3;
	allocator.pVulkanFunctions = &functions;
	check(vmaCreateAllocator(&allocator, &g->allocator_), "vmaCreateAllocator");

	// The shared samplers, fixed in the layout.
	VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	s.magFilter = s.minFilter = VK_FILTER_LINEAR;
	s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	s.maxLod = VK_LOD_CLAMP_NONE;
	s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	s.anisotropyEnable = VK_TRUE;
	s.maxAnisotropy = std::min(8.0f, c.properties.limits.maxSamplerAnisotropy);
	check(vkCreateSampler(c.device, &s, nullptr, &g->samplers_[sampler_linear_repeat]), "vkCreateSampler");
	s.anisotropyEnable = VK_FALSE;
	s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	check(vkCreateSampler(c.device, &s, nullptr, &g->samplers_[sampler_linear_clamp]), "vkCreateSampler");
	s.magFilter = s.minFilter = VK_FILTER_NEAREST;
	s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	check(vkCreateSampler(c.device, &s, nullptr, &g->samplers_[sampler_nearest_clamp]), "vkCreateSampler");
	s.magFilter = s.minFilter = VK_FILTER_LINEAR;
	s.compareEnable = VK_TRUE;
	s.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	check(vkCreateSampler(c.device, &s, nullptr, &g->samplers_[sampler_shadow]), "vkCreateSampler");

	g->ray_tracing_ = c.features.acceleration_structure && (c.features.ray_query || c.features.ray_tracing_pipeline);
	std::vector<VkDescriptorSetLayoutBinding> bindings{
		{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_textures, VK_SHADER_STAGE_ALL, nullptr},
		{1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_cubes, VK_SHADER_STAGE_ALL, nullptr},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_storage, VK_SHADER_STAGE_ALL, nullptr},
		{3, VK_DESCRIPTOR_TYPE_SAMPLER, sampler_count, VK_SHADER_STAGE_ALL, g->samplers_},
	};
	std::vector<VkDescriptorBindingFlags> flags{
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
		0,
	};
	if (g->ray_tracing_) {
		bindings.push_back({4, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_ALL, nullptr});
		flags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
	}
	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
	binding_flags.bindingCount = uint32_t(flags.size());
	binding_flags.pBindingFlags = flags.data();
	VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	layout.pNext = &binding_flags;
	layout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	layout.bindingCount = uint32_t(bindings.size());
	layout.pBindings = bindings.data();
	check(vkCreateDescriptorSetLayout(c.device, &layout, nullptr, &g->set_layout_), "vkCreateDescriptorSetLayout");

	std::vector<VkDescriptorPoolSize> sizes{
		{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_textures + max_cubes},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_storage},
		{VK_DESCRIPTOR_TYPE_SAMPLER, sampler_count},
	};
	if (g->ray_tracing_) {
		sizes.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1});
	}
	VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	pool.maxSets = 1;
	pool.poolSizeCount = uint32_t(sizes.size());
	pool.pPoolSizes = sizes.data();
	check(vkCreateDescriptorPool(c.device, &pool, nullptr, &g->pool_), "vkCreateDescriptorPool");
	VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	allocate.descriptorPool = g->pool_;
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &g->set_layout_;
	check(vkAllocateDescriptorSets(c.device, &allocate, &g->set_), "vkAllocateDescriptorSets");

	VkPushConstantRange push{VK_SHADER_STAGE_ALL, 0, 256};
	VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	pipeline.setLayoutCount = 1;
	pipeline.pSetLayouts = &g->set_layout_;
	pipeline.pushConstantRangeCount = 1;
	pipeline.pPushConstantRanges = &push;
	check(vkCreatePipelineLayout(c.device, &pipeline, nullptr, &g->pipeline_layout_), "vkCreatePipelineLayout");
	instance = g;

	// An upload ring for each frame in flight.
	for (uint32_t i = 0; i < c.frames_in_flight; ++i) {
		Ring ring;
		ring.buffer = Buffer::create(8u << 20, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Memory::Upload, "upload ring " + std::to_string(i));
		g->rings_.push_back(std::move(ring));
	}
}

void Gpu::stop()
{
	if (!instance) {
		return;
	}
	const auto &c = ctx();
	vkDeviceWaitIdle(c.device);
	Gpu *g = instance;
	g->rings_.clear();
	instance = nullptr;
	// Buffers and images freed above, and any still held elsewhere, go through vulkan::defer; the shell runs those
	// before it destroys the device. The allocator must outlive them, so it is freed last, the same way.
	VmaAllocator_T *allocator = g->allocator_;
	VkDevice device = c.device;
	vulkan::defer([g, allocator, device] {
		vkDestroyPipelineLayout(device, g->pipeline_layout_, nullptr);
		vkDestroyDescriptorPool(device, g->pool_, nullptr);
		vkDestroyDescriptorSetLayout(device, g->set_layout_, nullptr);
		for (VkSampler s : g->samplers_) {
			vkDestroySampler(device, s, nullptr);
		}
		vmaDestroyAllocator(allocator);
		delete g;
	});
}

void Gpu::begin_frame_if_new()
{
	const auto &c = ctx();
	if (ring_frame_ != c.frame_number) {
		ring_frame_ = c.frame_number;
		rings_[c.frame_slot].used = 0;
	}
}

VkDeviceAddress Gpu::upload(const void *data, std::size_t size)
{
	begin_frame_if_new();
	Ring &ring = rings_[ctx().frame_slot];
	const VkDeviceSize at = (ring.used + 255) & ~VkDeviceSize(255);
	if (at + size > ring.buffer->size()) {
		throw std::runtime_error("this frame's upload memory (8 MB) is full: upload less per frame, or use a Buffer");
	}
	std::memcpy(static_cast<char *>(ring.buffer->mapped()) + at, data, size);
	ring.used = at + size;
	return ring.buffer->address() + at;
}

void Gpu::bind(VkCommandBuffer cb, VkPipelineBindPoint bind_point, const void *constants, uint32_t size) const
{
	vkCmdBindDescriptorSets(cb, bind_point, pipeline_layout_, 0, 1, &set_, 0, nullptr);
	if (constants && size) {
		vkCmdPushConstants(cb, pipeline_layout_, VK_SHADER_STAGE_ALL, 0, size, constants);
	}
}

void Gpu::set_acceleration_structure(VkAccelerationStructureKHR tlas)
{
	if (!ray_tracing_) {
		return;
	}
	VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
	as.accelerationStructureCount = 1;
	as.pAccelerationStructures = &tlas;
	VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	w.pNext = &as;
	w.dstSet = set_;
	w.dstBinding = 4;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	vkUpdateDescriptorSets(ctx().device, 1, &w, 0, nullptr);
}

uint32_t Gpu::add_sampled(VkImageView view, bool cube)
{
	auto &free = cube ? free_cubes_ : free_textures_;
	auto &next = cube ? next_cube_ : next_texture_;
	const uint32_t most = cube ? max_cubes : max_textures;
	uint32_t index;
	if (!free.empty()) {
		index = free.back();
		free.pop_back();
	} else if (next < most) {
		index = next++;
	} else {
		throw std::runtime_error(cube ? "more than 512 cubemaps at once" : "more than 8192 textures at once");
	}
	VkDescriptorImageInfo info{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
	VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	w.dstSet = set_;
	w.dstBinding = cube ? 1 : 0;
	w.dstArrayElement = index;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	w.pImageInfo = &info;
	vkUpdateDescriptorSets(ctx().device, 1, &w, 0, nullptr);
	return index;
}

uint32_t Gpu::add_storage(VkImageView view)
{
	uint32_t index;
	if (!free_storage_.empty()) {
		index = free_storage_.back();
		free_storage_.pop_back();
	} else if (next_storage_ < max_storage) {
		index = next_storage_++;
	} else {
		throw std::runtime_error("more than 2048 storage images at once");
	}
	VkDescriptorImageInfo info{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
	VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	w.dstSet = set_;
	w.dstBinding = 2;
	w.dstArrayElement = index;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	w.pImageInfo = &info;
	vkUpdateDescriptorSets(ctx().device, 1, &w, 0, nullptr);
	return index;
}

void Gpu::remove_sampled(uint32_t index, bool cube)
{
	// Frames in flight may still read the slot: it is reused only after them.
	vulkan::defer([this, index, cube] { (cube ? free_cubes_ : free_textures_).push_back(index); });
}

void Gpu::remove_storage(uint32_t index)
{
	vulkan::defer([this, index] { free_storage_.push_back(index); });
}

void Gpu::name(VkObjectType type, uint64_t handle, const std::string &name) const
{
	if (!vkSetDebugUtilsObjectNameEXT || name.empty() || !handle) {
		return;
	}
	VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
	info.objectType = type;
	info.objectHandle = handle;
	info.pObjectName = name.c_str();
	vkSetDebugUtilsObjectNameEXT(ctx().device, &info);
}

// ---- Buffer ----------------------------------------------------------------------------------------------------------

std::shared_ptr<Buffer> Buffer::create(VkDeviceSize size, VkBufferUsageFlags usage, Memory memory, std::string name)
{
	Gpu *g = Gpu::get();
	if (!g) {
		throw std::runtime_error("Buffer::create before Gpu::start");
	}
	std::shared_ptr<Buffer> b(new Buffer());
	b->size_ = std::max<VkDeviceSize>(size, 4);
	b->name_ = std::move(name);
	VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	info.size = b->size_;
	info.usage = usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VmaAllocationCreateInfo allocation{};
	allocation.usage = VMA_MEMORY_USAGE_AUTO;
	if (memory == Memory::Upload) {
		allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	} else if (memory == Memory::Readback) {
		allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	}
	VmaAllocationInfo result{};
	check(vmaCreateBuffer(g->allocator(), &info, &allocation, &b->buffer_, &b->allocation_, &result), "vmaCreateBuffer");
	b->mapped_ = result.pMappedData;
	VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
	address.buffer = b->buffer_;
	b->address_ = vkGetBufferDeviceAddress(ctx().device, &address);
	g->name(VK_OBJECT_TYPE_BUFFER, uint64_t(b->buffer_), b->name_);
	return b;
}

Buffer::~Buffer()
{
	Gpu *g = Gpu::get();
	VmaAllocator_T *allocator = g ? g->allocator() : nullptr;
	VkBuffer buffer = buffer_;
	VmaAllocation_T *allocation = allocation_;
	if (!allocator) {
		return; // The kit is gone and the device with it.
	}
	vulkan::defer([allocator, buffer, allocation] { vmaDestroyBuffer(allocator, buffer, allocation); });
}

void Buffer::upload(const void *data, VkDeviceSize size, VkDeviceSize offset)
{
	if (offset + size > size_) {
		throw std::runtime_error(name_ + ": upload past the end of the buffer");
	}
	if (mapped_) {
		std::memcpy(static_cast<char *>(mapped_) + offset, data, size);
		return;
	}
	auto staging = Buffer::create(size, 0, Memory::Upload, name_ + " staging");
	std::memcpy(staging->mapped(), data, size);
	vulkan::submit_now([&](VkCommandBuffer cb) {
		VkBufferCopy copy{0, offset, size};
		vkCmdCopyBuffer(cb, staging->buffer(), buffer_, 1, &copy);
	});
}

// ---- Image -----------------------------------------------------------------------------------------------------------

bool Image::is_depth() const
{
	return depth_format(desc_.format);
}

std::shared_ptr<Image> Image::create(const ImageDesc &d)
{
	Gpu *g = Gpu::get();
	if (!g) {
		throw std::runtime_error("Image::create before Gpu::start");
	}
	std::shared_ptr<Image> img(new Image());
	img->desc_ = d;
	img->desc_.width = std::max(d.width, 1u);
	img->desc_.height = std::max(d.height, 1u);
	if (img->desc_.mips == 0) {
		img->desc_.mips = full_chain(img->desc_.width, img->desc_.height);
	}
	const bool depth = depth_format(d.format);
	VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	info.flags = d.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = d.format;
	info.extent = {img->desc_.width, img->desc_.height, 1};
	info.mipLevels = img->desc_.mips;
	info.arrayLayers = img->layers();
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (d.storage) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	if (d.attachment) info.usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VmaAllocationCreateInfo allocation{};
	allocation.usage = VMA_MEMORY_USAGE_AUTO;
	if (d.attachment || d.storage) {
		allocation.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT; // Render targets get memory of their own.
	}
	check(vmaCreateImage(g->allocator(), &info, &allocation, &img->image_, &img->allocation_, nullptr), "vmaCreateImage");
	g->name(VK_OBJECT_TYPE_IMAGE, uint64_t(img->image_), d.name);

	VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	view.image = img->image_;
	view.viewType = d.cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
	view.format = d.format;
	view.subresourceRange = {aspect_of(d.format), 0, img->desc_.mips, 0, img->layers()};
	check(vkCreateImageView(ctx().device, &view, nullptr, &img->view_), "vkCreateImageView");
	img->level_views_.assign(std::size_t(img->desc_.mips) * img->layers(), VK_NULL_HANDLE);
	img->level_storage_.assign(img->level_views_.size(), UINT32_MAX);

	// Into GENERAL for the rest of its life.
	vulkan::submit_now([&](VkCommandBuffer cb) {
		VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
		b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = img->image_;
		b.subresourceRange = {aspect_of(d.format), 0, img->desc_.mips, 0, img->layers()};
		VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &b;
		vkCmdPipelineBarrier2(cb, &dep);
	});

	if (!depth) {
		img->sampled_ = g->add_sampled(img->view_, d.cube);
	} else {
		img->sampled_ = g->add_sampled(img->view_, false); // Depth is read as a texture too (as red).
	}
	if (d.storage && !d.cube) {
		img->storage_ = img->level_storage_index(0);
	}
	return img;
}

std::shared_ptr<Image> Image::from_image(const rt::Image &source, bool srgb)
{
	if (source.width <= 0 || source.height <= 0) {
		return nullptr;
	}
	ImageDesc d;
	d.width = uint32_t(source.width);
	d.height = uint32_t(source.height);
	d.mips = 0;
	d.name = source.name;
	std::vector<unsigned char> pixels;
	const std::size_t count = std::size_t(source.width) * std::size_t(source.height);
	if (source.hdr) {
		d.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		pixels.resize(count * 16);
		auto *out = reinterpret_cast<float *>(pixels.data());
		for (std::size_t i = 0; i < count; ++i) {
			for (int c = 0; c < 4; ++c) {
				out[i * 4 + std::size_t(c)] = c < source.channels ? source.floats[i * std::size_t(source.channels) + std::size_t(c)] : 1.0f;
			}
		}
	} else if (source.channels == 1 || source.channels == 2) {
		d.format = source.channels == 1 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8_UNORM;
		pixels = source.bytes;
	} else {
		// Three channels have no common GPU format: widened to four.
		d.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		pixels.resize(count * 4);
		for (std::size_t i = 0; i < count; ++i) {
			for (int c = 0; c < 4; ++c) {
				pixels[i * 4 + std::size_t(c)] = c < source.channels ? source.bytes[i * std::size_t(source.channels) + std::size_t(c)] : 255;
			}
		}
	}
	auto img = create(d);
	auto staging = Buffer::create(pixels.size(), 0, Memory::Upload, d.name + " staging");
	std::memcpy(staging->mapped(), pixels.data(), pixels.size());
	vulkan::submit_now([&](VkCommandBuffer cb) {
		VkBufferImageCopy copy{};
		copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy.imageExtent = {d.width, d.height, 1};
		vkCmdCopyBufferToImage(cb, staging->buffer(), img->image_, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
		img->generate_mips(cb);
	});
	return img;
}

void Image::generate_mips(VkCommandBuffer cb)
{
	int32_t w = int32_t(desc_.width), h = int32_t(desc_.height);
	for (uint32_t level = 1; level < desc_.mips; ++level) {
		barrier(cb);
		VkImageBlit blit{};
		blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, layers()};
		blit.srcOffsets[1] = {w, h, 1};
		w = std::max(w / 2, 1);
		h = std::max(h / 2, 1);
		blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, layers()};
		blit.dstOffsets[1] = {w, h, 1};
		vkCmdBlitImage(cb, image_, VK_IMAGE_LAYOUT_GENERAL, image_, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
	}
	barrier(cb);
}

VkImageView Image::level_view(uint32_t mip, uint32_t layer)
{
	const std::size_t slot = std::size_t(mip) * layers() + layer;
	if (mip >= desc_.mips || layer >= layers()) {
		throw std::runtime_error(desc_.name + ": no mip " + std::to_string(mip) + " layer " + std::to_string(layer));
	}
	if (!level_views_[slot]) {
		VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		view.image = image_;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = desc_.format;
		view.subresourceRange = {aspect_of(desc_.format), mip, 1, layer, 1};
		check(vkCreateImageView(ctx().device, &view, nullptr, &level_views_[slot]), "vkCreateImageView");
	}
	return level_views_[slot];
}

uint32_t Image::level_storage_index(uint32_t mip, uint32_t layer)
{
	if (!desc_.storage) {
		throw std::runtime_error(desc_.name + ": not made as a storage image");
	}
	const std::size_t slot = std::size_t(mip) * layers() + layer;
	VkImageView view = level_view(mip, layer);
	if (level_storage_[slot] == UINT32_MAX) {
		level_storage_[slot] = Gpu::get()->add_storage(view);
	}
	return level_storage_[slot];
}

ImTextureID Image::imgui_texture()
{
	if (!imgui_) {
		imgui_ = ImGui_ImplVulkan_AddTexture(Gpu::get()->sampler(sampler_linear_clamp), view_, VK_IMAGE_LAYOUT_GENERAL);
	}
	return ImTextureID(uint64_t(imgui_));
}

Image::~Image()
{
	Gpu *g = Gpu::get();
	if (!g) {
		return;
	}
	if (sampled_ != UINT32_MAX) {
		g->remove_sampled(sampled_, desc_.cube && !is_depth());
	}
	for (uint32_t index : level_storage_) {
		if (index != UINT32_MAX) {
			g->remove_storage(index);
		}
	}
	VmaAllocator_T *allocator = g->allocator();
	VkDevice device = ctx().device;
	VkImage image = image_;
	VmaAllocation_T *allocation = allocation_;
	VkImageView view = view_;
	std::vector<VkImageView> levels = level_views_;
	VkDescriptorSet imgui = imgui_;
	vulkan::defer([=] {
		if (imgui) {
			ImGui_ImplVulkan_RemoveTexture(imgui);
		}
		for (VkImageView v : levels) {
			if (v) vkDestroyImageView(device, v, nullptr);
		}
		vkDestroyImageView(device, view, nullptr);
		vmaDestroyImage(allocator, image, allocation);
	});
}

// ---- RenderTarget ----------------------------------------------------------------------------------------------------

RenderTarget::RenderTarget(std::vector<VkFormat> colors, VkFormat depth, std::string name)
	: color_formats_(std::move(colors)), depth_format_(depth), name_(std::move(name))
{
}

bool RenderTarget::resize(uint32_t width, uint32_t height)
{
	width = std::max(width, 1u);
	height = std::max(height, 1u);
	if (width == width_ && height == height_ && (!colors_.empty() || depth_)) {
		return false;
	}
	width_ = width;
	height_ = height;
	colors_.clear();
	for (std::size_t i = 0; i < color_formats_.size(); ++i) {
		ImageDesc d;
		d.width = width;
		d.height = height;
		d.format = color_formats_[i];
		d.attachment = true;
		// Storage too, so compute passes can write them, where the format allows it (not sRGB).
		d.storage = color_formats_[i] != VK_FORMAT_R8G8B8A8_SRGB && color_formats_[i] != VK_FORMAT_B8G8R8A8_SRGB;
		d.name = name_ + " colour " + std::to_string(i);
		colors_.push_back(Image::create(d));
	}
	depth_.reset();
	if (depth_format_ != VK_FORMAT_UNDEFINED) {
		ImageDesc d;
		d.width = width;
		d.height = height;
		d.format = depth_format_;
		d.attachment = true;
		d.name = name_ + " depth";
		depth_ = Image::create(d);
	}
	return true;
}

void RenderTarget::begin(VkCommandBuffer cb, const float clear[4], bool clear_depth, bool load) const
{
	std::vector<VkRenderingAttachmentInfo> attachments;
	for (const auto &c : colors_) {
		VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
		a.imageView = c->view();
		a.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		a.loadOp = load || !clear ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
		a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		if (clear) {
			std::memcpy(a.clearValue.color.float32, clear, sizeof(float) * 4);
		}
		attachments.push_back(a);
	}
	VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
	if (depth_) {
		depth.imageView = depth_->view();
		depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		depth.loadOp = clear_depth && !load ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth.clearValue.depthStencil = {1.0f, 0};
	}
	VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
	info.renderArea = {{0, 0}, {width_, height_}};
	info.layerCount = 1;
	info.colorAttachmentCount = uint32_t(attachments.size());
	info.pColorAttachments = attachments.data();
	info.pDepthAttachment = depth_ ? &depth : nullptr;
	vkCmdBeginRendering(cb, &info);
	// Not flipped: with the kit's OpenGL-style matrices, NDC y = -1 lands in the image's first row in both APIs, so the
	// two labs' images are laid out the same and full-screen passes read what they wrote. Vertex shaders remap depth
	// from OpenGL's -1..1 to Vulkan's 0..1 (rimgui_vk_clip in rimgui_vk.glsl), which stores the same depth values.
	VkViewport viewport{0, 0, float(width_), float(height_), 0, 1};
	VkRect2D scissor{{0, 0}, {width_, height_}};
	vkCmdSetViewport(cb, 0, 1, &viewport);
	vkCmdSetScissor(cb, 0, 1, &scissor);
}

void RenderTarget::end(VkCommandBuffer cb) const
{
	vkCmdEndRendering(cb);
}

} // namespace rimgui::rt::vk
