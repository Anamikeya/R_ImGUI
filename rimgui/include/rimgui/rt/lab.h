#pragma once
// RenderLab: an application for researching realtime rendering. You write techniques; the lab gives each one a
// scene, a camera, a frame to draw into, timers, hot-reloaded shaders and image-based lighting, and shows the result.
//
//   Viewport   the picture, tonemapped, at the panel's size or a fixed resolution; the camera moves with the mouse
//   Compare    A/B: two techniques split by a line you drag, or the difference between them
//   Technique  the chosen techniques' settings (Technique::settings), kept between runs (Technique::serialize)
//   Scene      objects and their materials and maps, glTF and OBJ models, the HDR environment, the sun and sky, the
//              camera; drop .gltf, .glb, .obj and .hdr files on the window
//   Textures   every texture a technique exposed this frame (G-buffers, AO, shadow maps), by channel and range
//   Timings    GPU and CPU time of each pass
//   Log        shader errors by file and line, OpenGL errors and debug messages, a technique that threw
//
//   class MyTechnique : public rimgui::rt::Technique { ... };
//   int main(int argc, char **argv) {
//       rimgui::rt::Lab lab;
//       lab.add(std::make_unique<MyTechnique>());
//       rimgui::rt::LabConfig config;
//       config.shader_folders = {"shaders"};
//       return lab.run(config, argc, argv);
//   }
#include <rimgui/rt/camera.h>
#include <rimgui/rt/environment.h>
#include <rimgui/rt/mesh.h>
#include <rimgui/rt/model.h>
#include <rimgui/rt/scene.h>
#include <rimgui/rt/settings.h>
#include <rimgui/rt/shader.h>
#include <rimgui/rt/target.h>
#include <rimgui/rt/texture.h>
#include <rimgui/rt/timer.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rimgui::rt {

class Lab;

// What a technique draws with, this frame.
struct Frame {
	int width = 1, height = 1; // Of the output: the view's size times the render scale, or the fixed resolution.
	const OrbitCamera *camera = nullptr;
	mat4 view, projection, view_projection, inverse_view_projection, inverse_projection;
	vec3 camera_position;
	double time = 0;      // Seconds since the lab opened; stands still while time is paused.
	float seconds = 0;    // Since the last frame; 0 while time is paused.
	int frame_index = 0;  // Frames drawn since the lab opened.
	// Frames in a row with nothing changed: the camera, the scene, the environment, the output's size, this technique's
	// settings. 0 on the first frame after a change: where progressive techniques (path tracing, temporal
	// accumulation) start over.
	int accumulation = 0;
	Scene *scene = nullptr;
	const Environment *environment = nullptr;
	ShaderLibrary *shaders = nullptr;
	GpuTimers *timers = nullptr;
	// Where the technique's picture goes: linear HDR colour (RGBA16F) and depth, at width x height. The lab tonemaps
	// it. Bound and cleared before render() is called, with depth testing and back-face culling on.
	RenderTarget *output = nullptr;

	// Lists a texture in the Textures panel for this frame, to look at it by channel.
	void expose(const std::string &name, GLuint texture, int texture_width, int texture_height, bool is_depth = false) const;
	void expose(const std::string &name, const RenderTarget &target, std::size_t color_index) const;
	void expose_depth(const std::string &name, const RenderTarget &target) const;
	// Times a pass, on the GPU and the CPU, for the Timings panel: { auto t = frame.scope("Shadows"); ... }
	GpuTimers::Scope scope(const std::string &name) const { return timers->scope(prefix + name); }
	// Draws the sky (the environment image, or the gradient) where nothing has been drawn: depth test on, depth 1.
	void draw_sky() const;
	// Sets u_view, u_projection, u_view_projection, u_inverse_view_projection, u_inverse_projection, u_camera_position,
	// u_time, u_resolution, u_frame_index and u_accumulation on `program`, which must be in use.
	void set_camera(Program &program) const;
	// Sets the sun, the sky and the environment's maps (rimgui_lighting.glsl) on `program`, which must be in use.
	void set_lighting(Program &program) const;
	// Asks for accumulation to start over next frame, for a change the lab cannot see.
	void restart_accumulation() const;

	Lab *lab = nullptr;
	std::string prefix;
};

class Technique {
public:
	virtual ~Technique() = default;
	virtual std::string name() const = 0;
	// One line under the name in the Technique panel: what it is, or the paper it follows.
	virtual std::string description() const { return {}; }
	// Its settings, drawn into the Technique panel.
	virtual void settings() {}
	// Its settings to and from the lab's settings file: call s.value(key, member) for each.
	virtual void serialize(Settings &) {}
	// Draws the frame into frame.output. An exception stops the technique (with the message in the Log) until Retry
	// is pressed; the lab goes on.
	virtual void render(Frame &frame) = 0;
};

struct LabConfig {
	std::string title = "RenderLab";
	// Folders with the techniques' shaders. Relative ones are looked for beside the program and in the folder it was
	// built from, so editing the source shaders reloads them.
	std::vector<std::filesystem::path> shader_folders;
	// The scene at the start; empty gives a floor and test shapes, including a row of spheres from smooth to rough.
	std::function<void(Scene &)> make_scene;
	// Where captures are saved; empty: "captures" beside the program.
	std::filesystem::path capture_folder;
	// Where the lab keeps its state between runs; empty: "<title>.settings" beside the program.
	std::filesystem::path settings_file;
};

class Lab {
public:
	Lab();
	~Lab();
	void add(std::unique_ptr<Technique> technique);
	// Runs the lab; the process's exit code. Besides rimgui::run's options (--screenshot, --hidden) it reads:
	//   --technique <name or number>   the technique shown at the start
	//   --compare <name or number>     A/B against this one
	//   --difference                   ...showing |A - B| instead of a split
	//   --show <texture name>          that exposed texture over the view
	//   --environment <file.hdr>       image-based lighting from this image
	//   --model <file>                 a .gltf, .glb or .obj model added to the scene (may be given more than once)
	//   --resolution <width>x<height>  draw at this size whatever the view's size
	//   --camera <x,y,z,distance,yaw,pitch>  the camera's target, distance and angles (degrees), as the Camera
	//                                  section shows them: for the same view in every capture
	//   --scene empty                  only the floor, instead of the test shapes
	//   --no-settings                  start from the defaults and do not save (screenshots and checks do this too)
	//   --check                        draw 90 frames, then quit: exit code 0 when the picture is not blank and
	//                                  nothing was logged as an error; 3 errors were logged; 4 the picture is blank
	//   --log <file>                   every Log line also written to this file
	int run(const LabConfig &config, int argc = 0, char **argv = nullptr);

	struct State;

private:
	friend struct Frame;
	std::vector<std::unique_ptr<Technique>> techniques_;
	std::unique_ptr<State> state_;
};

} // namespace rimgui::rt
