#pragma once
// Images on the GPU: PNG, JPEG, TGA, BMP and Radiance HDR files (stb_image), or pixels from memory, with mipmaps.
// Colour images (albedo, emissive) are read as sRGB, so shaders see linear values; data images (normals, roughness)
// are read as they are.
#include <rimgui/rt/gl.h>
#include <rimgui/rt/image.h>

#include <filesystem>
#include <memory>
#include <string>

namespace rimgui::rt {

class Texture {
public:
	Texture() = default;
	~Texture();
	Texture(const Texture &) = delete;
	Texture &operator=(const Texture &) = delete;

	GLuint id() const { return id_; }
	int width() const { return width_; }
	int height() const { return height_; }
	bool hdr() const { return hdr_; }
	// Where it came from: a file's path, or a name given to pixels from memory.
	const std::string &name() const { return name_; }

	// 8-bit pixels, `channels` 1 to 4 a pixel, rows top to bottom.
	static std::shared_ptr<Texture> from_pixels(int width, int height, int channels, const unsigned char *pixels, bool srgb,
		std::string name = {});
	// A decoded image (rimgui/rt/image.h); `srgb` for colour images that are not HDR.
	static std::shared_ptr<Texture> from_image(const Image &image, bool srgb);
	// 32-bit float pixels, 3 or 4 channels a pixel, rows top to bottom.
	static std::shared_ptr<Texture> from_floats(int width, int height, int channels, const float *pixels, std::string name = {});

private:
	GLuint id_ = 0;
	int width_ = 0, height_ = 0;
	bool hdr_ = false;
	std::string name_;
};

// An image file. The same file and colour space loaded again gives the same texture while it is in use. Null, with
// `error` set, when it cannot be read.
std::shared_ptr<Texture> load_texture(const std::filesystem::path &path, bool srgb, std::string *error = nullptr);
// An encoded image in memory (as glTF binaries hold them).
std::shared_ptr<Texture> load_texture_from_memory(const unsigned char *data, std::size_t size, bool srgb, std::string name,
	std::string *error = nullptr);

} // namespace rimgui::rt
