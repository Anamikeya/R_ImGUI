#pragma once
// RenderLab on Vulkan: the same application as the OpenGL lab (rimgui/rt/lab.h): the same panels, settings, command
// line, captures and checks (both are LabCore), with techniques written in Vulkan through the kit (gpu.h, shader.h,
// scene.h). A technique records into frame.cb, the frame's command buffer, which the shell submits after the frame.
//
//   class MyTechnique : public rimgui::rt::vk::Technique { ... };
//   int main(int argc, char **argv) {
//       rimgui::rt::vk::Lab lab;
//       lab.add(std::make_unique<MyTechnique>());
//       rimgui::rt::vk::LabConfig config;
//       config.shader_folders = {"shaders"};
//       return lab.run(config, argc, argv);
//   }
//
// Besides the OpenGL lab's options it takes rimgui::run's --validation: the validation layers on, and any message from
// them logged as an error, so --check --validation fails on misuse of the API.
#include <rimgui/rt/camera.h>
#include <rimgui/rt/settings.h>
#include <rimgui/rt/vk/accel.h>
#include <rimgui/rt/vk/environment.h>
#include <rimgui/rt/vk/gpu.h>
#include <rimgui/rt/vk/scene.h>
#include <rimgui/rt/vk/shader.h>
#include <rimgui/rt/vk/timer.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rimgui::rt::vk {

class Lab;

// What a technique draws with, this frame.
struct Frame {
	int width = 1, height = 1; // Of the output: the view's size times the render scale, or the fixed resolution.
	const OrbitCamera *camera = nullptr;
	mat4 view, projection, view_projection, inverse_view_projection, inverse_projection; // OpenGL's conventions
	vec3 camera_position;
	double time = 0;     // Seconds since the lab opened; stands still while time is paused.
	float seconds = 0;   // Since the last frame; 0 while time is paused.
	int frame_index = 0; // Frames drawn since the lab opened.
	// Frames in a row with nothing changed; 0 when progressive techniques start over.
	int accumulation = 0;
	Scene *scene = nullptr;
	const Environment *environment = nullptr;
	ShaderLibrary *shaders = nullptr;
	GpuTimers *timers = nullptr;
	// The frame's command buffer: record into it. The shell submits it after the frame.
	VkCommandBuffer cb = VK_NULL_HANDLE;
	// Where the technique's picture goes: linear HDR colour (RGBA16F, also a storage image) and depth (D32), at
	// width x height, cleared (colour black, depth 1). The lab tonemaps it. Not being rendered to when render() starts.
	RenderTarget *output = nullptr;
	// This frame's FrameData (camera, sun, sky, environment, every object), and the scene's ObjectData array.
	VkDeviceAddress frame_data = 0;
	VkDeviceAddress objects = 0;

	// The first 16 bytes of a scene pass's push constants: { frame_data, 0 }.
	DrawConstants constants() const { return {frame_data, 0}; }
	// Begins rendering into the output (keeping what is there), viewport and scissor set to it; end_output() ends it.
	void begin_output() const;
	void end_output() const;
	// Draws the sky (the environment image, or the gradient) where nothing has been drawn: depth LESS_OR_EQUAL against
	// depth 1. Call it between begin_output() and end_output().
	void draw_sky() const;
	// Lists a texture in the Textures panel for this frame, to look at it by channel.
	void expose(const std::string &name, Image &image, int channels = 4) const;
	void expose(const std::string &name, const RenderTarget &target, std::size_t color_index) const;
	void expose_depth(const std::string &name, const RenderTarget &target) const;
	// Times a pass, on the GPU and the CPU, for the Timings panel: { auto t = frame.scope("Shadows"); ... }
	GpuTimers::Scope scope(const std::string &name) const { return timers->scope(cb, prefix + name); }
	// Asks for accumulation to start over next frame, for a change the lab cannot see.
	void restart_accumulation() const;
	// Builds or brings up to date the scene's acceleration structures (u_scene, binding 4), for ray tracing pipelines
	// and ray queries. False on a GPU without hardware ray tracing: the technique should say so and draw no rays.
	bool prepare_ray_tracing() const;
	// The structures prepare_ray_tracing() keeps (their counts, for statistics); null without ray tracing.
	const AccelerationStructures *acceleration_structures() const;

	Lab *lab = nullptr;
	std::string prefix;
};

class Technique {
public:
	virtual ~Technique() = default;
	virtual std::string name() const = 0;
	virtual std::string description() const { return {}; }
	virtual void settings() {}
	virtual void serialize(Settings &) {}
	// Records the frame into frame.cb, drawing into frame.output. An exception stops the technique (with the message
	// in the Log) until Retry is pressed; the lab goes on.
	virtual void render(Frame &frame) = 0;
	// Shown in the Timings panel under the passes: counters a technique keeps (culled meshlets, rays traced).
	virtual void statistics() {}
};

struct LabConfig {
	std::string title = "RenderLab Vulkan";
	std::vector<std::filesystem::path> shader_folders;
	std::function<void(Scene &)> make_scene;
	std::filesystem::path capture_folder;
	std::filesystem::path settings_file;
};

class Lab {
public:
	Lab();
	~Lab();
	void add(std::unique_ptr<Technique> technique);
	// Runs the lab; the process's exit code. The options are the OpenGL lab's (rimgui/rt/lab.h), plus --validation.
	int run(const LabConfig &config, int argc = 0, char **argv = nullptr);

	struct State;

private:
	friend struct Frame;
	std::vector<std::unique_ptr<Technique>> techniques_;
	std::unique_ptr<State> state_;
};

} // namespace rimgui::rt::vk
