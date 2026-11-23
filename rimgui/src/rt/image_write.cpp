// stb_image_write, once for both kits.
#include <rimgui/rt/image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_FALLBACK
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <vector>

namespace rimgui::rt {

bool write_png_bottom_up(const std::filesystem::path &path, int width, int height, const unsigned char *rgba)
{
	if (width <= 0 || height <= 0 || !rgba) {
		return false;
	}
	// From the last row with a negative stride: top first in the file.
	return stbi_write_png(path.string().c_str(), width, height, 4, rgba + std::size_t(width) * 4 * std::size_t(height - 1), -width * 4) != 0;
}

bool write_hdr_bottom_up(const std::filesystem::path &path, int width, int height, const float *rgb)
{
	if (width <= 0 || height <= 0 || !rgb) {
		return false;
	}
	const std::size_t row = std::size_t(width) * 3;
	std::vector<float> flipped(row * std::size_t(height));
	for (int y = 0; y < height; ++y) {
		std::copy_n(rgb + std::size_t(height - 1 - y) * row, row, flipped.data() + std::size_t(y) * row);
	}
	return stbi_write_hdr(path.string().c_str(), width, height, 3, flipped.data()) != 0;
}

} // namespace rimgui::rt
