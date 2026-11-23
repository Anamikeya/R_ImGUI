#include <rimgui/rt/image.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <climits>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace rimgui::rt {

namespace fs = std::filesystem;

namespace {

std::vector<unsigned char> read_file(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return in ? std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {}) : std::vector<unsigned char>{};
}

} // namespace

std::shared_ptr<Image> decode_image_from_memory(const unsigned char *data, std::size_t size, std::string name, std::string *error)
{
	if (!data || size == 0 || size > std::size_t(INT32_MAX)) {
		if (error) *error = name + ": no image data";
		return nullptr;
	}
	stbi_set_flip_vertically_on_load(0);
	auto image = std::make_shared<Image>();
	image->name = std::move(name);
	int w = 0, h = 0, n = 0;
	if (stbi_is_hdr_from_memory(data, int(size))) {
		float *pixels = stbi_loadf_from_memory(data, int(size), &w, &h, &n, 0);
		if (!pixels) {
			if (error) *error = image->name + ": " + stbi_failure_reason();
			return nullptr;
		}
		image->width = w;
		image->height = h;
		image->hdr = true;
		image->channels = n >= 4 ? 4 : 3;
		const std::size_t count = std::size_t(w) * std::size_t(h);
		image->floats.resize(count * std::size_t(image->channels));
		for (std::size_t i = 0; i < count; ++i) {
			for (int c = 0; c < image->channels; ++c) {
				// Grey or grey-alpha HDR: the grey repeated into RGB.
				const int from = n >= 3 ? c : 0;
				image->floats[i * std::size_t(image->channels) + std::size_t(c)] = pixels[i * std::size_t(n) + std::size_t(from)];
			}
		}
		stbi_image_free(pixels);
		return image;
	}
	unsigned char *pixels = stbi_load_from_memory(data, int(size), &w, &h, &n, 0);
	if (!pixels) {
		if (error) *error = image->name + ": " + stbi_failure_reason();
		return nullptr;
	}
	image->width = w;
	image->height = h;
	image->channels = n;
	image->bytes.assign(pixels, pixels + std::size_t(w) * std::size_t(h) * std::size_t(n));
	stbi_image_free(pixels);
	return image;
}

std::shared_ptr<Image> decode_image(const fs::path &path, std::string *error)
{
	const auto bytes = read_file(path);
	if (bytes.empty()) {
		if (error) *error = path.string() + ": cannot be read";
		return nullptr;
	}
	return decode_image_from_memory(bytes.data(), bytes.size(), path.string(), error);
}

FloatImage read_float_image(const fs::path &path, std::string *error)
{
	FloatImage image;
	const auto bytes = read_file(path);
	if (bytes.empty() || bytes.size() > std::size_t(INT32_MAX)) {
		if (error) *error = path.string() + ": cannot be read";
		return image;
	}
	int n = 0;
	float *pixels = stbi_loadf_from_memory(bytes.data(), int(bytes.size()), &image.width, &image.height, &n, 3);
	if (!pixels) {
		if (error) *error = path.string() + ": " + stbi_failure_reason();
		image.width = image.height = 0;
		return image;
	}
	const std::size_t count = std::size_t(image.width) * std::size_t(image.height) * 3;
	image.pixels = std::make_unique<float[]>(count);
	std::copy(pixels, pixels + count, image.pixels.get());
	stbi_image_free(pixels);
	return image;
}

} // namespace rimgui::rt
