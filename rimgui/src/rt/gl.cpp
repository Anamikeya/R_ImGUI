#define GLAD_GL_IMPLEMENTATION
#include <rimgui/rt/gl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace rimgui::rt {

namespace gl43 {
DispatchCompute dispatch_compute = nullptr;
MemoryBarrier memory_barrier = nullptr;
BindImageTexture bind_image_texture = nullptr;
TexStorage2D tex_storage_2d = nullptr;
} // namespace gl43

namespace {
bool loaded = false;
bool gl43_ready = false;
} // namespace

bool load_gl()
{
	if (loaded) {
		return true;
	}
	if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
		return false;
	}
	loaded = true;
	GLint major = 0, minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	if (major > 4 || (major == 4 && minor >= 3)) {
		gl43::dispatch_compute = reinterpret_cast<gl43::DispatchCompute>(glfwGetProcAddress("glDispatchCompute"));
		gl43::memory_barrier = reinterpret_cast<gl43::MemoryBarrier>(glfwGetProcAddress("glMemoryBarrier"));
		gl43::bind_image_texture = reinterpret_cast<gl43::BindImageTexture>(glfwGetProcAddress("glBindImageTexture"));
		gl43::tex_storage_2d = reinterpret_cast<gl43::TexStorage2D>(glfwGetProcAddress("glTexStorage2D"));
		gl43_ready = gl43::dispatch_compute && gl43::memory_barrier && gl43::bind_image_texture && gl43::tex_storage_2d;
	}
	return true;
}

bool has_gl43()
{
	return gl43_ready;
}

std::string gl_description()
{
	if (!loaded) {
		return "OpenGL not loaded";
	}
	const auto text = [](GLenum name) {
		const auto *s = reinterpret_cast<const char *>(glGetString(name));
		return std::string(s ? s : "?");
	};
	return text(GL_VERSION) + "  |  " + text(GL_RENDERER);
}

} // namespace rimgui::rt
