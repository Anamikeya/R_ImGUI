#pragma once
// RenderLab: an application for trying rendering techniques. You write techniques; the lab gives each one a scene, a
// camera, a frame to draw into, timers and hot-reloaded shaders, and shows the result:
//
//   Viewport   the picture, tonemapped; the camera moves with the mouse; A/B: two techniques split by a slider
//   Technique  the chosen technique's own settings (Technique::settings)
//   Scene      objects, materials, the sun and sky, adding test shapes and OBJ models
//   Textures   every texture a technique exposed this frame (G-buffers, AO, shadow maps), shown by channel and range,
//              in the panel or over the whole view
//   Timings    GPU and CPU time of each pass, and a graph
//   Log        shader errors by file and line, OpenGL debug messages
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
#include <rimgui/rt/mesh.h>
#include <rimgui/rt/shader.h>
#include <rimgui/rt/target.h>
#include <rimgui/rt/timer.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rimgui::rt {

struct Material {
	vec3 albedo{0.8f, 0.8f, 0.8f};
	float roughness = 0.5f, metallic = 0;
	vec3 emissive{0, 0, 0};
};

struct Object {
	std::string name;
	std::shared_ptr<Mesh> mesh;
	vec3 position{0, 0, 0}, rotation_degrees{0, 0, 0}, scale{1, 1, 1};
	Material material;
	bool visible = true;
	mat4 model() const;
};

struct Sun {
	float azimuth_degrees = 35, elevation_degrees = 45;
	vec3 color{1, 0.96f, 0.9f};
	float intensity = 3;
	vec3 sky{0.25f, 0.45f, 0.85f}, horizon{0.62f, 0.68f, 0.78f}, ground{0.22f, 0.19f, 0.16f};
	float ambient = 0.25f; // How strongly the sky lights what the sun does not.
	vec3 direction() const; // Towards the sun, unit length.
};

struct Scene {
	std::vector<Object> objects;
	Sun sun;
	// The visible objects with each one's u_model, u_normal_matrix, u_albedo, u_roughness, u_metallic and u_emissive
	// set on `program`, which must be in use.
	void draw(Program &program) const;
	// A sphere round every visible object.
	void bounds(vec3 &centre, float &radius) const;
	// Sets the sun and sky uniforms: u_sun_direction, u_sun_color (colour times intensity), u_sky, u_horizon, u_ground,
	// u_ambient.
	void set_lighting(Program &program) const;
};

class Lab;

// What a technique draws with, this frame.
struct Frame {
	int width = 1, height = 1;
	const OrbitCamera *camera = nullptr;
	mat4 view, projection, view_projection, inverse_view_projection, inverse_projection;
	vec3 camera_position;
	double time = 0;      // Seconds since the lab opened.
	float seconds = 0;    // Since the last frame.
	int frame_index = 0;
	Scene *scene = nullptr;
	ShaderLibrary *shaders = nullptr;
	GpuTimers *timers = nullptr;
	// Where the technique's picture goes: linear HDR colour (RGBA16F) and depth, sized to the view. The lab
	// tonemaps it. Bound and cleared before render() is called.
	RenderTarget *output = nullptr;

	// Lists a texture in the Textures panel for this frame, to look at it by channel.
	void expose(const std::string &name, GLuint texture, int texture_width, int texture_height, bool is_depth = false) const;
	void expose(const std::string &name, const RenderTarget &target, std::size_t color_index) const;
	void expose_depth(const std::string &name, const RenderTarget &target) const;
	// Times a pass, on the GPU and the CPU, for the Timings panel: { auto t = frame.scope("Shadows"); ... }
	GpuTimers::Scope scope(const std::string &name) const { return timers->scope(prefix + name); }
	// Draws the sky (from Scene::sun) where nothing has been drawn: depth test on, depth 1.
	void draw_sky() const;
	// Sets u_view, u_projection, u_view_projection, u_inverse_view_projection, u_inverse_projection, u_camera_position,
	// u_time and u_resolution on `program`, which must be in use.
	void set_camera(Program &program) const;

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
	// Draws the frame into frame.output.
	virtual void render(Frame &frame) = 0;
};

struct LabConfig {
	std::string title = "RenderLab";
	// Folders with the techniques' shaders. Relative ones are looked for beside the program and in the folder it was
	// built from, so editing the source shaders reloads them.
	std::vector<std::filesystem::path> shader_folders;
	// The scene at the start; empty gives a floor, a sphere, a cube and a torus.
	std::function<void(Scene &)> make_scene;
	// Where captures are saved; empty: "captures" beside the program.
	std::filesystem::path capture_folder;
};

class Lab {
public:
	Lab();
	~Lab();
	void add(std::unique_ptr<Technique> technique);
	// Runs the lab; the process's exit code. Besides rimgui::run's options it reads:
	//   --technique <name or number>   the technique shown at the start
	//   --compare <name or number>     A/B against this one
	//   --show <texture name>          that exposed texture over the view
	int run(const LabConfig &config, int argc = 0, char **argv = nullptr);

	// What the lab does each frame, for those who build their own application round it.
	struct State;
	State &state() { return *state_; }

private:
	friend struct Frame;
	std::vector<std::unique_ptr<Technique>> techniques_;
	std::unique_ptr<State> state_;
};

} // namespace rimgui::rt
