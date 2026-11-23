#include <rimgui/rt/lab.h>

#include "lab_core.inl"

namespace rimgui::rt {

namespace {

using lab_core::Say;

Say *debug_say = nullptr;

void GLAD_API_PTR gl_debug(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar *message, const void *)
{
	if (!debug_say || severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
		return;
	}
	// Each message once: a driver repeats one every frame.
	static std::set<GLuint> seen;
	if (!seen.insert(id).second) {
		return;
	}
	const int level = type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH ? 3 : 2;
	(*debug_say)(level, std::string("OpenGL: ") + message);
}

void reset_state()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glActiveTexture(GL_TEXTURE0);
}

ImTextureRef image(GLuint texture)
{
	return ImTextureRef(static_cast<ImTextureID>(texture));
}

} // namespace

// The OpenGL backend of LabCore (lab_core.inl).
struct GlBackend {
	using Scene = rt::Scene;
	using Object = rt::Object;
	using Material = rt::Material;
	using Mesh = rt::Mesh;
	using Texture = rt::Texture;
	using Model = rt::Model;
	using Environment = rt::Environment;
	using Technique = rt::Technique;
	using Frame = rt::Frame;
	using Config = LabConfig;
	using Owner = Lab;
	using TextureHandle = GLuint;
	static constexpr const char *api = "OpenGL";

	std::unique_ptr<ShaderLibrary> shaders;
	std::unique_ptr<GpuTimers> timers;
	std::unique_ptr<Environment> environment;
	std::array<std::unique_ptr<RenderTarget>, 2> output, display;
	std::unique_ptr<RenderTarget> preview;
	std::set<std::pair<int, unsigned>> reported_gl_errors;
	Say say;

	void configure(AppConfig &app)
	{
		app.backend = Backend::OpenGL;
		app.fixed_backend = true;
		app.gl_major = 4;
		app.gl_minor = 3;
	}

	bool start(const std::vector<std::filesystem::path> &folders, Say say_to)
	{
		say = std::move(say_to);
		if (!load_gl()) {
			say(3, "OpenGL 3.3 could not be loaded");
			return false;
		}
		say(0, gl_description());
		if (!has_gl43()) {
			say(2, "OpenGL 4.3 is not available: compute shaders cannot be used");
		}
		if (GLAD_GL_KHR_debug) {
			debug_say = &say;
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback(gl_debug, nullptr);
		}
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
		shaders = std::make_unique<ShaderLibrary>();
		for (const auto &f : folders) {
			shaders->add_folder(f);
		}
		shaders->on_message([this](bool ok, const std::string &text) { say(ok ? 1 : 3, text); });
		timers = std::make_unique<GpuTimers>();
		environment = std::make_unique<Environment>();
		std::string error;
		if (!environment->prepare(*shaders, &error)) {
			say(3, error);
		}
		for (int i = 0; i < 2; ++i) {
			output[std::size_t(i)] = std::make_unique<RenderTarget>(std::vector<Format>{Format::RGBA16F}, true, "Output");
			display[std::size_t(i)] = std::make_unique<RenderTarget>(std::vector<Format>{Format::RGBA8}, false, "Display");
		}
		preview = std::make_unique<RenderTarget>(std::vector<Format>{Format::RGBA8}, false, "Preview");
		return true;
	}

	void stop()
	{
		debug_say = nullptr;
		for (auto &t : output) t.reset();
		for (auto &t : display) t.reset();
		preview.reset();
		environment.reset();
		timers.reset();
		shaders.reset();
	}

	std::shared_ptr<Mesh> make_mesh(const MeshData &data, const std::string &name) { return std::make_shared<Mesh>(data, name); }
	Model load_model(const std::filesystem::path &path, std::string *error) { return rt::load_model(path, error); }
	std::shared_ptr<Texture> load_texture(const std::filesystem::path &path, bool srgb, std::string *error)
	{
		return rt::load_texture(path, srgb, error);
	}
	static lab_core::TextureView view(Texture &t) { return {image(t.id()), t.width(), t.height(), t.name()}; }

	void begin_frame() {}
	void end_frame() { timers->end_frame(); }
	void retry() { reported_gl_errors.clear(); }
	void timings_extra() {}

	void begin_side(int side, int width, int height, const float clear[4])
	{
		RenderTarget &out = *output[std::size_t(side)];
		out.resize(width, height);
		out.bind();
		out.clear(clear);
		display[std::size_t(side)]->resize(width, height);
	}

	void stopped_side(int side)
	{
		const float stopped[4]{0.18f, 0.02f, 0.03f, 1};
		output[std::size_t(side)]->bind();
		output[std::size_t(side)]->clear(stopped);
	}

	void prepare(Frame &f, int side)
	{
		output[std::size_t(side)]->bind();
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		while (glGetError() != GL_NO_ERROR) {
		}
		f.environment = environment.get();
		f.shaders = shaders.get();
		f.timers = timers.get();
		f.output = output[std::size_t(side)].get();
	}

	void finish_side(int, int index, const std::string &name, const Say &log)
	{
		for (int i = 0; i < 8; ++i) {
			const GLenum error = glGetError();
			if (error == GL_NO_ERROR) {
				break;
			}
			if (reported_gl_errors.insert({index, error}).second) {
				char line[160];
				std::snprintf(line, sizeof line, "OpenGL error 0x%04X during %s (reported once)", error, name.c_str());
				log(3, line);
			}
		}
		reset_state();
	}

	void expose_output(Frame &f, int side)
	{
		f.expose("Output (HDR)", *output[std::size_t(side)], 0);
		f.expose_depth("Depth", *output[std::size_t(side)]);
	}

	void tonemap(int side, float exposure, int mode, const std::string &timer_name)
	{
		auto scope = timers->scope(timer_name);
		Program &p = shaders->graphics("rimgui.tonemap", "rimgui_fullscreen.vert", "rimgui_tonemap.frag");
		display[std::size_t(side)]->bind();
		if (p.ready()) {
			p.use();
			p.texture("u_hdr", 0, output[std::size_t(side)]->color(0));
			p.set("u_exposure", exposure);
			p.set("u_mode", mode);
			draw_fullscreen_triangle();
		}
		reset_state();
	}

	void inspect(const lab_core::Exposed<GLuint> &e, const lab_core::InspectSettings &settings, int side, int width, int height)
	{
		if (side < 0) {
			preview->resize(width, height);
		}
		RenderTarget *target = side < 0 ? preview.get() : display[std::size_t(side)].get();
		Program &p = shaders->graphics("rimgui.inspect", "rimgui_fullscreen.vert", "rimgui_inspect.frag");
		target->bind();
		if (p.ready()) {
			p.use();
			p.texture("u_texture", 0, e.texture);
			p.set("u_channel", settings.channel);
			p.set("u_min", settings.range_min);
			p.set("u_max", settings.range_max);
			p.set("u_depth", e.depth ? 1 : 0);
			p.set("u_channels", e.channels);
			p.set("u_near", settings.near_plane);
			p.set("u_far", settings.far_plane);
			draw_fullscreen_triangle();
		}
		reset_state();
	}

	void difference(float exposure, float scale)
	{
		Program &p = shaders->graphics("rimgui.difference", "rimgui_fullscreen.vert", "rimgui_difference.frag");
		display[0]->bind();
		if (p.ready()) {
			p.use();
			p.texture("u_a", 0, output[0]->color(0));
			p.texture("u_b", 1, output[1]->color(0));
			p.set("u_exposure", exposure);
			p.set("u_scale", scale);
			draw_fullscreen_triangle();
		}
		reset_state();
	}

	ImTextureRef display_texture(int side) const { return image(display[std::size_t(side)]->color()); }
	ImTextureRef preview_texture() const { return image(preview->color()); }

	void read_display(int side, const std::function<void(int, int, const std::vector<unsigned char> &)> &done)
	{
		RenderTarget &shown = *display[std::size_t(side)];
		const int w = shown.width(), h = shown.height();
		std::vector<unsigned char> pixels(std::size_t(w) * std::size_t(h) * 4);
		glBindFramebuffer(GL_FRAMEBUFFER, shown.framebuffer());
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		done(w, h, pixels);
	}

	void read_output(int side, const std::function<void(int, int, const std::vector<float> &)> &done)
	{
		RenderTarget &out = *output[std::size_t(side)];
		const int w = out.width(), h = out.height();
		std::vector<float> pixels(std::size_t(w) * std::size_t(h) * 4);
		glBindFramebuffer(GL_FRAMEBUFFER, out.framebuffer());
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, pixels.data());
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		done(w, h, pixels);
	}
};

struct Lab::State : lab_core::State<GlBackend> {};

// ---- Frame ---------------------------------------------------------------------------------------------------------

void Frame::expose(const std::string &name, GLuint texture, int texture_width, int texture_height, bool is_depth) const
{
	if (lab) {
		lab->state_->exposed.push_back({prefix + name, texture, texture_width, texture_height, is_depth, is_depth ? 1 : 4});
	}
}

void Frame::expose(const std::string &name, const RenderTarget &target, std::size_t color_index) const
{
	expose(name, target.color(color_index), target.width(), target.height(), false);
	if (lab && !lab->state_->exposed.empty()) {
		const Format f = target.format(color_index);
		lab->state_->exposed.back().channels = f == Format::R8 || f == Format::R16F || f == Format::R32F ? 1
											   : f == Format::RG16F || f == Format::RG32F                 ? 2
											   : f == Format::R11G11B10F                                   ? 3
																										   : 4;
	}
}

void Frame::expose_depth(const std::string &name, const RenderTarget &target) const
{
	expose(name, target.depth(), target.width(), target.height(), true);
}

void Frame::set_camera(Program &program) const
{
	program.set("u_view", view);
	program.set("u_projection", projection);
	program.set("u_view_projection", view_projection);
	program.set("u_inverse_view_projection", inverse_view_projection);
	program.set("u_inverse_projection", inverse_projection);
	program.set("u_camera_position", camera_position);
	program.set("u_time", float(time));
	program.set("u_resolution", vec2{float(width), float(height)});
	program.set("u_frame_index", frame_index);
	program.set("u_accumulation", accumulation);
}

void Frame::set_lighting(Program &program) const
{
	scene->set_lighting(program);
	if (environment) {
		environment->bind(program);
	}
}

void Frame::restart_accumulation() const
{
	if (lab) {
		lab->state_->restart_requested = true;
	}
}

void Frame::draw_sky() const
{
	Program &sky = shaders->graphics("rimgui.sky", "rimgui_sky.vert", "rimgui_sky.frag");
	if (!sky.ready()) {
		return;
	}
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	sky.use();
	set_camera(sky);
	set_lighting(sky);
	draw_fullscreen_triangle();
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glActiveTexture(GL_TEXTURE0);
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
	return lab_core::run_lab<GlBackend>(*state_, this, techniques_, config, argc, argv);
}

} // namespace rimgui::rt
