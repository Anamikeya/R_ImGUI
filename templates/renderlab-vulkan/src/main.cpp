// RenderLab on Vulkan: techniques written in Vulkan through the research kit (rimgui/rt/vk/lab.h), in the same
// application as the OpenGL RenderLab.
//
//   Sky and environment   the background alone: the gradient sky with its sun, or an HDR environment whose
//                         image-based lighting maps the kit built on the GPU with compute shaders
//
// Add your own: derive from rimgui::rt::vk::Technique, record into frame.cb, write its shaders in shaders/ (include
// "rimgui_vk_scene.glsl", "rimgui_vk_lighting.glsl", "rimgui_vk_material.glsl"), and lab.add() it in main().
#include <rimgui/rt/vk/lab.h>

#include <imgui.h>

#include <memory>

using namespace rimgui::rt::vk;

namespace {

class SkyOnly : public Technique {
public:
	std::string name() const override { return "Sky and environment"; }
	std::string description() const override
	{
		return "The background alone: the gradient sky and its sun, or the HDR environment (Scene panel), as every "
			   "technique's sky pass draws it.";
	}
	void render(Frame &frame) override
	{
		auto timed = frame.scope("Sky");
		frame.begin_output();
		frame.draw_sky();
		frame.end_output();
	}
};

} // namespace

int main(int argc, char **argv)
{
	Lab lab;
	lab.add(std::make_unique<SkyOnly>());
	LabConfig config;
	config.title = "RenderLab Vulkan";
	config.shader_folders = {RIMGUI_SHADERS, "shaders"};
	return lab.run(config, argc, argv);
}
