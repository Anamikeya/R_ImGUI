#pragma once
// Images decoded on the CPU, for any graphics API to upload: PNG, JPEG, TGA, BMP and Radiance HDR (stb_image).
// Part of rimgui_rt_core, which has no graphics API in it.
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rimgui::rt {

struct Image {
	std::string name;     // A file's path, or what the image is called inside a model file.
	int width = 0, height = 0;
	int channels = 0;     // 1 to 4 for 8-bit images; 3 or 4 for HDR ones.
	bool hdr = false;
	std::vector<unsigned char> bytes; // 8-bit pixels, rows top first (when !hdr).
	std::vector<float> floats;        // Float pixels, rows top first (when hdr).
};

// Null, with `error` set, when the file cannot be read or decoded. A grey HDR image comes back as RGB.
std::shared_ptr<Image> decode_image(const std::filesystem::path &path, std::string *error = nullptr);
std::shared_ptr<Image> decode_image_from_memory(const unsigned char *data, std::size_t size, std::string name, std::string *error = nullptr);

// Decoded Radiance HDR (or any image as floats), rows top to bottom, RGB.
struct FloatImage {
	int width = 0, height = 0;
	std::unique_ptr<float[]> pixels;
};
FloatImage read_float_image(const std::filesystem::path &path, std::string *error = nullptr);

// Writes 8-bit RGBA pixels as a PNG, or RGB floats as a Radiance .hdr file. Rows are given bottom first (as both kits
// read pictures back) and written top first. False when the file cannot be written.
bool write_png_bottom_up(const std::filesystem::path &path, int width, int height, const unsigned char *rgba);
bool write_hdr_bottom_up(const std::filesystem::path &path, int width, int height, const float *rgb);

} // namespace rimgui::rt
