#include <rimgui/rt/texture.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

namespace rimgui::rt {

namespace fs = std::filesystem;

Texture::~Texture()
{
	if (id_) {
		glDeleteTextures(1, &id_);
	}
}

namespace {

// Anisotropic filtering: core in OpenGL 4.6, and before it an extension (the core name is listed too on 4.6 drivers).
bool glfw_has_anisotropy()
{
	return glfwExtensionSupported("GL_EXT_texture_filter_anisotropic") || glfwExtensionSupported("GL_ARB_texture_filter_anisotropic");
}

void sampling(bool mipmaps)
{
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	// Anisotropic filtering, up to 8x where the driver has it.
	static const GLfloat most = [] {
		GLfloat value = 0;
		if (glfw_has_anisotropy()) {
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &value);
		}
		return value;
	}();
	if (most > 1) {
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(most, 8.0f));
	}
}

std::vector<unsigned char> read_file(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return in ? std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {}) : std::vector<unsigned char>{};
}

std::map<std::string, std::weak_ptr<Texture>> &cache()
{
	static std::map<std::string, std::weak_ptr<Texture>> c;
	return c;
}

} // namespace

std::shared_ptr<Texture> Texture::from_pixels(int width, int height, int channels, const unsigned char *pixels, bool srgb, std::string name)
{
	if (width <= 0 || height <= 0 || channels < 1 || channels > 4 || !pixels) {
		return nullptr;
	}
	auto t = std::make_shared<Texture>();
	t->width_ = width;
	t->height_ = height;
	t->name_ = std::move(name);
	glGenTextures(1, &t->id_);
	glBindTexture(GL_TEXTURE_2D, t->id_);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	// One and two channels stay data; sRGB applies to colour (three or four channels).
	static const GLenum formats[]{GL_RED, GL_RG, GL_RGB, GL_RGBA};
	const GLenum format = formats[channels - 1];
	GLenum internal = channels == 1 ? GL_R8 : channels == 2 ? GL_RG8 : channels == 3 ? GL_RGB8 : GL_RGBA8;
	if (srgb && channels == 3) internal = GL_SRGB8;
	if (srgb && channels == 4) internal = GL_SRGB8_ALPHA8;
	// Uploaded top row first, so texture coordinate v = 0 is the image's top, as glTF has it. OBJ files count v from
	// the bottom; load_obj turns theirs round.
	glTexImage2D(GL_TEXTURE_2D, 0, GLint(internal), width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
	if (channels == 1) {
		const GLint grey[]{GL_RED, GL_RED, GL_RED, GL_ONE};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, grey);
	}
	glGenerateMipmap(GL_TEXTURE_2D);
	sampling(true);
	glBindTexture(GL_TEXTURE_2D, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	return t;
}

std::shared_ptr<Texture> Texture::from_floats(int width, int height, int channels, const float *pixels, std::string name)
{
	if (width <= 0 || height <= 0 || (channels != 3 && channels != 4) || !pixels) {
		return nullptr;
	}
	auto t = std::make_shared<Texture>();
	t->width_ = width;
	t->height_ = height;
	t->hdr_ = true;
	t->name_ = std::move(name);
	glGenTextures(1, &t->id_);
	glBindTexture(GL_TEXTURE_2D, t->id_);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	// 32-bit float: a photographed sun is far brighter than half floats hold (65504), and would become infinite.
	glTexImage2D(GL_TEXTURE_2D, 0, channels == 3 ? GL_RGB32F : GL_RGBA32F, width, height, 0, channels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT, pixels);
	glGenerateMipmap(GL_TEXTURE_2D);
	sampling(true);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Equirectangular images do not wrap at the poles.
	glBindTexture(GL_TEXTURE_2D, 0);
	return t;
}

std::shared_ptr<Texture> Texture::from_image(const Image &image, bool srgb)
{
	if (image.hdr) {
		return from_floats(image.width, image.height, image.channels, image.floats.data(), image.name);
	}
	return from_pixels(image.width, image.height, image.channels, image.bytes.data(), srgb, image.name);
}

std::shared_ptr<Texture> load_texture_from_memory(const unsigned char *data, std::size_t size, bool srgb, std::string name, std::string *error)
{
	const auto image = decode_image_from_memory(data, size, std::move(name), error);
	if (!image) {
		return nullptr;
	}
	auto t = Texture::from_image(*image, srgb);
	if (!t && error) *error = image->name + ": unusable image";
	return t;
}

std::shared_ptr<Texture> load_texture(const fs::path &path, bool srgb, std::string *error)
{
	std::error_code ignored;
	const std::string key = fs::weakly_canonical(path, ignored).string() + (srgb ? "|srgb" : "|linear");
	if (auto found = cache()[key].lock()) {
		return found;
	}
	const auto bytes = read_file(path);
	if (bytes.empty()) {
		if (error) *error = path.string() + ": cannot be read";
		return nullptr;
	}
	auto t = load_texture_from_memory(bytes.data(), bytes.size(), srgb, path.string(), error);
	if (t) {
		cache()[key] = t;
	}
	return t;
}

} // namespace rimgui::rt
