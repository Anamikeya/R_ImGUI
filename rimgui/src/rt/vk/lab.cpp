#include <rimgui/rt/vk/lab.h>

#include "../lab_core.inl"

namespace rimgui::rt::vk {

namespace {

using lab_core::Say;

float half_to_float(uint16_t h)
{
	const int exponent = (h >> 10) & 31, mantissa = h & 1023;
	const float sign = (h & 0x8000) ? -1.0f : 1.0f;
	if (exponent == 31) {
		return mantissa ? std::numeric_limits<float>::quiet_NaN() : sign * std::numeric_limits<float>::infinity();
	}
	return exponent == 0 ? sign * std::ldexp(float(mantissa), -24) : sign * std::ldexp(float(mantissa + 1024), exponent - 25);
}

int channels_of(VkFormat f)
{
	switch (f) {
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R16_SFLOAT:
	case VK_FORMAT_R32_SFLOAT:
		return 1;
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R32G32_SFLOAT:
		return 2;
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
		return 3;
	default:
		return 4;
	}
}

uint32_t groups(uint32_t n)
{
	return (n + 7) / 8;
}

// A copy of an image the CPU reads once the frame that made it has finished on the GPU.
struct Readback {
	uint64_t ready_at = 0;
	std::shared_ptr<Buffer> buffer;
	int width = 0, height = 0;
	bool half_floats = false;
	std::function<void(int, int, const std::vector<unsigned char> &)> bytes_done;
	std::function<void(int, int, const std::vector<float> &)> floats_done;
};

} // namespace

// The Vulkan backend of LabCore (lab_core.inl).
struct VkBackend {
	using Scene = vk::Scene;
	using Object = vk::Object;
	using Material = vk::Material;
	using Mesh = vk::Mesh;
	using Texture = vk::Image;
	using Model = vk::Model;
	using Environment = vk::Environment;
	using Technique = vk::Technique;
	using Frame = vk::Frame;
	using Config = LabConfig;
	using Owner = Lab;
	using TextureHandle = Image *;
	static constexpr const char *api = "Vulkan";

	std::unique_ptr<ShaderLibrary> shaders;
	std::unique_ptr<GpuTimers> timers;
	std::unique_ptr<Environment> environment;
	std::array<std::unique_ptr<RenderTarget>, 2> output, display;
	std::unique_ptr<RenderTarget> preview;
	std::vector<Readback> readbacks;
	VkCommandBuffer cb = VK_NULL_HANDLE;
	bool gpu_started = false;
	Say say;

	void configure(AppConfig &app)
	{
		app.backend = Backend::Vulkan;
		app.fixed_backend = true;
	}

	bool start(const std::vector<std::filesystem::path> &folders, Say say_to)
	{
		say = std::move(say_to);
		const vulkan::Context *c = vulkan::context();
		if (!c) {
			say(3, "Vulkan is not running");
			return false;
		}
		const auto yes = [](bool b) { return b ? "yes" : "no"; };
		char line[400];
		std::snprintf(line, sizeof line, "%s, Vulkan %u.%u; ray tracing %s, ray queries %s, mesh shaders %s; validation %s", c->device_name.c_str(),
			VK_API_VERSION_MAJOR(c->api_version), VK_API_VERSION_MINOR(c->api_version), yes(c->features.ray_tracing_pipeline),
			yes(c->features.ray_query), yes(c->features.mesh_shader), c->features.validation ? "on" : "off");
		say(0, line);
		// Under validation every message is a failure of the API's rules: an error for --check.
		vulkan::on_message([this](int level, const std::string &text) {
			if (level < 2) {
				return;
			}
			const bool validating = vulkan::context() && vulkan::context()->features.validation;
			say(validating ? 3 : 2, "Vulkan: " + text);
		});
		Gpu::start();
		gpu_started = true;
		shaders = std::make_unique<ShaderLibrary>(folders);
		shaders->on_message([this](bool ok, const std::string &text) { say(ok ? 1 : 3, text); });
		timers = std::make_unique<GpuTimers>();
		environment = std::make_unique<Environment>();
		std::string error;
		if (!environment->prepare(*shaders, &error)) {
			say(3, error);
		}
		for (int i = 0; i < 2; ++i) {
			output[std::size_t(i)] = std::make_unique<RenderTarget>(std::vector<VkFormat>{VK_FORMAT_R16G16B16A16_SFLOAT}, VK_FORMAT_D32_SFLOAT,
				i == 0 ? "Output A" : "Output B");
			display[std::size_t(i)] = std::make_unique<RenderTarget>(std::vector<VkFormat>{VK_FORMAT_R8G8B8A8_UNORM}, VK_FORMAT_UNDEFINED,
				i == 0 ? "Display A" : "Display B");
		}
		preview = std::make_unique<RenderTarget>(std::vector<VkFormat>{VK_FORMAT_R8G8B8A8_UNORM}, VK_FORMAT_UNDEFINED, "Preview");
		return true;
	}

	void stop()
	{
		readbacks.clear();
		for (auto &t : output) t.reset();
		for (auto &t : display) t.reset();
		preview.reset();
		environment.reset();
		timers.reset();
		shaders.reset();
		if (gpu_started) {
			Gpu::stop();
			gpu_started = false;
		}
		vulkan::on_message(nullptr);
	}

	std::shared_ptr<Mesh> make_mesh(const MeshData &data, const std::string &name) { return std::make_shared<Mesh>(data, name); }
	Model load_model(const std::filesystem::path &path, std::string *error) { return vk::load_model(path, error); }
	std::shared_ptr<Texture> load_texture(const std::filesystem::path &path, bool srgb, std::string *error)
	{
		return vk::load_texture(path, srgb, error);
	}
	static lab_core::TextureView view(Texture &t)
	{
		return {ImTextureRef(t.imgui_texture()), int(t.width()), int(t.height()), t.desc().name};
	}

	void begin_frame()
	{
		const vulkan::Context &c = *vulkan::context();
		cb = c.command_buffer;
		timers->begin_frame();
		// Copies whose frames have finished: the shell waited for this frame slot's last use.
		for (std::size_t i = 0; i < readbacks.size();) {
			Readback &r = readbacks[i];
			if (c.frame_number < r.ready_at) {
				++i;
				continue;
			}
			const std::size_t count = std::size_t(r.width) * std::size_t(r.height) * 4;
			if (r.half_floats) {
				std::vector<float> pixels(count);
				const auto *half = static_cast<const uint16_t *>(r.buffer->mapped());
				for (std::size_t p = 0; p < count; ++p) {
					pixels[p] = half_to_float(half[p]);
				}
				if (r.floats_done) r.floats_done(r.width, r.height, pixels);
			} else {
				const auto *bytes = static_cast<const unsigned char *>(r.buffer->mapped());
				const std::vector<unsigned char> pixels(bytes, bytes + count);
				if (r.bytes_done) r.bytes_done(r.width, r.height, pixels);
			}
			readbacks.erase(readbacks.begin() + std::ptrdiff_t(i));
		}
	}

	void end_frame()
	{
		barrier(cb); // ImGui samples the pictures next, in the same command buffer.
		timers->end_frame();
	}

	void retry() {}

	void begin_side(int side, int width, int height, const float clear[4])
	{
		RenderTarget &out = *output[std::size_t(side)];
		out.resize(uint32_t(width), uint32_t(height));
		display[std::size_t(side)]->resize(uint32_t(width), uint32_t(height));
		barrier(cb);
		out.begin(cb, clear, true);
		out.end(cb);
	}

	void stopped_side(int side)
	{
		const float stopped[4]{0.18f, 0.02f, 0.03f, 1};
		output[std::size_t(side)]->begin(cb, stopped, true);
		output[std::size_t(side)]->end(cb);
	}

	void prepare(Frame &f, int side)
	{
		FrameData d{};
		d.view = f.view;
		d.projection = f.projection;
		d.view_projection = f.view_projection;
		d.inverse_view_projection = f.inverse_view_projection;
		d.inverse_projection = f.inverse_projection;
		d.camera_position = f.camera_position;
		d.time = float(f.time);
		d.resolution = {float(f.width), float(f.height)};
		d.frame_index = f.frame_index;
		d.accumulation = f.accumulation;
		const Sun &sun = f.scene->sun;
		d.sun_direction = sun.direction();
		d.sun_color = sun.color * sun.intensity;
		d.sky = sun.sky;
		d.horizon = sun.horizon;
		d.ground = sun.ground;
		d.ambient = sun.ambient;
		environment->fill(d);
		d.objects = f.scene->upload();
		d.object_count = uint32_t(f.scene->objects.size());
		f.frame_data = Gpu::get()->upload(d);
		f.objects = d.objects;
		f.cb = cb;
		f.environment = environment.get();
		f.shaders = shaders.get();
		f.timers = timers.get();
		f.output = output[std::size_t(side)].get();
		barrier(cb); // The clear, before the technique.
	}

	void finish_side(int, int, const std::string &, const Say &) { barrier(cb); }

	void expose_output(Frame &f, int side)
	{
		f.expose("Output (HDR)", *output[std::size_t(side)], 0);
		f.expose_depth("Depth", *output[std::size_t(side)]);
	}

	void tonemap(int side, float exposure, int mode, const std::string &timer_name)
	{
		auto timed = timers->scope(cb, timer_name);
		Pipeline &p = shaders->compute("rimgui.tonemap", "rimgui_vk_tonemap.comp");
		if (!p.ready()) {
			return;
		}
		RenderTarget &out = *output[std::size_t(side)];
		RenderTarget &shown = *display[std::size_t(side)];
		struct {
			uint32_t hdr, target;
			int32_t width, height;
			float exposure;
			int32_t mode;
		} c{out.color()->sampled_index(), shown.color()->storage_index(), int32_t(out.width()), int32_t(out.height()), exposure, mode};
		barrier(cb);
		p.bind(cb, c);
		vkCmdDispatch(cb, groups(out.width()), groups(out.height()), 1);
		barrier(cb);
	}

	void inspect(const lab_core::Exposed<Image *> &e, const lab_core::InspectSettings &settings, int side, int width, int height)
	{
		if (side < 0) {
			preview->resize(uint32_t(width), uint32_t(height));
		}
		RenderTarget &target = side < 0 ? *preview : *display[std::size_t(side)];
		Pipeline &p = shaders->compute("rimgui.inspect", "rimgui_vk_inspect.comp");
		if (!p.ready() || !e.texture) {
			return;
		}
		struct {
			uint32_t texture, target;
			int32_t width, height;
			int32_t channel;
			float range_min, range_max;
			int32_t depth, channels;
			float near_plane, far_plane;
		} c{e.texture->sampled_index(), target.color()->storage_index(), int32_t(target.width()), int32_t(target.height()), settings.channel,
			settings.range_min, settings.range_max, e.depth ? 1 : 0, e.channels, settings.near_plane, settings.far_plane};
		barrier(cb);
		p.bind(cb, c);
		vkCmdDispatch(cb, groups(target.width()), groups(target.height()), 1);
		barrier(cb);
	}

	void difference(float exposure, float scale)
	{
		Pipeline &p = shaders->compute("rimgui.difference", "rimgui_vk_difference.comp");
		if (!p.ready()) {
			return;
		}
		RenderTarget &shown = *display[0];
		struct {
			uint32_t a, b, target;
			int32_t width, height;
			float exposure, scale;
		} c{output[0]->color()->sampled_index(), output[1]->color()->sampled_index(), shown.color()->storage_index(), int32_t(shown.width()),
			int32_t(shown.height()), exposure, scale};
		barrier(cb);
		p.bind(cb, c);
		vkCmdDispatch(cb, groups(shown.width()), groups(shown.height()), 1);
		barrier(cb);
	}

	ImTextureRef display_texture(int side) { return ImTextureRef(display[std::size_t(side)]->color()->imgui_texture()); }
	ImTextureRef preview_texture() { return ImTextureRef(preview->color()->imgui_texture()); }

	Readback copy(Image &image, bool half_floats)
	{
		Readback r;
		r.width = int(image.width());
		r.height = int(image.height());
		r.half_floats = half_floats;
		r.buffer = Buffer::create(VkDeviceSize(image.width()) * image.height() * format_size(image.format()), 0, Memory::Readback, "Readback");
		barrier(cb);
		VkBufferImageCopy region{};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {image.width(), image.height(), 1};
		vkCmdCopyImageToBuffer(cb, image.image(), VK_IMAGE_LAYOUT_GENERAL, r.buffer->buffer(), 1, &region);
		const vulkan::Context &c = *vulkan::context();
		r.ready_at = c.frame_number + c.frames_in_flight;
		return r;
	}

	void read_display(int side, const std::function<void(int, int, const std::vector<unsigned char> &)> &done)
	{
		Readback r = copy(*display[std::size_t(side)]->color(), false);
		r.bytes_done = done;
		readbacks.push_back(std::move(r));
	}

	void read_output(int side, const std::function<void(int, int, const std::vector<float> &)> &done)
	{
		Readback r = copy(*output[std::size_t(side)]->color(), true);
		r.floats_done = done;
		readbacks.push_back(std::move(r));
	}
};

struct Lab::State : lab_core::State<VkBackend> {};

// ---- Frame ---------------------------------------------------------------------------------------------------------

void Frame::begin_output() const
{
	output->begin(cb, nullptr, false, true);
}

void Frame::end_output() const
{
	output->end(cb);
}

void Frame::draw_sky() const
{
	GraphicsDesc d;
	d.vertex = "rimgui_vk_sky.vert";
	d.fragment = "rimgui_vk_sky.frag";
	d.colors = output->color_formats();
	d.depth = output->depth_format();
	d.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	d.depth_write = false;
	d.cull = VK_CULL_MODE_NONE;
	Pipeline &sky = shaders->graphics("rimgui.sky", d);
	if (!sky.ready()) {
		return;
	}
	sky.bind(cb, constants());
	vkCmdDraw(cb, 3, 1, 0, 0);
}

void Frame::expose(const std::string &name, Image &image, int channels) const
{
	if (lab) {
		lab->state_->exposed.push_back(
			{prefix + name, &image, int(image.width()), int(image.height()), image.is_depth(), image.is_depth() ? 1 : channels});
	}
}

void Frame::expose(const std::string &name, const RenderTarget &target, std::size_t color_index) const
{
	if (color_index < target.color_count() && target.color(color_index)) {
		expose(name, *target.color(color_index), channels_of(target.color_formats()[color_index]));
	}
}

void Frame::expose_depth(const std::string &name, const RenderTarget &target) const
{
	if (target.depth()) {
		expose(name, *target.depth(), 1);
	}
}

void Frame::restart_accumulation() const
{
	if (lab) {
		lab->state_->restart_requested = true;
	}
}

// ---- Lab -----------------------------------------------------------------------------------------------------------

Lab::Lab() : state_(std::make_unique<State>()) {}
Lab::~Lab() = default;

void Lab::add(std::unique_ptr<Technique> technique)
{
	techniques_.push_back(std::move(technique));
}

int Lab::run(const LabConfig &config, int argc, char **argv)
{
	return lab_core::run_lab<VkBackend>(*state_, this, techniques_, config, argc, argv);
}

} // namespace rimgui::rt::vk
