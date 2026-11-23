#include <rimgui/rt/environment.h>

#include <rimgui/rt/mesh.h>
#include <rimgui/rt/texture.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace rimgui::rt {

namespace {

constexpr int environment_size = 512, irradiance_size = 32, prefiltered_size = 256, prefiltered_level_count = 6, lut_size = 256;

GLuint make_cubemap(int size, bool mipmaps)
{
	GLuint cube = 0;
	glGenTextures(1, &cube);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
	for (int face = 0; face < 6; ++face) {
		// 32-bit float, as the image: half floats overflow to infinity on a bright sun, and the maps made from it to NaN.
		glTexImage2D(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGBA32F, size, size, 0, GL_RGBA, GL_FLOAT, nullptr);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	for (const GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) {
		glTexParameteri(GL_TEXTURE_CUBE_MAP, wrap, GL_CLAMP_TO_EDGE);
	}
	if (mipmaps) {
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
	}
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	return cube;
}

// Draws `program` once for each face of `cube` at `level`, telling it the face (u_face) and, for the prefilter, the
// roughness of the level.
void render_faces(Program &program, GLuint framebuffer, GLuint cube, int level, int size)
{
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glViewport(0, 0, size, size);
	for (int face = 0; face < 6; ++face) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), cube, level);
		program.set("u_face", face);
		draw_fullscreen_triangle();
	}
}

void state_for_passes()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

} // namespace

Environment::~Environment()
{
	clear();
	if (brdf_lut_) {
		glDeleteTextures(1, &brdf_lut_);
	}
}

void Environment::clear()
{
	for (GLuint *t : {&environment_, &irradiance_, &prefiltered_}) {
		if (*t) {
			glDeleteTextures(1, t);
			*t = 0;
		}
	}
	path_.clear();
	prefiltered_levels_ = 0;
}

bool Environment::make_brdf_lut(ShaderLibrary &shaders, std::string *error)
{
	if (brdf_lut_) {
		return true;
	}
	Program &p = shaders.graphics("rimgui.brdf_lut", "rimgui_fullscreen.vert", "rimgui_brdf_lut.frag");
	if (!p.ready()) {
		if (error) *error = "the BRDF table shader did not build: " + p.error();
		return false;
	}
	glGenTextures(1, &brdf_lut_);
	glBindTexture(GL_TEXTURE_2D, brdf_lut_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, lut_size, lut_size, 0, GL_RG, GL_HALF_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdf_lut_, 0);
	glViewport(0, 0, lut_size, lut_size);
	state_for_passes();
	p.use();
	draw_fullscreen_triangle();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

bool Environment::load(const std::filesystem::path &path, ShaderLibrary &shaders, std::string *error)
{
	const auto start = std::chrono::steady_clock::now();
	if (!make_brdf_lut(shaders, error)) {
		return false;
	}
	std::string problem;
	const FloatImage image = read_float_image(path, &problem);
	if (!image.pixels) {
		if (error) *error = problem;
		return false;
	}
	GLint most = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &most);
	if (image.width > most || image.height > most) {
		if (error) *error = path.string() + ": " + std::to_string(image.width) + "x" + std::to_string(image.height) +
							" is larger than this GPU's textures (" + std::to_string(most) + ")";
		return false;
	}
	Program &to_cube = shaders.graphics("rimgui.equirect_to_cube", "rimgui_fullscreen.vert", "rimgui_equirect_to_cube.frag");
	Program &irradiance = shaders.graphics("rimgui.irradiance", "rimgui_fullscreen.vert", "rimgui_irradiance.frag");
	Program &prefilter = shaders.graphics("rimgui.prefilter", "rimgui_fullscreen.vert", "rimgui_prefilter.frag");
	for (Program *p : {&to_cube, &irradiance, &prefilter}) {
		if (!p->ready()) {
			if (error) *error = p->name() + " did not build: " + p->error();
			return false;
		}
	}
	const auto equirect = Texture::from_floats(image.width, image.height, 3, image.pixels.get(), path.string());
	if (!equirect) {
		if (error) *error = path.string() + ": could not be put on the GPU";
		return false;
	}

	GLint previous_framebuffer = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
	state_for_passes();
	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);

	const GLuint cube = make_cubemap(environment_size, true);
	to_cube.use();
	to_cube.texture("u_equirect", 0, equirect->id());
	render_faces(to_cube, fbo, cube, 0, environment_size);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	const GLuint irr = make_cubemap(irradiance_size, false);
	irradiance.use();
	irradiance.texture("u_environment", 0, cube, GL_TEXTURE_CUBE_MAP);
	irradiance.set("u_source_size", float(environment_size));
	render_faces(irradiance, fbo, irr, 0, irradiance_size);

	const GLuint pre = make_cubemap(prefiltered_size, true);
	prefilter.use();
	prefilter.texture("u_environment", 0, cube, GL_TEXTURE_CUBE_MAP);
	prefilter.set("u_source_size", float(environment_size));
	for (int level = 0; level < prefiltered_level_count; ++level) {
		prefilter.set("u_roughness", float(level) / float(prefiltered_level_count - 1));
		render_faces(prefilter, fbo, pre, level, std::max(prefiltered_size >> level, 1));
	}
	glBindTexture(GL_TEXTURE_CUBE_MAP, pre);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, prefiltered_level_count - 1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	glDeleteFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous_framebuffer));
	glUseProgram(0);
	glFinish(); // So the time taken is the GPU's too.

	clear();
	environment_ = cube;
	irradiance_ = irr;
	prefiltered_ = pre;
	prefiltered_levels_ = prefiltered_level_count;
	path_ = path.string();
	build_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return true;
}

void Environment::bind(Program &program) const
{
	const int unit = environment_texture_unit;
	program.set("u_has_environment", loaded() ? 1 : 0);
	if (brdf_lut_ && program.location("u_brdf_lut") >= 0) {
		program.texture("u_brdf_lut", unit + 3, brdf_lut_);
	}
	if (!loaded()) {
		return;
	}
	if (program.location("u_environment") >= 0) program.texture("u_environment", unit, environment_, GL_TEXTURE_CUBE_MAP);
	if (program.location("u_irradiance") >= 0) program.texture("u_irradiance", unit + 1, irradiance_, GL_TEXTURE_CUBE_MAP);
	if (program.location("u_prefiltered") >= 0) program.texture("u_prefiltered", unit + 2, prefiltered_, GL_TEXTURE_CUBE_MAP);
	program.set("u_environment_intensity", intensity);
	program.set("u_environment_rotation", rotation_degrees * pi / 180);
	program.set("u_prefiltered_levels", float(prefiltered_levels_));
	program.set("u_environment_as_sky", show_as_sky ? 1 : 0);
	glActiveTexture(GL_TEXTURE0);
}

} // namespace rimgui::rt
