// LabCore: RenderLab's UI, settings, command line, log, checks and captures, written once over a backend that draws.
// Included by each kit's lab.cpp (OpenGL: src/rt/lab.cpp; Vulkan: src/rt/vk/lab.cpp), which defines the backend and
// instantiates run_lab with it. Nothing here calls a graphics API.
//
// A backend B gives:
//   types       Scene, Object, Material, Mesh, Texture, Model, Environment, Technique, Frame, Config, Owner (its Lab),
//               TextureHandle (what Frame::expose takes)
//   static constexpr const char *api        "OpenGL" or "Vulkan", for messages
//   void configure(AppConfig &)              the shell's backend and version
//   bool start(folders, say)                 the device's part of the lab: shaders, timers, environment, targets
//   std::shared_ptr<Mesh> make_mesh(const MeshData &, const std::string &name)
//   Model load_model(path, std::string *error)
//   std::shared_ptr<Texture> load_texture(path, bool srgb, std::string *error)
//   static TextureView view(Texture &)       for the Scene panel's map thumbnails
//   shaders, timers, environment             members: poll(), reload_all(); stats(), total_gpu_ms(); load(), clear()...
//   void begin_frame(); void end_frame()      around everything the lab draws in a frame
//   void retry()                             a stopped technique is tried again
//   void begin_side(side, w, h, clear)       the side's output and display sized, the output cleared
//   void stopped_side(side)                  the output of a technique that stopped
//   void prepare(Frame &, side)              the frame's backend fields: output, shaders, timers, command buffer...
//   void finish_side(side, index, name, say) after render(): errors the API saw, state put back
//   void expose_output(Frame &, side)        the output's colour and depth in the Textures panel
//   void tonemap(side, exposure, mode, timer_name)
//   void inspect(const Exposed &, const InspectSettings &, side or -1 for the preview, w, h)
//   void difference(exposure, scale)         |A - B| into side A's display
//   ImTextureRef display_texture(side), preview_texture()   rows bottom first (OpenGL's image layout)
//   void read_display(side, fn(w, h, rgba8)) and read_output(side, fn(w, h, rgba32f)): rows bottom first; now or a
//                                            few frames later, when the GPU has finished the frame
//   void stop()
#pragma once

#include <rimgui/app.h>
#include <rimgui/rt/camera.h>
#include <rimgui/rt/image.h>
#include <rimgui/rt/mesh_data.h>
#include <rimgui/rt/model_data.h>
#include <rimgui/rt/settings.h>
#include <rimgui/theme.h>
#include <rimgui/widgets.h>

#include <imgui.h>
#include <imgui_internal.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifndef RIMGUI_KIT_SHADERS
#define RIMGUI_KIT_SHADERS ""
#endif

namespace rimgui::rt::lab_core {

namespace fs = std::filesystem;

using Say = std::function<void(int level, std::string text)>;

// A texture a technique listed for the Textures panel this frame.
template <typename Handle>
struct Exposed {
	std::string name;
	Handle texture{};
	int width = 0, height = 0;
	bool depth = false;
	int channels = 4; // One-channel textures show as grey in RGB.
};

struct InspectSettings {
	int channel = 0; // 0 RGB, 1 R, 2 G, 3 B, 4 A
	float range_min = 0, range_max = 1;
	float near_plane = 0.05f, far_plane = 500;
};

struct TextureView {
	ImTextureRef id;
	int width = 0, height = 0;
	std::string name;
};

inline fs::path utf8_path(const std::string &bytes)
{
	return fs::path(std::u8string(bytes.begin(), bytes.end()));
}

inline std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return s;
}

inline fs::path program_folder()
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

inline std::string safe_name(std::string s)
{
	for (char &c : s) {
		if (!std::isalnum(static_cast<unsigned char>(c))) {
			c = '_';
		}
	}
	return s;
}

// The shader folders a lab searches, in order: the technique folders given (absolute ones, and relative ones beside the
// program), the kit's folder in the source tree, then the copies beside the program.
inline std::vector<fs::path> shader_folders(const std::vector<fs::path> &given, const fs::path &exe_folder)
{
	std::vector<fs::path> out;
	std::error_code ignored;
	for (const auto &folder : given) {
		if (folder.is_absolute() && fs::exists(folder, ignored)) {
			out.push_back(folder);
		}
		if (fs::exists(exe_folder / folder, ignored)) {
			out.push_back(exe_folder / folder);
		}
	}
	const fs::path kit = RIMGUI_KIT_SHADERS;
	if (!kit.empty() && fs::exists(kit, ignored)) {
		out.push_back(kit);
	}
	out.push_back(exe_folder / "shaders" / "rimgui");
	out.push_back(exe_folder / "shaders");
	return out;
}

constexpr std::array<std::array<int, 2>, 5> resolutions{{{0, 0}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}}};

// ---- The lab's state -----------------------------------------------------------------------------------------------

template <typename B>
struct State {
	using Scene = typename B::Scene;
	using Object = typename B::Object;
	using Mesh = typename B::Mesh;

	typename B::Config config;
	B gpu;
	OrbitCamera camera;
	Scene scene;
	std::shared_ptr<Mesh> sphere, cube, torus, plane;

	int technique[2]{0, 1};
	bool compare = false;
	int compare_mode = 0; // 0 split, 1 difference
	float split = 0.5f;
	bool dragging_split = false;
	float difference_scale = 4;
	float exposure_ev = 0;
	int tonemapper = 0; // ACES, Reinhard, Clamp
	float render_scale = 1;
	int resolution = 0; // The view's size, 1280x720, 1920x1080, 2560x1440, 3840x2160; -1 fixed from the command line
	int render_width = 1, render_height = 1;

	std::vector<Exposed<typename B::TextureHandle>> exposed;
	std::string inspected;
	bool inspect_in_view = false;
	InspectSettings inspect;

	struct Line {
		int level; // 0 info, 1 good, 2 warning, 3 error
		std::string text;
	};
	std::deque<Line> log;
	std::ofstream log_file;
	int errors_logged = 0;
	void say(int level, std::string text)
	{
		if (level == 3) {
			++errors_logged;
		}
		if (log_file) {
			static const char *const names[]{"info", "ok", "warning", "error"};
			log_file << names[std::clamp(level, 0, 3)] << ": " << text << '\n';
			log_file.flush();
		}
		log.push_back({level, std::move(text)});
		while (log.size() > 400) {
			log.pop_front();
		}
	}
	Say sayer()
	{
		return [this](int level, std::string text) { say(level, std::move(text)); };
	}

	int frame_index = 0;
	float fps = 0;
	bool paused = false;
	double time = 0;
	int accumulation = 0;
	std::size_t last_fingerprint = 0;
	bool restart_requested = true;
	bool technique_touched = false;
	std::vector<std::string> failed; // Per technique: why it stopped; empty while it runs.

	int selected_object = -1;
	std::array<char, 1024> model_path{}, environment_path{};
	bool capture_requested = false, capture_hdr_requested = false;
	int captures = 0;
	std::string show_at_start;
	bool started = false;
	fs::path exe_folder;

	fs::path settings_path;
	bool settings_enabled = true;
	std::string saved_text;
	double last_save_check = 0;

	bool check = false, check_asked = false;
	std::string save_view; // --save-view: the picture as a PNG after 90 frames
	std::vector<std::string> start_models;
	std::string start_environment;
	std::string start_technique, start_compare;
	bool start_difference = false;
	int fixed_width = 0, fixed_height = 0;
	bool camera_given = false;
	float camera_values[6]{};
	bool empty_scene = false;
};

// ---- The scene -----------------------------------------------------------------------------------------------------

template <typename B>
void default_scene(State<B> &s)
{
	using Object = typename B::Object;
	using Material = typename B::Material;
	auto add = [&](std::string name, std::shared_ptr<typename B::Mesh> mesh, vec3 at, Material m, vec3 rotation = {0, 0, 0}, vec3 size = {1, 1, 1}) {
		Object o;
		o.name = std::move(name);
		o.mesh = std::move(mesh);
		o.position = at;
		o.material = m;
		o.rotation_degrees = rotation;
		o.scale = size;
		s.scene.objects.push_back(o);
	};
	const auto material = [](vec3 albedo, float roughness, float metallic) {
		Material m;
		m.albedo = albedo;
		m.roughness = roughness;
		m.metallic = metallic;
		return m;
	};
	add("Floor", s.plane, {0, 0, 0}, material({0.32f, 0.32f, 0.32f}, 0.85f, 0));
	add("Sphere", s.sphere, {-1.3f, 0.5f, 0}, material({0.85f, 0.25f, 0.2f}, 0.3f, 0));
	add("Gold cube", s.cube, {0, 0.5f, 0}, material({1.0f, 0.77f, 0.34f}, 0.3f, 1), {0, 30, 0});
	add("Torus", s.torus, {1.35f, 0.5f, 0}, material({0.2f, 0.45f, 0.9f}, 0.5f, 0), {70, 0, 0});
	// A row of spheres from smooth to rough, for comparing BRDFs.
	for (int i = 0; i < 6; ++i) {
		const float r = 0.05f + 0.19f * float(i);
		char name[32];
		std::snprintf(name, sizeof name, "Roughness %.2f", double(r));
		add(name, s.sphere, {-1.75f + 0.7f * float(i), 0.25f, 1.4f}, material({0.7f, 0.7f, 0.7f}, r, 0), {0, 0, 0}, {0.5f, 0.5f, 0.5f});
	}
}

// A model's parts into the scene: scaled to about a metre and a half and stood on the floor behind the test shapes,
// to the right of models loaded before. Returns the index of its first part, or -1.
template <typename B>
int add_model(State<B> &s, const fs::path &path)
{
	std::string error;
	auto model = s.gpu.load_model(path, &error);
	if (model.parts.empty()) {
		s.say(3, error.empty() ? path.string() + ": nothing to draw in it" : error);
		return -1;
	}
	for (const auto &w : model.warnings) {
		s.say(2, path.filename().string() + ": " + w);
	}
	const vec3 extent = model.bounds_max - model.bounds_min;
	const float largest = std::max({extent.x, extent.y, extent.z, 1e-6f});
	const float k = 1.5f / largest;
	std::set<std::string> sources;
	for (const auto &o : s.scene.objects) {
		if (!o.source.empty()) {
			sources.insert(o.source);
		}
	}
	const float across = 1.8f * float(sources.size());
	const vec3 at{-(model.bounds_min.x + model.bounds_max.x) * 0.5f * k + across, -model.bounds_min.y * k,
		-(model.bounds_min.z + model.bounds_max.z) * 0.5f * k - 1.8f};
	const int first = int(s.scene.objects.size());
	std::size_t triangles = 0, maps = 0;
	for (auto &part : model.parts) {
		part.scale = {k, k, k};
		part.position = at;
		triangles += part.mesh->triangles();
		maps += std::size_t(std::popcount(unsigned(part.material.maps())));
		s.scene.objects.push_back(std::move(part));
	}
	char size[160];
	std::snprintf(size, sizeof size, "; %.2f x %.2f x %.2f in the file, scaled by %.3g to stand at (%.2f, %.2f, %.2f)", double(extent.x),
		double(extent.y), double(extent.z), double(k), double(at.x), double(at.y), double(at.z));
	s.say(1, "Loaded " + path.filename().string() + ": " + std::to_string(model.parts.size()) + " parts, " + std::to_string(triangles) +
				 " triangles, " + std::to_string(maps) + " texture maps" + size);
	return first;
}

template <typename B>
bool load_environment(State<B> &s, const fs::path &path)
{
	std::string error;
	if (!s.gpu.environment->load(path, *s.gpu.shaders, &error)) {
		s.say(3, error);
		return false;
	}
	char line[160];
	std::snprintf(line, sizeof line, " (built in %.0f ms)", s.gpu.environment->build_ms());
	s.say(1, "Environment " + path.filename().string() + line);
	const std::string text = path.string();
	std::snprintf(s.environment_path.data(), s.environment_path.size(), "%s", text.c_str());
	return true;
}

// A file dropped on the window or typed in: a model, an environment, or an image for the selected object's albedo.
template <typename B>
void open_file(State<B> &s, const fs::path &path)
{
	const std::string extension = lower(path.extension().string());
	if (is_model_file(path)) {
		s.selected_object = add_model(s, path);
	} else if (extension == ".hdr") {
		load_environment(s, path);
	} else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp") {
		if (s.selected_object < 0 || s.selected_object >= int(s.scene.objects.size())) {
			s.say(2, path.filename().string() + ": select an object first; an image dropped on the window becomes its albedo map");
			return;
		}
		std::string error;
		auto t = s.gpu.load_texture(path, true, &error);
		if (!t) {
			s.say(3, error);
			return;
		}
		s.scene.objects[std::size_t(s.selected_object)].material.albedo_map = t;
		s.say(1, path.filename().string() + " is now the albedo map of " + s.scene.objects[std::size_t(s.selected_object)].name);
	} else {
		s.say(2, path.filename().string() + ": the lab opens .gltf, .glb and .obj models, .hdr environments and PNG/JPEG/TGA/BMP images");
	}
}

// ---- Settings ------------------------------------------------------------------------------------------------------

template <typename B, typename Techniques>
void serialize_lab(State<B> &s, const Techniques &techniques, Settings &st)
{
	st.push("camera");
	st.value("target", s.camera.target);
	st.value("distance", s.camera.distance);
	st.value("yaw", s.camera.yaw);
	st.value("pitch", s.camera.pitch);
	st.value("vertical_fov", s.camera.vertical_fov);
	st.value("near", s.camera.near_plane);
	st.value("far", s.camera.far_plane);
	st.value("fly_speed", s.camera.fly_speed);
	st.pop();
	st.push("sun");
	Sun &sun = s.scene.sun;
	st.value("azimuth", sun.azimuth_degrees);
	st.value("elevation", sun.elevation_degrees);
	st.value("color", sun.color);
	st.value("intensity", sun.intensity);
	st.value("sky", sun.sky);
	st.value("horizon", sun.horizon);
	st.value("ground", sun.ground);
	st.value("ambient", sun.ambient);
	st.pop();
	st.push("display");
	st.value("exposure", s.exposure_ev);
	st.value("tonemapper", s.tonemapper);
	st.value("render_scale", s.render_scale);
	st.value("resolution", s.resolution);
	st.pop();
	st.push("compare");
	st.value("on", s.compare);
	st.value("mode", s.compare_mode);
	st.value("split", s.split);
	st.value("difference_scale", s.difference_scale);
	st.pop();
	for (int side = 0; side < 2 && !techniques.empty(); ++side) {
		std::string name = techniques[std::size_t(s.technique[side])]->name();
		st.value(side == 0 ? "technique.a" : "technique.b", name);
		if (st.loading()) {
			for (int i = 0; i < int(techniques.size()); ++i) {
				if (techniques[std::size_t(i)]->name() == name) {
					s.technique[side] = i;
				}
			}
		}
	}
	auto &env = *s.gpu.environment;
	st.push("environment");
	std::string path = env.path();
	st.value("path", path);
	st.value("intensity", env.intensity);
	st.value("rotation", env.rotation_degrees);
	st.value("as_sky", env.show_as_sky);
	if (st.loading() && !path.empty() && path != env.path()) {
		load_environment(s, utf8_path(path));
	}
	st.pop();
	// Models from files: each file once, with the transform of its first part.
	st.push("models");
	if (st.saving()) {
		std::vector<std::string> seen;
		for (const auto &o : s.scene.objects) {
			if (o.source.empty() || std::find(seen.begin(), seen.end(), o.source) != seen.end()) {
				continue;
			}
			const std::string index = std::to_string(seen.size());
			std::string source = o.source;
			vec3 position = o.position, rotation = o.rotation_degrees, scale = o.scale;
			st.value(index + ".path", source);
			st.value(index + ".position", position);
			st.value(index + ".rotation", rotation);
			st.value(index + ".scale", scale);
			seen.push_back(o.source);
		}
		int count = int(seen.size());
		st.value("count", count);
	} else {
		int count = 0;
		st.value("count", count);
		for (int i = 0; i < std::min(count, 64); ++i) {
			const std::string index = std::to_string(i);
			std::string source;
			st.value(index + ".path", source);
			std::error_code ignored;
			if (source.empty() || !fs::exists(utf8_path(source), ignored)) {
				s.say(2, "A model the lab had last time is no longer there: " + source);
				continue;
			}
			const int first = add_model(s, utf8_path(source));
			if (first < 0) {
				continue;
			}
			vec3 position = s.scene.objects[std::size_t(first)].position, rotation{0, 0, 0}, scale = s.scene.objects[std::size_t(first)].scale;
			st.value(index + ".position", position);
			st.value(index + ".rotation", rotation);
			st.value(index + ".scale", scale);
			for (std::size_t p = std::size_t(first); p < s.scene.objects.size(); ++p) {
				s.scene.objects[p].position = position;
				s.scene.objects[p].rotation_degrees = rotation;
				s.scene.objects[p].scale = scale;
			}
		}
	}
	st.pop();
	for (const auto &t : techniques) {
		st.push("technique." + safe_name(t->name()));
		t->serialize(st);
		st.pop();
	}
}

// ---- Run -----------------------------------------------------------------------------------------------------------

// Runs the lab: `owner` is the backend's Lab (frames point back to it), `techniques` its techniques.
template <typename B>
int run_lab(State<B> &s, typename B::Owner *owner, std::vector<std::unique_ptr<typename B::Technique>> &techniques,
	const typename B::Config &config, int argc, char **argv)
{
	using Technique = typename B::Technique;
	using Frame = typename B::Frame;
	using Object = typename B::Object;
	using Material = typename B::Material;
	using Texture = typename B::Texture;
	s.config = config;
	bool screenshot = false;
	std::string log_path;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const bool more = i + 1 < argc;
		if (arg == "--technique" && more) s.start_technique = argv[++i];
		else if (arg == "--compare" && more) s.start_compare = argv[++i];
		else if (arg == "--difference") s.start_difference = true;
		else if (arg == "--show" && more) s.show_at_start = argv[++i];
		else if (arg == "--environment" && more) s.start_environment = argv[++i];
		else if (arg == "--model" && more) s.start_models.push_back(argv[++i]);
		else if (arg == "--no-settings") s.settings_enabled = false;
		else if (arg == "--check") s.check = true;
		else if (arg == "--log" && more) log_path = argv[++i];
		else if (arg == "--save-view" && more) s.save_view = argv[++i];
		else if (arg == "--screenshot") screenshot = true;
		else if (arg == "--scene" && more) s.empty_scene = std::string(argv[++i]) == "empty";
		else if (arg == "--camera" && more) {
			float *v = s.camera_values;
			s.camera_given = std::sscanf(argv[++i], "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6;
		} else if (arg == "--resolution" && more) {
			int w = 0, h = 0;
			if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0 && w <= 16384 && h <= 16384) {
				s.fixed_width = w;
				s.fixed_height = h;
			}
		}
	}
	// Runs that make pictures or check them start from the defaults, so they come out the same every time.
	if (screenshot || s.check || !s.save_view.empty()) {
		s.settings_enabled = false;
	}
	if (!log_path.empty()) {
		s.log_file.open(utf8_path(log_path), std::ios::trunc);
	}
	s.failed.assign(techniques.size(), {});
	s.inspected = s.show_at_start;
	s.inspect_in_view = !s.show_at_start.empty();

	AppConfig app;
	app.title = config.title;
	s.gpu.configure(app);
	app.first_layout = [](const DockAreas &a) {
		ImGui::DockBuilderDockWindow("Toolbar", a.top);
		ImGui::DockBuilderDockWindow("Scene", a.left);
		ImGui::DockBuilderDockWindow("Technique", a.right);
		ImGui::DockBuilderDockWindow("Textures", a.right);
		ImGui::DockBuilderDockWindow("Viewport", a.center);
		ImGui::DockBuilderDockWindow("Timings", a.bottom);
		ImGui::DockBuilderDockWindow("Log", a.bottom_right);
	};
	app.on_drop = [&s](const std::vector<std::string> &paths) {
		if (!s.started) {
			return;
		}
		for (const auto &p : paths) {
			open_file(s, utf8_path(p));
		}
	};

	const auto find_technique = [&techniques](const std::string &wanted) {
		if (wanted.empty()) {
			return -1;
		}
		for (int i = 0; i < int(techniques.size()); ++i) {
			if (techniques[std::size_t(i)]->name() == wanted || std::to_string(i + 1) == wanted) {
				return i;
			}
		}
		return -1;
	};

	const auto save_settings = [&techniques, &s](bool force) {
		if (!s.settings_enabled || s.settings_path.empty() || !s.started) {
			return;
		}
		Settings st(Settings::Mode::Save);
		serialize_lab(s, techniques, st);
		const std::string text = st.text();
		if (!force && text == s.saved_text) {
			return;
		}
		if (st.write_file(s.settings_path)) {
			s.saved_text = text;
		} else if (force) {
			s.say(2, "The lab's settings could not be saved to " + s.settings_path.string());
		}
	};

	app.on_start = [&techniques, &s, &find_technique] {
		s.exe_folder = program_folder();
		try {
			if (!s.gpu.start(shader_folders(s.config.shader_folders, s.exe_folder), s.sayer())) {
				return;
			}
			s.sphere = s.gpu.make_mesh(make_sphere(), "Sphere");
			s.cube = s.gpu.make_mesh(make_cube(), "Cube");
			s.torus = s.gpu.make_mesh(make_torus(), "Torus");
			s.plane = s.gpu.make_mesh(make_plane(), "Plane");
		} catch (const std::exception &e) {
			s.say(3, std::string(B::api) + " could not start: " + e.what());
			return;
		}
		if (s.config.make_scene) {
			s.config.make_scene(s.scene);
			vec3 centre;
			float radius = 1;
			s.scene.bounds(centre, radius);
			s.camera.frame(centre, radius * 0.75f);
		} else {
			default_scene(s);
			if (s.empty_scene) {
				s.scene.objects.resize(1); // The floor.
			}
			// The test objects, not the whole floor.
			s.camera.target = {0, 0.35f, 0.6f};
			s.camera.distance = 5.2f;
			s.camera.yaw = 0.45f;
			s.camera.pitch = 0.42f;
		}

		// The last run's state, then what the command line asks for on top.
		s.settings_path = s.config.settings_file.empty() ? s.exe_folder / (safe_name(s.config.title) + ".settings") : s.config.settings_file;
		if (s.settings_enabled) {
			Settings st(Settings::Mode::Load);
			if (st.read_file(s.settings_path)) {
				serialize_lab(s, techniques, st);
				s.say(0, "Settings from " + s.settings_path.string());
			}
			Settings saved(Settings::Mode::Save);
			serialize_lab(s, techniques, saved);
			s.saved_text = saved.text();
		}
		if (const int a = find_technique(s.start_technique); a >= 0) {
			s.technique[0] = a;
		} else if (!s.start_technique.empty()) {
			s.say(3, "No technique called \"" + s.start_technique + "\"");
		}
		if (const int b = find_technique(s.start_compare); b >= 0) {
			s.technique[1] = b;
			s.compare = true;
			s.compare_mode = s.start_difference ? 1 : 0;
		} else if (!s.start_compare.empty()) {
			s.say(3, "No technique called \"" + s.start_compare + "\"");
		}
		for (int &t : s.technique) {
			if (t >= int(techniques.size())) {
				t = 0;
			}
		}
		if (s.camera_given) {
			const float *v = s.camera_values;
			s.camera.target = {v[0], v[1], v[2]};
			s.camera.distance = std::max(v[3], 0.01f);
			s.camera.yaw = v[4] * pi / 180;
			s.camera.pitch = std::clamp(v[5], -89.0f, 89.0f) * pi / 180;
		}
		if (!s.start_environment.empty()) {
			load_environment(s, utf8_path(s.start_environment));
		}
		for (const auto &m : s.start_models) {
			add_model(s, utf8_path(m));
		}
		if (s.fixed_width > 0) {
			s.resolution = -1;
		}
		s.started = true;
		if (techniques.empty()) {
			s.say(2, "No techniques: add some with Lab::add");
		}
	};

	app.on_exit = [&techniques, &s, &save_settings] {
		save_settings(true);
		techniques.clear();
		s.scene.objects.clear();
		s.sphere.reset();
		s.cube.reset();
		s.torus.reset();
		s.plane.reset();
		s.exposed.clear();
		s.gpu.stop();
	};

	const auto render_side = [owner, &techniques, &s](int side, int width, int height) {
		if (techniques.empty()) {
			return;
		}
		const int index = s.technique[side];
		Technique &t = *techniques[std::size_t(index)];
		const float black[4]{0, 0, 0, 1};
		s.gpu.begin_side(side, width, height, black);
		const std::string prefix = s.compare ? (side == 0 ? "A " : "B ") : "";
		if (!s.failed[std::size_t(index)].empty()) {
			s.gpu.stopped_side(side);
			s.gpu.tonemap(side, std::pow(2.0f, s.exposure_ev), s.tonemapper, prefix + "Tonemap");
			return;
		}
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
		f.time = s.time;
		f.seconds = s.paused ? 0.0f : ImGui::GetIO().DeltaTime;
		f.frame_index = s.frame_index;
		f.accumulation = s.accumulation;
		f.scene = &s.scene;
		f.lab = owner;
		f.prefix = prefix;
		s.gpu.prepare(f, side);
		try {
			t.render(f);
		} catch (const std::exception &e) {
			s.failed[std::size_t(index)] = e.what();
			s.say(3, t.name() + " stopped: " + e.what());
		} catch (...) {
			s.failed[std::size_t(index)] = "an exception that is not a std::exception";
			s.say(3, t.name() + " stopped: an exception that is not a std::exception");
		}
		s.gpu.finish_side(side, index, t.name(), s.sayer());
		s.gpu.expose_output(f, side);
		// The inspected texture over the whole view, when asked (side A only).
		if (side == 0 && s.inspect_in_view) {
			for (const auto &e : s.exposed) {
				if (e.name == s.inspected) {
					s.gpu.inspect(e, s.inspect, side, width, height);
					return;
				}
			}
		}
		s.gpu.tonemap(side, std::pow(2.0f, s.exposure_ev), s.tonemapper, prefix + "Tonemap");
	};

	const auto capture = [&s, &techniques](int side, bool hdr) {
		const std::string name = techniques.empty() ? std::string("capture") : safe_name(techniques[std::size_t(s.technique[side])]->name());
		const fs::path folder = s.config.capture_folder.empty() ? s.exe_folder / "captures" : s.config.capture_folder;
		std::error_code error;
		fs::create_directories(folder, error);
		const fs::path file = folder / (name + "_" + std::to_string(++s.captures) + (hdr ? ".hdr" : ".png"));
		State<B> *state = &s;
		if (hdr) {
			s.gpu.read_output(side, [state, file](int w, int h, const std::vector<float> &rgba) {
				std::vector<float> rgb(std::size_t(w) * std::size_t(h) * 3);
				for (std::size_t i = 0; i < rgb.size() / 3; ++i) {
					for (std::size_t c = 0; c < 3; ++c) rgb[i * 3 + c] = rgba[i * 4 + c];
				}
				const bool ok = write_hdr_bottom_up(file, w, h, rgb.data());
				state->say(ok ? 1 : 3, ok ? "Saved " + file.string() : "Could not write " + file.string());
			});
		} else {
			s.gpu.read_display(side, [state, file](int w, int h, const std::vector<unsigned char> &rgba) {
				const bool ok = write_png_bottom_up(file, w, h, rgba.data());
				state->say(ok ? 1 : 3, ok ? "Saved " + file.string() : "Could not write " + file.string());
			});
		}
	};

	const auto technique_combo = [&techniques](const char *id, int &chosen) {
		ImGui::SetNextItemWidth(px(190));
		const std::string preview = techniques.empty() ? std::string("(none)") : techniques[std::size_t(chosen)]->name();
		if (ImGui::BeginCombo(id, preview.c_str())) {
			for (int i = 0; i < int(techniques.size()); ++i) {
				if (ImGui::Selectable(techniques[std::size_t(i)]->name().c_str(), chosen == i)) {
					chosen = i;
				}
			}
			ImGui::EndCombo();
		}
	};

	const auto toolbar = [&s, &technique_combo] {
		if (begin_toolbar("Toolbar")) {
			ImGui::TextDisabled("Technique");
			ImGui::SameLine();
			technique_combo("##technique_a", s.technique[0]);
			ImGui::SameLine();
			ImGui::Checkbox("Compare", &s.compare);
			ImGui::SetItemTooltip("Two techniques side by side (drag the line in the view), or the difference between them");
			if (s.compare) {
				ImGui::SameLine();
				technique_combo("##technique_b", s.technique[1]);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(px(110));
				ImGui::Combo("##compare_mode", &s.compare_mode, "Split\0Difference\0");
			}
			divider();
			ImGui::Checkbox("Pause time", &s.paused);
			ImGui::SetItemTooltip("Stops frame.time, so animated techniques hold still for comparing and capturing");
			divider();
			if (button("Reload shaders", Tone::Normal, true, "Build every shader again (they also reload when saved). Ctrl+R")) {
				s.gpu.shaders->reload_all();
				s.say(0, "Shaders reloaded");
			}
			ImGui::SameLine();
			if (button("Capture", Tone::Accent, true, "Save the view as a PNG (both sides when comparing). Ctrl+P")) {
				s.capture_requested = true;
			}
			ImGui::SameLine();
			if (button("HDR", Tone::Normal, true, "Save the technique's linear HDR output as a Radiance .hdr file, before tonemapping")) {
				s.capture_hdr_requested = true;
			}
			divider();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s   %.0f fps   %.2f ms GPU   %dx%d   frame %d", B::api, double(s.fps), s.gpu.timers ? s.gpu.timers->total_gpu_ms() : 0.0,
				s.render_width, s.render_height, s.accumulation);
			ImGui::SetItemTooltip("The last number counts frames since anything changed: progressive techniques accumulate over them");
		}
		end_toolbar();
	};

	const auto viewport = [&techniques, &s, &render_side, &capture] {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
		const bool visible = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		if (!visible) {
			ImGui::End();
			return;
		}
		const ImVec2 origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
		// The size drawn at: the view's, scaled, or a fixed resolution shown letterboxed.
		int width = std::max(int(size.x * s.render_scale), 1), height = std::max(int(size.y * s.render_scale), 1);
		if (s.resolution < 0) {
			width = s.fixed_width;
			height = s.fixed_height;
		} else if (s.resolution > 0) {
			width = resolutions[std::size_t(s.resolution)][0];
			height = resolutions[std::size_t(s.resolution)][1];
		}
		s.render_width = width;
		s.render_height = height;
		const float aspect = float(width) / float(height);
		ImVec2 shown_size = size;
		if (s.resolution != 0) {
			shown_size = {size.x, size.x / aspect};
			if (shown_size.y > size.y) {
				shown_size = {size.y * aspect, size.y};
			}
		}
		const ImVec2 min{origin.x + (size.x - shown_size.x) * 0.5f, origin.y + (size.y - shown_size.y) * 0.5f};
		const ImVec2 max{min.x + shown_size.x, min.y + shown_size.y};

		render_side(0, width, height);
		if (s.compare) {
			render_side(1, width, height);
			if (s.compare_mode == 1 && !s.inspect_in_view) {
				s.gpu.difference(std::pow(2.0f, s.exposure_ev), s.difference_scale);
			}
		}
		if (s.capture_requested || s.capture_hdr_requested) {
			const bool hdr = s.capture_hdr_requested;
			s.capture_requested = s.capture_hdr_requested = false;
			capture(0, hdr);
			if (s.compare && s.compare_mode == 0) {
				capture(1, hdr);
			}
		}
		ImGui::SetCursorScreenPos(origin);
		ImGui::InvisibleButton("##view", {std::max(size.x, 1.0f), std::max(size.y, 1.0f)},
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(8, 9, 11, 255));
		// Pictures are stored bottom row first (OpenGL's layout, which the Vulkan kit keeps): shown flipped.
		draw->AddImage(s.gpu.display_texture(0), min, max, {0, 1}, {1, 0});
		const auto label = [&](float x, const std::string &text, bool right) {
			const ImVec2 t = ImGui::CalcTextSize(text.c_str());
			const float left = right ? x + px(10) : x - t.x - px(10);
			draw->AddRectFilled({left - px(6), min.y + px(8)}, {left + t.x + px(6), min.y + px(8) + t.y + px(6)}, IM_COL32(10, 12, 16, 170), px(4));
			draw->AddText({left, min.y + px(11)}, IM_COL32(235, 240, 248, 255), text.c_str());
		};
		if (s.compare && s.compare_mode == 0 && !techniques.empty()) {
			const float split_x = min.x + s.split * shown_size.x;
			draw->AddImage(s.gpu.display_texture(1), {split_x, min.y}, max, {s.split, 1}, {1, 0});
			const bool near_line = hovered && std::fabs(ImGui::GetIO().MousePos.x - split_x) < px(6);
			if (near_line && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s.dragging_split = true;
			}
			if (s.dragging_split) {
				s.split = std::clamp((ImGui::GetIO().MousePos.x - min.x) / shown_size.x, 0.02f, 0.98f);
			}
			if (near_line || s.dragging_split) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			}
			draw->AddLine({split_x, min.y}, {split_x, max.y}, IM_COL32(255, 255, 255, 220), px(2));
			label(split_x, "A  " + techniques[std::size_t(s.technique[0])]->name(), false);
			label(split_x, "B  " + techniques[std::size_t(s.technique[1])]->name(), true);
		} else if (s.compare && s.compare_mode == 1 && !techniques.empty()) {
			char text[96];
			std::snprintf(text, sizeof text, "|A - B| x %.0f   (black: the same)", double(s.difference_scale));
			label(min.x + px(4), text, true);
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s.dragging_split = false;
		}
		if (!s.dragging_split && s.camera.update(hovered, shown_size, ImGui::GetIO().DeltaTime)) {
			s.restart_requested = true;
		}
		if ((hovered || ImGui::IsWindowFocused()) && ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().WantTextInput) {
			vec3 centre;
			float radius = 1;
			s.scene.bounds(centre, radius);
			s.camera.frame(centre, radius * 0.75f);
		}
		float y = min.y + px(10);
		if (s.inspect_in_view) {
			const std::string text = "Showing texture: " + s.inspected + "   (Textures panel)";
			draw->AddText({min.x + px(10), y}, IM_COL32(255, 220, 120, 255), text.c_str());
			y += ImGui::GetTextLineHeightWithSpacing();
		}
		for (int side = 0; side < (s.compare ? 2 : 1) && !techniques.empty(); ++side) {
			const auto &why = s.failed[std::size_t(s.technique[side])];
			if (!why.empty()) {
				const std::string text = techniques[std::size_t(s.technique[side])]->name() + " stopped: " + why + "   (Retry in the Technique panel)";
				draw->AddText({min.x + px(10), y}, IM_COL32(255, 120, 110, 255), text.c_str());
				y += ImGui::GetTextLineHeightWithSpacing();
			}
		}
		overlay_caption(draw, min, max, "Left drag orbit   Middle drag pan   Wheel zoom   Right drag + WASD fly   F frame   Drop .gltf .glb .obj .hdr files here");
		ImGui::End();
	};

	const auto technique_panel = [&techniques, &s] {
		ImGui::Begin("Technique");
		if (techniques.empty()) {
			hint("No techniques yet: add one with Lab::add.");
		}
		const int sides = s.compare ? 2 : 1;
		for (int side = 0; side < sides; ++side) {
			if (techniques.empty()) {
				break;
			}
			if (side == 1 && s.technique[1] == s.technique[0]) {
				break; // The same settings already shown.
			}
			const int index = s.technique[side];
			Technique &t = *techniques[std::size_t(index)];
			ImGui::PushID(side);
			const std::string title = (s.compare ? (side == 0 ? "A  " : "B  ") : "") + t.name();
			if (section(title.c_str())) {
				const std::string about = t.description();
				if (!about.empty()) {
					hint(about.c_str());
				}
				auto &why = s.failed[std::size_t(index)];
				if (!why.empty()) {
					text_error(("Stopped: " + why).c_str());
					if (button("Retry", Tone::Go)) {
						why.clear();
						s.gpu.retry();
					}
				}
				fit_items_to_labels(130);
				try {
					t.settings();
				} catch (const std::exception &e) {
					why = std::string("its settings threw: ") + e.what();
				}
				ImGui::PopItemWidth();
			}
			ImGui::PopID();
		}
		if (section("Display")) {
			fit_items_to_labels(130);
			ImGui::SliderFloat("Exposure (EV)", &s.exposure_ev, -10, 10, "%.1f");
			ImGui::Combo("Tonemapper", &s.tonemapper, "ACES (fitted)\0Reinhard\0Clamp\0");
			const char *sizes[]{"The view's size", "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160"};
			if (s.resolution < 0) {
				ImGui::TextDisabled("Resolution %d x %d (from the command line)", s.fixed_width, s.fixed_height);
			} else {
				ImGui::Combo("Resolution", &s.resolution, sizes, 5);
			}
			ImGui::BeginDisabled(s.resolution != 0);
			ImGui::SliderFloat("Render scale", &s.render_scale, 0.25f, 2.0f, "%.2fx");
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Of the view's size: below 1 for speed, above 1 for supersampling");
			if (s.compare) {
				ImGui::SliderFloat("Difference scale", &s.difference_scale, 1, 100, "x%.0f", ImGuiSliderFlags_Logarithmic);
			}
			ImGui::PopItemWidth();
		}
		s.technique_touched = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsAnyItemActive();
		ImGui::End();
	};

	const auto map_row = [&s](Material &m, const char *label, int bit, const std::shared_ptr<Texture> &t) {
		if (!t) {
			return;
		}
		const TextureView v = B::view(*t);
		bool on = (m.maps_enabled & bit) != 0;
		ImGui::PushID(bit);
		if (ImGui::Checkbox("##on", &on)) {
			m.maps_enabled = on ? (m.maps_enabled | bit) : (m.maps_enabled & ~bit);
			s.restart_requested = true;
		}
		ImGui::SetItemTooltip("Use this map; off shows what the factor alone gives");
		ImGui::SameLine();
		const float side = ImGui::GetFrameHeight();
		ImGui::Image(v.id, {side, side});
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::Image(v.id, {px(256), px(256)});
			ImGui::Text("%s  %dx%d", v.name.c_str(), v.width, v.height);
			ImGui::EndTooltip();
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine();
		ImGui::TextDisabled("%dx%d", v.width, v.height);
		ImGui::PopID();
	};

	const auto scene_panel = [&s, &map_row] {
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
			const auto add = [&s](const char *name, std::shared_ptr<typename B::Mesh> mesh) {
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
			const bool selected = s.selected_object >= 0 && s.selected_object < int(s.scene.objects.size());
			ImGui::SameLine();
			if (button("Remove", Tone::Stop, selected, "Remove the selected object; Shift+click removes its whole model")) {
				const std::string source = s.scene.objects[std::size_t(s.selected_object)].source;
				if (ImGui::GetIO().KeyShift && !source.empty()) {
					std::erase_if(s.scene.objects, [&](const Object &o) { return o.source == source; });
				} else {
					s.scene.objects.erase(s.scene.objects.begin() + s.selected_object);
				}
				s.selected_object = -1;
			}
			ImGui::SetNextItemWidth(-px(70));
			ImGui::InputTextWithHint("##model", "A .gltf, .glb or .obj file (or drop one)", s.model_path.data(), s.model_path.size());
			ImGui::SameLine();
			if (button("Load", Tone::Normal, s.model_path[0] != 0)) {
				open_file(s, utf8_path(s.model_path.data()));
			}
		}
		if (s.selected_object >= 0 && s.selected_object < int(s.scene.objects.size())) {
			Object &o = s.scene.objects[std::size_t(s.selected_object)];
			Material &m = o.material;
			if (section(("Selected: " + o.name).c_str()) && begin_properties("##object")) {
				property("Position");
				ImGui::DragFloat3("##position", o.position.data(), 0.02f, 0, 0, "%.2f");
				property("Rotation");
				ImGui::DragFloat3("##rotation", o.rotation_degrees.data(), 0.5f, 0, 0, "%.0f");
				property("Scale");
				ImGui::DragFloat3("##scale", o.scale.data(), 0.01f, 0.0001f, 1000, "%.3f");
				property("Albedo");
				ImGui::ColorEdit3("##albedo", m.albedo.data());
				property("Roughness");
				ImGui::SliderFloat("##roughness", &m.roughness, 0.0f, 1);
				property("Metallic");
				ImGui::SliderFloat("##metallic", &m.metallic, 0, 1);
				property("Emissive");
				ImGui::ColorEdit3("##emissive", m.emissive.data(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
				property("Alpha");
				int mode = int(m.alpha_mode);
				if (ImGui::Combo("##alpha_mode", &mode, "Opaque\0Mask\0Blend\0")) {
					m.alpha_mode = AlphaMode(mode);
				}
				if (m.alpha_mode != AlphaMode::Opaque) {
					property(m.alpha_mode == AlphaMode::Mask ? "Cutoff" : "Opacity");
					if (m.alpha_mode == AlphaMode::Mask) {
						ImGui::SliderFloat("##cutoff", &m.alpha_cutoff, 0, 1);
					} else {
						ImGui::SliderFloat("##opacity", &m.alpha, 0, 1);
					}
				}
				property("Double-sided");
				ImGui::Checkbox("##double_sided", &m.double_sided);
				if (m.normal_map) {
					property("Normal strength");
					ImGui::SliderFloat("##normal_scale", &m.normal_scale, 0, 2);
				}
				if (m.occlusion_map) {
					property("Occlusion strength");
					ImGui::SliderFloat("##occlusion_strength", &m.occlusion_strength, 0, 1);
				}
				end_properties();
				if (m.albedo_map || m.normal_map || m.metal_rough_map || m.occlusion_map || m.emissive_map) {
					ImGui::SeparatorText("Maps");
					map_row(m, "Albedo", map_albedo, m.albedo_map);
					map_row(m, "Normal", map_normal, m.normal_map);
					map_row(m, "Metal / roughness", map_metal_rough, m.metal_rough_map);
					map_row(m, "Occlusion", map_occlusion, m.occlusion_map);
					map_row(m, "Emissive", map_emissive, m.emissive_map);
				} else {
					hint("No maps: drop a PNG or JPEG on the window to use it as this object's albedo map.");
				}
				if (!o.source.empty()) {
					ImGui::TextDisabled("From %s", o.source.c_str());
				}
			}
		}
		if (section("Environment")) {
			auto &env = *s.gpu.environment;
			ImGui::SetNextItemWidth(-px(70));
			ImGui::InputTextWithHint("##environment", "An equirectangular .hdr file (or drop one)", s.environment_path.data(), s.environment_path.size());
			ImGui::SameLine();
			if (button("Load##environment", Tone::Normal, s.environment_path[0] != 0)) {
				load_environment(s, utf8_path(s.environment_path.data()));
			}
			if (env.loaded()) {
				if (begin_properties("##environment_properties")) {
					property("Intensity");
					ImGui::SliderFloat("##environment_intensity", &env.intensity, 0, 4, "%.2f");
					property("Rotation");
					ImGui::SliderFloat("##environment_rotation", &env.rotation_degrees, -180, 180, "%.0f deg");
					property("Show as the sky");
					ImGui::Checkbox("##environment_sky", &env.show_as_sky);
					end_properties();
				}
				ImGui::TextDisabled("%s, built in %.0f ms", fs::path(env.path()).filename().string().c_str(), env.build_ms());
				if (button("Use the gradient sky instead", Tone::Normal)) {
					env.clear();
					s.environment_path[0] = 0;
				}
			} else {
				hint("No environment: the gradient sky below lights the scene.");
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
			ImGui::DragFloat("##far", &s.camera.far_plane, 1, 1, 100000, "%.0f");
			property("Fly speed");
			ImGui::SliderFloat("##speed", &s.camera.fly_speed, 0.1f, 50, "%.1f m/s");
			end_properties();
			char view[160];
			std::snprintf(view, sizeof view, "--camera %.3f,%.3f,%.3f,%.3f,%.1f,%.1f", double(s.camera.target.x), double(s.camera.target.y),
				double(s.camera.target.z), double(s.camera.distance), double(s.camera.yaw * 180 / pi), double(s.camera.pitch * 180 / pi));
			ImGui::TextDisabled("%s", view);
			ImGui::SameLine();
			if (ImGui::SmallButton("Copy")) {
				ImGui::SetClipboardText(view);
			}
			ImGui::SetItemTooltip("This view as a command-line option, for the same view in a scripted capture");
		}
		ImGui::End();
	};

	const auto textures_panel = [&s] {
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
						s.inspect.range_min = 0;
						s.inspect.range_max = 20;
					}
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%d x %d", e.width, e.height);
				ImGui::TableSetColumnIndex(2);
				ImGui::TextDisabled("%s", e.depth ? "depth" : e.channels == 1 ? "R" : e.channels == 2 ? "RG" : e.channels == 3 ? "RGB" : "RGBA");
			}
			ImGui::EndTable();
		}
		const Exposed<typename B::TextureHandle> *chosen = nullptr;
		for (const auto &e : s.exposed) {
			if (e.name == s.inspected) {
				chosen = &e;
			}
		}
		if (chosen) {
			ImGui::Spacing();
			const char *channels[]{"RGB", "R", "G", "B", "A"};
			for (int c = 0; c < 5; ++c) {
				bool on = s.inspect.channel == c;
				if (c) {
					ImGui::SameLine();
				}
				if (chip(channels[c], &on)) {
					s.inspect.channel = c;
				}
			}
			ImGui::SameLine(0, px(16));
			ImGui::Checkbox("Show in the view", &s.inspect_in_view);
			fit_items_to_labels(110);
			ImGui::DragFloatRange2(chosen->depth ? "Range (m)" : "Range", &s.inspect.range_min, &s.inspect.range_max, 0.01f, -1000, 1000, "%.2f");
			ImGui::PopItemWidth();
			const float w = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
			const float h = w * float(std::max(chosen->height, 1)) / float(std::max(chosen->width, 1));
			s.gpu.inspect(*chosen, s.inspect, -1, int(w), int(h));
			ImGui::Image(s.gpu.preview_texture(), {w, h}, {0, 1}, {1, 0});
		} else if (!s.inspected.empty() && s.inspect_in_view) {
			text_warning(("\"" + s.inspected + "\" is not exposed this frame").c_str());
			if (button("Stop showing it")) {
				s.inspect_in_view = false;
			}
		}
		ImGui::End();
	};

	const auto timings_panel = [&s, &techniques] {
		ImGui::Begin("Timings");
		if (s.gpu.timers && ImGui::BeginTable("##timings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, px(80));
			ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, px(80));
			ImGui::TableHeadersRow();
			for (const auto &p : s.gpu.timers->stats()) {
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
			ImGui::Text("%.3f", s.gpu.timers->total_gpu_ms());
			ImGui::TableSetColumnIndex(2);
			ImGui::TextDisabled("%.1f fps", double(s.fps));
			ImGui::EndTable();
		}
		// Counters the shown techniques keep, where the backend's techniques have them.
		if constexpr (requires(Technique &t) { t.statistics(); }) {
			for (int side = 0; side < (s.compare ? 2 : 1) && !techniques.empty(); ++side) {
				if (side == 1 && s.technique[1] == s.technique[0]) {
					break;
				}
				ImGui::PushID(side);
				techniques[std::size_t(s.technique[side])]->statistics();
				ImGui::PopID();
			}
		}
		hint("GPU times come from timer queries a few frames late; hover a pass for its graph.");
		ImGui::End();
	};

	const auto log_panel = [&s] {
		ImGui::Begin("Log");
		if (button("Clear")) {
			s.log.clear();
		}
		ImGui::SameLine();
		if (button("Copy")) {
			std::string all;
			for (const auto &line : s.log) {
				all += line.text + "\n";
			}
			ImGui::SetClipboardText(all.c_str());
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

	const auto menu = [&s, &save_settings] {
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("Capture PNG", "Ctrl+P")) s.capture_requested = true;
				if (ImGui::MenuItem("Capture HDR")) s.capture_hdr_requested = true;
				ImGui::Separator();
				if (ImGui::MenuItem("Save settings now", nullptr, false, s.settings_enabled)) save_settings(true);
				ImGui::Separator();
				if (ImGui::MenuItem("Quit")) quit();
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View")) {
				ImGui::MenuItem("Compare A and B", nullptr, &s.compare);
				ImGui::MenuItem("Pause time", nullptr, &s.paused);
				if (ImGui::MenuItem("Reload shaders", "Ctrl+R")) s.gpu.shaders->reload_all();
				ImGui::Separator();
				if (ImGui::MenuItem("Reset layout")) reset_layout();
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R)) s.gpu.shaders->reload_all();
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P)) s.capture_requested = true;
	};

	// What makes the picture: when it changes, accumulation starts over.
	const auto fingerprint = [&s]() {
		std::size_t h = s.scene.fingerprint();
		const auto mix_bits = [&h](std::uint64_t bits) { h = (h ^ bits) * 1099511628211ull; };
		const auto mix = [&mix_bits](float v) {
			std::uint32_t bits = 0;
			std::memcpy(&bits, &v, sizeof bits);
			mix_bits(bits);
		};
		mix(s.camera.target.x); mix(s.camera.target.y); mix(s.camera.target.z);
		mix(s.camera.distance); mix(s.camera.yaw); mix(s.camera.pitch); mix(s.camera.vertical_fov);
		mix(s.camera.near_plane); mix(s.camera.far_plane);
		const auto &env = *s.gpu.environment;
		mix(env.intensity); mix(env.rotation_degrees); mix(env.show_as_sky ? 1.0f : 0.0f);
		mix_bits(std::hash<std::string>{}(env.loaded() ? env.path() : std::string()));
		mix(float(s.render_width)); mix(float(s.render_height));
		mix(float(s.technique[0])); mix(float(s.technique[1])); mix(s.compare ? 1.0f : 0.0f);
		return h;
	};

	// --check: once 90 frames are drawn, the picture is read back (now, or when the GPU has drawn it) and judged.
	const auto judge = [&s](int w, int h, const std::vector<unsigned char> &pixels) {
		if (!s.save_view.empty()) {
			const bool saved = write_png_bottom_up(utf8_path(s.save_view), w, h, pixels.data());
			s.say(saved ? 1 : 3, (saved ? "Saved the view to " : "Could not write ") + s.save_view);
			if (!s.check) {
				quit(saved ? 0 : 3);
				return;
			}
		}
		int lo = 255, hi = 0;
		for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
			const int y = (pixels[i] * 54 + pixels[i + 1] * 183 + pixels[i + 2] * 19) >> 8;
			lo = std::min(lo, y);
			hi = std::max(hi, y);
		}
		const int spread = w > 0 && h > 0 ? hi - lo : 0;
		if (s.errors_logged > 0) {
			s.say(3, "Check failed: " + std::to_string(s.errors_logged) + " errors were logged");
			quit(3);
		} else if (spread < 3) {
			s.say(3, "Check failed: the picture is one flat colour");
			quit(4);
		} else {
			s.say(1, "Check passed: the picture's brightness spans " + std::to_string(spread) + " of 255 levels, no errors");
			quit(0);
		}
	};

	const int exit_code = rimgui::run(app, [&] {
		if (!s.started || !s.gpu.shaders) {
			ImGui::Begin("Viewport");
			text_error((std::string(B::api) + " could not start; see the Log.").c_str());
			ImGui::End();
			log_panel();
			if (s.check) quit(3);
			return;
		}
		s.gpu.begin_frame();
		s.gpu.shaders->poll();
		const float dt = ImGui::GetIO().DeltaTime;
		s.fps = s.fps <= 0 ? 1 / std::max(dt, 1e-4f) : s.fps * 0.95f + 0.05f / std::max(dt, 1e-4f);
		if (!s.paused) {
			s.time += double(dt);
		}
		const std::size_t now = fingerprint();
		if (now != s.last_fingerprint || s.restart_requested || s.technique_touched) {
			s.accumulation = 0;
			s.last_fingerprint = now;
			s.restart_requested = false;
		} else {
			++s.accumulation;
		}
		s.exposed.clear();
		s.inspect.near_plane = s.camera.near_plane;
		s.inspect.far_plane = s.camera.far_plane;
		menu();
		toolbar();
		viewport(); // First: it draws the frame, which the other panels show.
		technique_panel();
		scene_panel();
		textures_panel();
		timings_panel();
		log_panel();
		s.gpu.end_frame();
		++s.frame_index;
		if (ImGui::GetTime() - s.last_save_check > 2.0) {
			s.last_save_check = ImGui::GetTime();
			save_settings(false);
		}
		if ((s.check || !s.save_view.empty()) && s.frame_index >= 90 && !s.check_asked) {
			s.check_asked = true;
			s.gpu.read_display(0, judge);
		}
	}, argc, argv);
	return exit_code;
}

} // namespace rimgui::rt::lab_core
