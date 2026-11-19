#include <rimgui/rt/lab.h>

#include <rimgui/app.h>
#include <rimgui/theme.h>
#include <rimgui/widgets.h>

#include <imgui.h>
#include <imgui_internal.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_FALLBACK
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>

#ifndef RIMGUI_KIT_SHADERS
#define RIMGUI_KIT_SHADERS ""
#endif

namespace rimgui::rt {

namespace fs = std::filesystem;

// ---- Scene ---------------------------------------------------------------------------------------------------------

mat4 Object::model() const
{
	const float d = pi / 180;
	return translate(position) * rotate(rotation_degrees.y * d, {0, 1, 0}) * rotate(rotation_degrees.x * d, {1, 0, 0}) *
		   rotate(rotation_degrees.z * d, {0, 0, 1}) * rt::scale(scale);
}

vec3 Sun::direction() const
{
	const float a = azimuth_degrees * pi / 180, e = elevation_degrees * pi / 180;
	return normalize({std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)});
}

void Scene::draw(Program &program) const
{
	for (const auto &o : objects) {
		if (!o.visible || !o.mesh) {
			continue;
		}
		const mat4 model = o.model();
		program.set("u_model", model);
		program.set("u_normal_matrix", transpose(inverse(model)));
		program.set("u_albedo", o.material.albedo);
		program.set("u_roughness", o.material.roughness);
		program.set("u_metallic", o.material.metallic);
		program.set("u_emissive", o.material.emissive);
		o.mesh->draw();
	}
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

// ---- The lab's state -----------------------------------------------------------------------------------------------

struct Lab::State {
	LabConfig config;
	std::unique_ptr<ShaderLibrary> shaders;
	std::unique_ptr<GpuTimers> timers;
	OrbitCamera camera;
	Scene scene;
	std::shared_ptr<Mesh> sphere, cube, torus, plane;

	std::array<std::unique_ptr<RenderTarget>, 2> output, display;
	int technique[2]{0, 1};
	bool compare = false;
	float split = 0.5f;
	bool dragging_split = false;
	float exposure_ev = 0;
	int tonemapper = 0; // ACES, Reinhard, Clamp

	struct Exposed {
		std::string name;
		GLuint texture = 0;
		int width = 0, height = 0;
		bool depth = false;
		int channels = 4; // One-channel textures show as grey in RGB.
	};
	std::vector<Exposed> exposed;
	std::string inspected;
	bool inspect_in_view = false;
	int channel = 0;
	float range_min = 0, range_max = 1;
	std::unique_ptr<RenderTarget> preview;

	struct Line {
		int level; // 0 info, 1 good, 2 warning, 3 error
		std::string text;
	};
	std::deque<Line> log;
	void say(int level, std::string text)
	{
		log.push_back({level, std::move(text)});
		while (log.size() > 300) {
			log.pop_front();
		}
	}

	int frame_index = 0;
	double start = 0;
	float fps = 0;
	int selected_object = -1;
	std::array<char, 512> obj_path{};
	bool capture_requested = false;
	int captures = 0;
	std::string show_at_start;
	bool started = false;
	fs::path exe_folder;
};

namespace {

fs::path utf8_path(const char *text)
{
	const std::string bytes(text);
	return fs::path(std::u8string(bytes.begin(), bytes.end()));
}

Lab::State *debug_target = nullptr;

void GLAD_API_PTR gl_debug(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar *message, const void *)
{
	if (!debug_target || severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
		return;
	}
	const int level = type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH ? 3 : 2;
	debug_target->say(level, std::string("OpenGL: ") + message);
}

fs::path program_folder()
{
#ifdef _WIN32
	wchar_t path[MAX_PATH] = L"";
	const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	return n ? fs::path(std::wstring(path, n)).parent_path() : fs::current_path();
#else
	std::error_code ignored;
	const auto exe = fs::read_symlink("/proc/self/exe", ignored);
	return ignored ? fs::current_path() : exe.parent_path();
#endif
}

void reset_state()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glActiveTexture(GL_TEXTURE0);
}

ImTextureRef image(GLuint texture)
{
	return ImTextureRef(static_cast<ImTextureID>(texture));
}

} // namespace

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
	sky.use();
	set_camera(sky);
	scene->set_lighting(sky);
	draw_fullscreen_triangle();
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
}

// ---- Lab -----------------------------------------------------------------------------------------------------------

Lab::Lab() : state_(std::make_unique<State>()) {}
Lab::~Lab() = default;

void Lab::add(std::unique_ptr<Technique> technique)
{
	techniques_.push_back(std::move(technique));
}

namespace {

void default_scene(Lab::State &s)
{
	auto add = [&](std::string name, std::shared_ptr<Mesh> mesh, vec3 at, Material m, vec3 rotation = {0, 0, 0}, vec3 size = {1, 1, 1}) {
		Object o;
		o.name = std::move(name);
		o.mesh = std::move(mesh);
		o.position = at;
		o.material = m;
		o.rotation_degrees = rotation;
		o.scale = size;
		s.scene.objects.push_back(o);
	};
	add("Floor", s.plane, {0, 0, 0}, {{0.32f, 0.32f, 0.32f}, 0.85f, 0, {0, 0, 0}});
	add("Sphere", s.sphere, {-1.3f, 0.5f, 0}, {{0.85f, 0.25f, 0.2f}, 0.3f, 0, {0, 0, 0}});
	add("Gold cube", s.cube, {0, 0.5f, 0}, {{1.0f, 0.77f, 0.34f}, 0.3f, 1, {0, 0, 0}}, {0, 30, 0});
	add("Torus", s.torus, {1.35f, 0.5f, 0}, {{0.2f, 0.45f, 0.9f}, 0.5f, 0, {0, 0, 0}}, {70, 0, 0});
	// A row of spheres from smooth to rough, for comparing BRDFs.
	for (int i = 0; i < 6; ++i) {
		const float r = 0.05f + 0.19f * float(i);
		char name[32];
		std::snprintf(name, sizeof name, "Roughness %.2f", double(r));
		add(name, s.sphere, {-1.75f + 0.7f * float(i), 0.25f, 1.4f}, {{0.7f, 0.7f, 0.7f}, r, 0, {0, 0, 0}}, {0, 0, 0}, {0.5f, 0.5f, 0.5f});
	}
}

} // namespace

int Lab::run(const LabConfig &config, int argc, char **argv)
{
	State &s = *state_;
	s.config = config;
	std::string start_technique, start_compare;
	for (int i = 1; i + 1 < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--technique") start_technique = argv[++i];
		else if (arg == "--compare") start_compare = argv[++i];
		else if (arg == "--show") s.show_at_start = argv[++i];
	}
	const auto find_technique = [&](const std::string &wanted) {
		if (wanted.empty()) {
			return -1;
		}
		for (int i = 0; i < int(techniques_.size()); ++i) {
			if (techniques_[std::size_t(i)]->name() == wanted || std::to_string(i + 1) == wanted) {
				return i;
			}
		}
		return -1;
	};
	if (const int a = find_technique(start_technique); a >= 0) {
		s.technique[0] = a;
	}
	if (const int b = find_technique(start_compare); b >= 0) {
		s.technique[1] = b;
		s.compare = true;
	}
	if (s.technique[1] >= int(techniques_.size())) {
		s.technique[1] = 0;
	}
	s.inspected = s.show_at_start;
	s.inspect_in_view = !s.show_at_start.empty();

	AppConfig app;
	app.title = config.title;
	app.gl_major = 4;
	app.gl_minor = 3;
	app.first_layout = [](const DockAreas &a) {
		ImGui::DockBuilderDockWindow("Toolbar", a.top);
		ImGui::DockBuilderDockWindow("Scene", a.left);
		ImGui::DockBuilderDockWindow("Technique", a.right);
		ImGui::DockBuilderDockWindow("Textures", a.right);
		ImGui::DockBuilderDockWindow("Viewport", a.center);
		ImGui::DockBuilderDockWindow("Timings", a.bottom);
		ImGui::DockBuilderDockWindow("Log", a.bottom_right);
	};

	app.on_start = [this, &s] {
		if (!load_gl()) {
			s.say(3, "OpenGL 3.3 could not be loaded");
			return;
		}
		s.say(0, gl_description());
		if (!has_gl43()) {
			s.say(2, "OpenGL 4.3 is not available: compute shaders cannot be used");
		}
		if (GLAD_GL_KHR_debug) {
			debug_target = &s;
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback(gl_debug, nullptr);
		}
		s.exe_folder = program_folder();
		s.shaders = std::make_unique<ShaderLibrary>();
		for (const auto &folder : s.config.shader_folders) {
			std::error_code ignored;
			if (folder.is_absolute() && fs::exists(folder, ignored)) {
				s.shaders->add_folder(folder);
			}
			if (fs::exists(s.exe_folder / folder, ignored)) {
				s.shaders->add_folder(s.exe_folder / folder);
			}
		}
		{
			std::error_code ignored;
			const fs::path kit = RIMGUI_KIT_SHADERS;
			if (!kit.empty() && fs::exists(kit, ignored)) {
				s.shaders->add_folder(kit);
			}
			s.shaders->add_folder(s.exe_folder / "shaders" / "rimgui");
			s.shaders->add_folder(s.exe_folder / "shaders");
		}
		s.shaders->on_message([&s](bool ok, const std::string &text) { s.say(ok ? 1 : 3, text); });
		s.timers = std::make_unique<GpuTimers>();
		s.sphere = std::make_shared<Mesh>(make_sphere(), "Sphere");
		s.cube = std::make_shared<Mesh>(make_cube(), "Cube");
		s.torus = std::make_shared<Mesh>(make_torus(), "Torus");
		s.plane = std::make_shared<Mesh>(make_plane(), "Plane");
		if (s.config.make_scene) {
			s.config.make_scene(s.scene);
		} else {
			default_scene(s);
		}
		if (s.config.make_scene) {
			vec3 centre;
			float radius = 1;
			s.scene.bounds(centre, radius);
			s.camera.frame(centre, radius * 0.75f);
		} else {
			// The test objects, not the whole floor.
			s.camera.target = {0, 0.35f, 0.6f};
			s.camera.distance = 5.2f;
			s.camera.yaw = 0.45f;
			s.camera.pitch = 0.42f;
		}
		for (int i = 0; i < 2; ++i) {
			s.output[std::size_t(i)] = std::make_unique<RenderTarget>(std::vector<Format>{Format::RGBA16F}, true, "Output");
			s.display[std::size_t(i)] = std::make_unique<RenderTarget>(std::vector<Format>{Format::RGBA8}, false, "Display");
		}
		s.preview = std::make_unique<RenderTarget>(std::vector<Format>{Format::RGBA8}, false, "Preview");
		s.start = ImGui::GetTime();
		s.started = true;
		if (techniques_.empty()) {
			s.say(2, "No techniques: add some with Lab::add");
		}
	};

	app.on_exit = [this, &s] {
		debug_target = nullptr;
		techniques_.clear();
		s.scene.objects.clear();
		s.sphere.reset();
		s.cube.reset();
		s.torus.reset();
		s.plane.reset();
		for (auto &t : s.output) t.reset();
		for (auto &t : s.display) t.reset();
		s.preview.reset();
		s.timers.reset();
		s.shaders.reset();
	};

	// Draws `texture` as the inspector says into `target`, sized to it.
	const auto inspect_into = [&s](const State::Exposed &e, RenderTarget &target) {
		Program &p = s.shaders->graphics("rimgui.inspect", "rimgui_fullscreen.vert", "rimgui_inspect.frag");
		target.bind();
		if (!p.ready()) {
			return;
		}
		p.use();
		p.texture("u_texture", 0, e.texture);
		p.set("u_channel", s.channel);
		p.set("u_min", s.range_min);
		p.set("u_max", s.range_max);
		p.set("u_depth", e.depth ? 1 : 0);
		p.set("u_channels", e.channels);
		p.set("u_near", s.camera.near_plane);
		p.set("u_far", s.camera.far_plane);
		draw_fullscreen_triangle();
		reset_state();
	};

	const auto render_side = [this, &s, &inspect_into](int side, int width, int height) {
		if (techniques_.empty()) {
			return;
		}
		Technique &t = *techniques_[std::size_t(s.technique[side])];
		RenderTarget &out = *s.output[std::size_t(side)];
		out.resize(width, height);
		out.bind();
		const float black[4]{0, 0, 0, 1};
		out.clear(black);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		Frame f;
		f.width = width;
		f.height = height;
		f.camera = &s.camera;
		f.view = s.camera.view();
		f.projection = s.camera.projection(float(width) / float(height));
		f.view_projection = f.projection * f.view;
		f.inverse_view_projection = inverse(f.view_projection);
		f.inverse_projection = inverse(f.projection);
		f.camera_position = s.camera.position();
		f.time = ImGui::GetTime() - s.start;
		f.seconds = ImGui::GetIO().DeltaTime;
		f.frame_index = s.frame_index;
		f.scene = &s.scene;
		f.shaders = s.shaders.get();
		f.timers = s.timers.get();
		f.output = &out;
		f.lab = this;
		f.prefix = s.compare ? (side == 0 ? "A " : "B ") : "";
		t.render(f);
		reset_state();
		f.expose("Output (HDR)", out, 0);
		f.expose_depth("Depth", out);

		RenderTarget &shown = *s.display[std::size_t(side)];
		shown.resize(width, height);
		// The inspected texture over the whole view, when asked (side A only).
		if (side == 0 && s.inspect_in_view) {
			for (const auto &e : s.exposed) {
				if (e.name == s.inspected) {
					inspect_into(e, shown);
					return;
				}
			}
		}
		auto scope = s.timers->scope(f.prefix + "Tonemap");
		Program &tonemap = s.shaders->graphics("rimgui.tonemap", "rimgui_fullscreen.vert", "rimgui_tonemap.frag");
		shown.bind();
		if (tonemap.ready()) {
			tonemap.use();
			tonemap.texture("u_hdr", 0, out.color(0));
			tonemap.set("u_exposure", std::pow(2.0f, s.exposure_ev));
			tonemap.set("u_mode", s.tonemapper);
			draw_fullscreen_triangle();
		}
		reset_state();
	};

	const auto capture = [&s, this](int side) {
		RenderTarget &shown = *s.display[std::size_t(side)];
		const int w = shown.width(), h = shown.height();
		std::vector<unsigned char> pixels(std::size_t(w) * std::size_t(h) * 4);
		glBindFramebuffer(GL_FRAMEBUFFER, shown.framebuffer());
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		const fs::path folder = s.config.capture_folder.empty() ? s.exe_folder / "captures" : s.config.capture_folder;
		std::error_code error;
		fs::create_directories(folder, error);
		std::string name = techniques_.empty() ? "capture" : techniques_[std::size_t(s.technique[side])]->name();
		for (char &c : name) {
			if (!std::isalnum(static_cast<unsigned char>(c))) {
				c = '_';
			}
		}
		const fs::path file = folder / (name + "_" + std::to_string(++s.captures) + ".png");
		// Rows bottom-up from OpenGL: written from the last row with a negative stride.
		const int ok = stbi_write_png(file.string().c_str(), w, h, 4, pixels.data() + std::size_t(w) * 4 * std::size_t(h - 1), -w * 4);
		s.say(ok ? 1 : 3, ok ? "Saved " + file.string() : "Could not write " + file.string());
	};

	const auto toolbar = [this, &s] {
		if (begin_toolbar("Toolbar")) {
			const auto combo = [this](const char *id, int &chosen) {
				ImGui::SetNextItemWidth(px(200));
				const std::string preview = techniques_.empty() ? std::string("(none)") : techniques_[std::size_t(chosen)]->name();
				if (ImGui::BeginCombo(id, preview.c_str())) {
					for (int i = 0; i < int(techniques_.size()); ++i) {
						if (ImGui::Selectable(techniques_[std::size_t(i)]->name().c_str(), chosen == i)) {
							chosen = i;
						}
					}
					ImGui::EndCombo();
				}
			};
			ImGui::TextDisabled("Technique");
			ImGui::SameLine();
			combo("##technique_a", s.technique[0]);
			ImGui::SameLine();
			ImGui::Checkbox("Compare", &s.compare);
			ImGui::SetItemTooltip("Show two techniques side by side; drag the line in the view to move the split");
			if (s.compare) {
				ImGui::SameLine();
				combo("##technique_b", s.technique[1]);
			}
			divider();
			if (button("Reload shaders", Tone::Normal, true, "Build every shader again (they also reload when saved)")) {
				s.shaders->reload_all();
				s.say(0, "Shaders reloaded");
			}
			ImGui::SameLine();
			if (button("Capture", Tone::Accent, true, "Save the view as a PNG")) {
				s.capture_requested = true;
			}
			divider();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%.0f fps   %.2f ms GPU", double(s.fps), s.timers ? s.timers->total_gpu_ms() : 0.0);
		}
		end_toolbar();
	};

	const auto viewport = [this, &s, &render_side, &capture] {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
		const bool visible = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		if (!visible) {
			ImGui::End();
			return;
		}
		const ImVec2 min = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
		const int width = std::max(int(size.x), 1), height = std::max(int(size.y), 1);
		render_side(0, width, height);
		if (s.compare) {
			render_side(1, width, height);
		}
		if (s.capture_requested) {
			s.capture_requested = false;
			capture(0);
			if (s.compare) {
				capture(1);
			}
		}
		ImGui::SetCursorScreenPos(min);
		ImGui::InvisibleButton("##view", {float(width), float(height)},
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *draw = ImGui::GetWindowDrawList();
		const ImVec2 max{min.x + float(width), min.y + float(height)};
		draw->AddImage(image(s.display[0]->color()), min, max, {0, 1}, {1, 0});
		const float split_x = min.x + s.split * float(width);
		if (s.compare) {
			draw->AddImage(image(s.display[1]->color()), {split_x, min.y}, max, {s.split, 1}, {1, 0});
			const bool near_line = hovered && std::fabs(ImGui::GetIO().MousePos.x - split_x) < px(6);
			if (near_line && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s.dragging_split = true;
			}
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				s.dragging_split = false;
			}
			if (s.dragging_split) {
				s.split = std::clamp((ImGui::GetIO().MousePos.x - min.x) / float(width), 0.02f, 0.98f);
			}
			if (near_line || s.dragging_split) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			}
			draw->AddLine({split_x, min.y}, {split_x, max.y}, IM_COL32(255, 255, 255, 220), px(2));
			const auto label = [&](float x, const std::string &text, bool right) {
				const ImVec2 t = ImGui::CalcTextSize(text.c_str());
				const float left = right ? x + px(10) : x - t.x - px(10);
				draw->AddRectFilled({left - px(6), min.y + px(8)}, {left + t.x + px(6), min.y + px(8) + t.y + px(6)}, IM_COL32(10, 12, 16, 170), px(4));
				draw->AddText({left, min.y + px(11)}, IM_COL32(235, 240, 248, 255), text.c_str());
			};
			if (!techniques_.empty()) {
				label(split_x, "A  " + techniques_[std::size_t(s.technique[0])]->name(), false);
				label(split_x, "B  " + techniques_[std::size_t(s.technique[1])]->name(), true);
			}
		}
		if (!s.dragging_split) {
			s.camera.update(hovered, size, ImGui::GetIO().DeltaTime);
		}
		if ((hovered || ImGui::IsWindowFocused()) && ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().WantTextInput) {
			vec3 centre;
			float radius = 1;
			s.scene.bounds(centre, radius);
			s.camera.frame(centre, radius * 0.75f);
		}
		if (s.inspect_in_view) {
			const std::string text = "Showing texture: " + s.inspected + "   (Textures panel)";
			draw->AddText({min.x + px(10), min.y + px(10)}, IM_COL32(255, 220, 120, 255), text.c_str());
		}
		overlay_caption(draw, min, max, "Left drag orbit   Middle drag pan   Wheel zoom   Right drag + WASD fly   F frame");
		ImGui::End();
	};

	const auto technique_panel = [this, &s] {
		ImGui::Begin("Technique");
		if (techniques_.empty()) {
			hint("No techniques yet: add one with Lab::add.");
		}
		const int sides = s.compare ? 2 : 1;
		for (int side = 0; side < sides; ++side) {
			if (techniques_.empty()) {
				break;
			}
			if (side == 1 && s.technique[1] == s.technique[0]) {
				break; // The same settings already shown.
			}
			Technique &t = *techniques_[std::size_t(s.technique[side])];
			ImGui::PushID(side);
			const std::string title = (s.compare ? (side == 0 ? "A  " : "B  ") : "") + t.name();
			if (section(title.c_str())) {
				const std::string about = t.description();
				if (!about.empty()) {
					hint(about.c_str());
				}
				fit_items_to_labels(130);
				t.settings();
				ImGui::PopItemWidth();
			}
			ImGui::PopID();
		}
		if (section("Display")) {
			fit_items_to_labels(130);
			ImGui::SliderFloat("Exposure (EV)", &s.exposure_ev, -6, 6, "%.1f");
			ImGui::Combo("Tonemapper", &s.tonemapper, "ACES (fitted)\0Reinhard\0Clamp\0");
			ImGui::PopItemWidth();
		}
		ImGui::End();
	};

	const auto scene_panel = [&s] {
		ImGui::Begin("Scene");
		if (section("Objects")) {
			ImGui::BeginChild("##objects", {0, px(170)}, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
			for (int i = 0; i < int(s.scene.objects.size()); ++i) {
				Object &o = s.scene.objects[std::size_t(i)];
				ImGui::PushID(i);
				if (!o.visible) {
					ImGui::PushStyleColor(ImGuiCol_Text, palette().muted);
				}
				if (ImGui::Selectable(o.name.c_str(), s.selected_object == i, ImGuiSelectableFlags_AllowOverlap)) {
					s.selected_object = i;
				}
				if (!o.visible) {
					ImGui::PopStyleColor();
				}
				if (row_toggle("##visible", o.visible, o.visible ? "Click to hide" : "Hidden. Click to show", ImGui::GetFrameHeight())) {
					o.visible = !o.visible;
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
			const auto add = [&s](const char *name, std::shared_ptr<Mesh> mesh) {
				Object o;
				o.name = name;
				o.mesh = std::move(mesh);
				o.position = {0, 0.5f, -1.5f};
				s.scene.objects.push_back(o);
				s.selected_object = int(s.scene.objects.size()) - 1;
			};
			if (button("+ Sphere")) add("Sphere", s.sphere);
			ImGui::SameLine();
			if (button("+ Cube")) add("Cube", s.cube);
			ImGui::SameLine();
			if (button("+ Torus")) add("Torus", s.torus);
			ImGui::SameLine();
			if (button("Remove", Tone::Stop, s.selected_object >= 0 && s.selected_object < int(s.scene.objects.size()))) {
				s.scene.objects.erase(s.scene.objects.begin() + s.selected_object);
				s.selected_object = -1;
			}
			ImGui::SetNextItemWidth(-px(70));
			ImGui::InputTextWithHint("##obj", "Path to an .obj file", s.obj_path.data(), s.obj_path.size());
			ImGui::SameLine();
			if (button("Load", Tone::Normal, s.obj_path[0] != 0)) {
				std::string error;
				const MeshData data = load_obj(utf8_path(s.obj_path.data()), &error);
				if (data.indices.empty()) {
					s.say(3, error);
				} else {
					Object o;
					o.name = utf8_path(s.obj_path.data()).stem().string();
					o.mesh = std::make_shared<Mesh>(data, o.name);
					// Scaled to about a metre and stood on the floor.
					const vec3 extent = data.bounds_max - data.bounds_min;
					const float largest = std::max({extent.x, extent.y, extent.z, 1e-6f});
					const float k = 1.2f / largest;
					o.scale = {k, k, k};
					o.position = {-(data.bounds_min.x + data.bounds_max.x) * 0.5f * k, -data.bounds_min.y * k, -(data.bounds_min.z + data.bounds_max.z) * 0.5f * k - 1.5f};
					s.scene.objects.push_back(o);
					s.selected_object = int(s.scene.objects.size()) - 1;
					s.say(1, "Loaded " + o.name + ": " + std::to_string(o.mesh->triangles()) + " triangles");
				}
			}
		}
		if (s.selected_object >= 0 && s.selected_object < int(s.scene.objects.size())) {
			Object &o = s.scene.objects[std::size_t(s.selected_object)];
			if (section(("Selected: " + o.name).c_str()) && begin_properties("##object")) {
				property("Position");
				ImGui::DragFloat3("##position", o.position.data(), 0.02f, 0, 0, "%.2f");
				property("Rotation");
				ImGui::DragFloat3("##rotation", o.rotation_degrees.data(), 0.5f, 0, 0, "%.0f");
				property("Scale");
				ImGui::DragFloat3("##scale", o.scale.data(), 0.01f, 0.001f, 100, "%.2f");
				property("Albedo");
				ImGui::ColorEdit3("##albedo", o.material.albedo.data());
				property("Roughness");
				ImGui::SliderFloat("##roughness", &o.material.roughness, 0.02f, 1);
				property("Metallic");
				ImGui::SliderFloat("##metallic", &o.material.metallic, 0, 1);
				property("Emissive");
				ImGui::ColorEdit3("##emissive", o.material.emissive.data(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
				end_properties();
			}
		}
		if (section("Sun and sky") && begin_properties("##sun")) {
			Sun &sun = s.scene.sun;
			property("Azimuth");
			ImGui::SliderFloat("##azimuth", &sun.azimuth_degrees, -180, 180, "%.0f deg");
			property("Elevation");
			ImGui::SliderFloat("##elevation", &sun.elevation_degrees, -10, 90, "%.0f deg");
			property("Colour");
			ImGui::ColorEdit3("##sun_color", sun.color.data());
			property("Intensity");
			ImGui::SliderFloat("##intensity", &sun.intensity, 0, 20, "%.1f");
			property("Sky");
			ImGui::ColorEdit3("##sky", sun.sky.data());
			property("Horizon");
			ImGui::ColorEdit3("##horizon", sun.horizon.data());
			property("Ground");
			ImGui::ColorEdit3("##ground", sun.ground.data());
			property("Ambient");
			ImGui::SliderFloat("##ambient", &sun.ambient, 0, 2, "%.2f");
			end_properties();
		}
		if (section("Camera", false) && begin_properties("##camera")) {
			float fov = s.camera.vertical_fov * 180 / pi;
			property("Field of view");
			if (ImGui::SliderFloat("##fov", &fov, 10, 120, "%.0f deg")) {
				s.camera.vertical_fov = fov * pi / 180;
			}
			property("Near");
			ImGui::DragFloat("##near", &s.camera.near_plane, 0.005f, 0.001f, 10, "%.3f");
			property("Far");
			ImGui::DragFloat("##far", &s.camera.far_plane, 1, 1, 10000, "%.0f");
			property("Fly speed");
			ImGui::SliderFloat("##speed", &s.camera.fly_speed, 0.1f, 50, "%.1f m/s");
			end_properties();
		}
		ImGui::End();
	};

	const auto textures_panel = [&s, &inspect_into] {
		ImGui::Begin("Textures");
		if (s.exposed.empty()) {
			hint("A technique lists its textures with frame.expose(...); they show here.");
		}
		if (ImGui::BeginTable("##textures", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
			ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, px(90));
			ImGui::TableSetupColumn("##kind", ImGuiTableColumnFlags_WidthFixed, px(44));
			ImGui::TableHeadersRow();
			for (const auto &e : s.exposed) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				if (ImGui::Selectable(e.name.c_str(), s.inspected == e.name, ImGuiSelectableFlags_SpanAllColumns)) {
					s.inspected = e.name;
					if (e.depth) {
						s.range_min = 0;
						s.range_max = 20;
					}
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%d x %d", e.width, e.height);
				ImGui::TableSetColumnIndex(2);
				ImGui::TextDisabled("%s", e.depth ? "depth" : e.channels == 1 ? "R" : e.channels == 2 ? "RG" : e.channels == 3 ? "RGB" : "RGBA");
			}
			ImGui::EndTable();
		}
		const State::Exposed *chosen = nullptr;
		for (const auto &e : s.exposed) {
			if (e.name == s.inspected) {
				chosen = &e;
			}
		}
		if (chosen) {
			ImGui::Spacing();
			const char *channels[]{"RGB", "R", "G", "B", "A"};
			for (int c = 0; c < 5; ++c) {
				bool on = s.channel == c;
				if (c) {
					ImGui::SameLine();
				}
				if (chip(channels[c], &on)) {
					s.channel = c;
				}
			}
			ImGui::SameLine(0, px(16));
			ImGui::Checkbox("Show in the view", &s.inspect_in_view);
			fit_items_to_labels(110);
			ImGui::DragFloatRange2(chosen->depth ? "Range (m)" : "Range", &s.range_min, &s.range_max, 0.01f, -1000, 1000, "%.2f");
			ImGui::PopItemWidth();
			const float w = ImGui::GetContentRegionAvail().x;
			const float h = w * float(std::max(chosen->height, 1)) / float(std::max(chosen->width, 1));
			s.preview->resize(int(w), int(h));
			inspect_into(*chosen, *s.preview);
			ImGui::Image(image(s.preview->color()), {w, h}, {0, 1}, {1, 0});
		}
		ImGui::End();
	};

	const auto timings_panel = [&s] {
		ImGui::Begin("Timings");
		if (s.timers && ImGui::BeginTable("##timings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, px(80));
			ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, px(80));
			ImGui::TableHeadersRow();
			for (const auto &p : s.timers->stats()) {
				if (p.gpu_ms <= 0 && p.cpu_ms <= 0) {
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(p.name.c_str());
				if (!p.gpu_history.empty() && ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::PlotLines("##history", p.gpu_history.data(), int(p.gpu_history.size()), 0, nullptr, 0, FLT_MAX, {px(240), px(60)});
					ImGui::EndTooltip();
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.3f", p.gpu_ms);
				ImGui::TableSetColumnIndex(2);
				ImGui::TextDisabled("%.3f", p.cpu_ms);
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("Frame");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.3f", s.timers->total_gpu_ms());
			ImGui::TableSetColumnIndex(2);
			ImGui::TextDisabled("%.1f fps", double(s.fps));
			ImGui::EndTable();
		}
		hint("GPU times come from timer queries a few frames late; hover a pass for its graph.");
		ImGui::End();
	};

	const auto log_panel = [&s] {
		ImGui::Begin("Log");
		if (button("Clear")) {
			s.log.clear();
		}
		ImGui::BeginChild("##log", {0, 0}, ImGuiChildFlags_Borders);
		push_code_font();
		for (const auto &line : s.log) {
			const ImVec4 colour = line.level == 3 ? palette().error : line.level == 2 ? palette().warning : line.level == 1 ? palette().good : palette().text;
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextWrapped("%s", line.text.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::PopFont();
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) {
			ImGui::SetScrollHereY(1);
		}
		ImGui::EndChild();
		ImGui::End();
	};

	const auto menu = [&s] {
		if (!ImGui::BeginMainMenuBar()) {
			return;
		}
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Capture", "Ctrl+P")) {
				s.capture_requested = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Quit")) {
				quit();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Compare A and B", nullptr, &s.compare);
			if (ImGui::MenuItem("Reload shaders", "Ctrl+R")) {
				s.shaders->reload_all();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset layout")) {
				reset_layout();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R)) {
			s.shaders->reload_all();
		}
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)) {
			s.capture_requested = true;
		}
	};

	return rimgui::run(app, [&] {
		if (!s.started || !s.shaders) {
			ImGui::Begin("Viewport");
			text_error("OpenGL could not start; see the Log.");
			ImGui::End();
			return;
		}
		s.shaders->poll();
		const float dt = ImGui::GetIO().DeltaTime;
		s.fps = s.fps <= 0 ? 1 / std::max(dt, 1e-4f) : s.fps * 0.95f + 0.05f / std::max(dt, 1e-4f);
		s.exposed.clear();
		menu();
		toolbar();
		viewport(); // First: it draws the frame, which the other panels show.
		technique_panel();
		scene_panel();
		textures_panel();
		timings_panel();
		log_panel();
		s.timers->end_frame();
		++s.frame_index;
	}, argc, argv);
}

} // namespace rimgui::rt
