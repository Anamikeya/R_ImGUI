// Tests for the Vulkan research kit, on the real device, in a window that is never shown:
//   rimgui_vk_tests --vulkan --hidden --validation
// Every check reads results back from the GPU. Any validation message is a failure too.
#include <rimgui/app.h>
#include <rimgui/rt/vk/accel.h>
#include <rimgui/rt/vk/environment.h>
#include <rimgui/rt/vk/gpu.h>
#include <rimgui/rt/vk/shader.h>
#include <rimgui/rt/vk/timer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>


using namespace rimgui::rt::vk;
namespace fs = std::filesystem;

namespace {

int failures = 0, checks = 0;
std::string current;
std::vector<std::string> validation;

void check(bool ok, const std::string &what)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("  FAIL %s: %s\n", current.c_str(), what.c_str());
	}
}

bool near(float a, float b, float tolerance)
{
	return std::fabs(a - b) <= tolerance;
}

void write_text(const fs::path &path, const std::string &text)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << text;
}

void touch(const fs::path &path, int seconds)
{
	fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(seconds));
}

// An image's first mip as floats (RGBA32F or RGBA16F images; half floats converted).
std::vector<float> read_image(Image &image)
{
	const uint32_t size = format_size(image.format());
	auto buffer = Buffer::create(VkDeviceSize(image.width()) * image.height() * size, 0, Memory::Readback, "readback");
	rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
		barrier(cb);
		VkBufferImageCopy copy{};
		copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy.imageExtent = {image.width(), image.height(), 1};
		vkCmdCopyImageToBuffer(cb, image.image(), VK_IMAGE_LAYOUT_GENERAL, buffer->buffer(), 1, &copy);
	});
	std::vector<float> out(std::size_t(image.width()) * image.height() * 4);
	if (image.format() == VK_FORMAT_R32G32B32A32_SFLOAT) {
		std::memcpy(out.data(), buffer->mapped(), out.size() * 4);
	} else if (image.format() == VK_FORMAT_R16G16B16A16_SFLOAT) {
		const auto *half = static_cast<const uint16_t *>(buffer->mapped());
		for (std::size_t i = 0; i < out.size(); ++i) {
			const uint16_t h = half[i];
			const int exponent = (h >> 10) & 31, mantissa = h & 1023;
			const float sign = (h & 0x8000) ? -1.0f : 1.0f;
			out[i] = exponent == 0 ? sign * std::ldexp(float(mantissa), -24) : sign * std::ldexp(float(mantissa + 1024), exponent - 25);
		}
	}
	return out;
}

struct Fill {
	uint32_t image;
	float r, g, b, a;
};

const char *compute_glsl = R"(#version 460
#include "rimgui_vk.glsl"
layout(local_size_x = 8, local_size_y = 8) in;
layout(push_constant, scalar) uniform Constants { uint image; vec4 colour; } pc;
void main() { imageStore(u_images[pc.image], ivec2(gl_GlobalInvocationID.xy), pc.colour); }
)";

const char *compute_hlsl = R"(#include "rimgui_vk.hlsl"
struct Constants { uint image; float4 colour; };
[[vk::push_constant]] Constants pc;
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) { u_images[pc.image][id.xy] = pc.colour * 2.0; }
)";

void test_buffers()
{
	std::vector<uint32_t> data(256);
	for (uint32_t i = 0; i < 256; ++i) data[i] = i * 2654435761u;
	auto gpu_buffer = Buffer::create(1024, 0, Memory::Gpu, "test");
	gpu_buffer->upload(data.data(), 1024);
	auto back = Buffer::create(1024, 0, Memory::Readback, "back");
	rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
		VkBufferCopy copy{0, 0, 1024};
		vkCmdCopyBuffer(cb, gpu_buffer->buffer(), back->buffer(), 1, &copy);
	});
	check(std::memcmp(back->mapped(), data.data(), 1024) == 0, "a GPU buffer round-trips through a staging upload");
	check(gpu_buffer->address() != 0, "buffers have device addresses");
}

void test_compute(ShaderLibrary &shaders, const fs::path &folder)
{
	write_text(folder / "fill.comp", compute_glsl);
	write_text(folder / "fill.hlsl", compute_hlsl);
	ImageDesc d;
	d.width = d.height = 16;
	d.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	d.storage = true;
	d.name = "compute target";
	auto image = Image::create(d);
	for (const char *file : {"fill.comp", "fill.hlsl"}) {
		Pipeline &p = shaders.compute(std::string("fill ") + file, file);
		check(p.ready(), std::string(file) + " builds (" + p.error() + ")");
		if (!p.ready()) continue;
		rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
			p.bind(cb, Fill{image->storage_index(), 0.25f, 0.5f, 0.125f, 1});
			vkCmdDispatch(cb, 2, 2, 1);
		});
		const auto pixels = read_image(*image);
		const float scale = std::string(file).find(".hlsl") != std::string::npos ? 2.0f : 1.0f;
		check(near(pixels[0], 0.25f * scale, 1e-6f) && near(pixels[(15 * 16 + 15) * 4 + 1], 0.5f * scale, 1e-6f),
			std::string(file) + " wrote every pixel through the bindless storage images");
	}
}

void test_errors(ShaderLibrary &shaders, const fs::path &folder)
{
	write_text(folder / "broken.glsl", "// line 1\n// line 2\nvec4 oops() { return undefined_thing; }\n");
	write_text(folder / "uses_broken.comp", "#version 460\n#include \"broken.glsl\"\nlayout(local_size_x = 1) in;\nvoid main() { oops(); }\n");
	Pipeline &bad = shaders.compute("bad glsl", "uses_broken.comp");
	check(!bad.ready() && bad.error().find("broken.glsl:3") != std::string::npos, "a GLSL error is placed in the included file (" + bad.error() + ")");
	write_text(folder / "broken.hlsli", "// line 1\n// line 2\nfloat4 oops() { return undefined_thing; }\n");
	write_text(folder / "uses_broken.hlsl", "#include \"broken.hlsli\"\n[numthreads(1, 1, 1)] void main() { oops(); }\n");
	Pipeline &bad_hlsl = shaders.compute("bad hlsl", "uses_broken.hlsl");
	check(!bad_hlsl.ready() && bad_hlsl.error().find("broken.hlsli:3") != std::string::npos,
		"an HLSL error is placed in the included file (" + bad_hlsl.error() + ")");
	// Fixed and polled: it builds.
	write_text(folder / "broken.glsl", "vec4 oops() { return vec4(1.0); }\n");
	touch(folder / "broken.glsl", 2);
	shaders.poll(true);
	check(bad.ready() && bad.version() == 1, "the fixed shader builds on poll");
	// Broken again: the last good pipeline stays.
	const VkPipeline before = bad.handle();
	write_text(folder / "broken.glsl", "vec4 oops() { return nonsense; }\n");
	touch(folder / "broken.glsl", 4);
	shaders.poll(true);
	check(bad.ready() && bad.handle() == before && !bad.error().empty(), "a broken edit keeps the last good pipeline");
}

void test_graphics(ShaderLibrary &shaders, const fs::path &folder)
{
	write_text(folder / "triangle.vert", R"(#version 460
#include "rimgui_vk.glsl"
void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = rimgui_vk_clip(vec4(corner * 2.0 - 1.0, 0.5, 1.0));
}
)");
	write_text(folder / "colour.frag", R"(#version 460
#include "rimgui_vk.glsl"
layout(push_constant, scalar) uniform Constants { vec4 colour; } pc;
layout(location = 0) out vec4 o;
void main() { o = pc.colour * vec4(gl_FragCoord.y < 16.0 ? 1.0 : 0.5); }
)");
	RenderTarget target({VK_FORMAT_R16G16B16A16_SFLOAT}, VK_FORMAT_D32_SFLOAT, "test target");
	target.resize(32, 32);
	GraphicsDesc d;
	d.vertex = "triangle.vert";
	d.fragment = "colour.frag";
	d.colors = target.color_formats();
	d.depth = target.depth_format();
	Pipeline &p = shaders.graphics("triangle", d);
	check(p.ready(), "a graphics pipeline builds (" + p.error() + ")");
	if (!p.ready()) return;
	rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
		const float clear[4]{0, 0, 0, 0};
		target.begin(cb, clear);
		const float colour[4]{1, 0.5f, 0.25f, 1};
		p.bind(cb, colour, sizeof colour);
		vkCmdDraw(cb, 3, 1, 0, 0);
		target.end(cb);
	});
	const auto pixels = read_image(*target.color());
	check(near(pixels[0], 1, 1e-3f) && near(pixels[2], 0.25f, 1e-3f), "the triangle covers the target with the pushed colour");
	// Row 0 of the image is NDC y = -1, as in OpenGL: fragments there have gl_FragCoord.y < 16.
	check(near(pixels[(31 * 32) * 4], 0.5f, 1e-3f), "row 0 is the bottom of the picture (y = -1), as in the OpenGL lab");
}

void test_textures(ShaderLibrary &shaders, const fs::path &folder)
{
	rimgui::rt::Image source;
	source.width = source.height = 2;
	source.channels = 4;
	source.bytes = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
	source.name = "2x2";
	auto texture = Image::from_image(source, false);
	check(texture && texture->mips() == 2, "an image uploads with its mip chain");
	write_text(folder / "sample.comp", R"(#version 460
#include "rimgui_vk.glsl"
layout(local_size_x = 1) in;
layout(push_constant, scalar) uniform Constants { uint texture; uint image; } pc;
void main() {
    imageStore(u_images[pc.image], ivec2(0, 0), sample_2d_lod(pc.texture, vec2(0.25, 0.25), 0.0));
    imageStore(u_images[pc.image], ivec2(1, 0), sample_2d_lod(pc.texture, vec2(0.5, 0.5), 1.0));
}
)");
	ImageDesc d;
	d.width = 2;
	d.height = 1;
	d.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	d.storage = true;
	auto out = Image::create(d);
	Pipeline &p = shaders.compute("sample", "sample.comp");
	check(p.ready(), "a sampling compute shader builds (" + p.error() + ")");
	if (!p.ready() || !texture) return;
	struct {
		uint32_t texture, image;
	} c{texture->sampled_index(), out->storage_index()};
	rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
		p.bind(cb, c);
		vkCmdDispatch(cb, 1, 1, 1);
	});
	const auto pixels = read_image(*out);
	check(near(pixels[0], 1, 1e-3f) && near(pixels[1], 0, 1e-3f), "texel (0, 0) reads back red");
	check(near(pixels[4], 0.5f, 0.02f) && near(pixels[5], 0.5f, 0.02f) && near(pixels[6], 0.5f, 0.02f), "the 1x1 mip is the average of the four");
}

void test_ray_tracing(ShaderLibrary &shaders, const fs::path &folder)
{
	if (!rimgui::vulkan::context()->features.ray_tracing_pipeline) {
		std::printf("  (no ray tracing pipelines on this GPU: not tested)\n");
		return;
	}
	write_text(folder / "test.rgen", R"(#version 460
#extension GL_EXT_ray_tracing : require
#include "rimgui_vk.glsl"
layout(push_constant, scalar) uniform Constants { uint image; } pc;
layout(location = 0) rayPayloadEXT vec4 payload;
void main() {
    payload = vec4(float(gl_LaunchIDEXT.x) / 8.0, 0.0, 1.0, 1.0);
    imageStore(u_images[pc.image], ivec2(gl_LaunchIDEXT.xy), payload);
}
)");
	write_text(folder / "test.rmiss", R"(#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
void main() { payload = vec4(0.0); }
)");
	write_text(folder / "test.rchit", R"(#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT vec4 payload;
void main() { payload = vec4(1.0); }
)");
	RayTracingDesc d;
	d.raygen = "test.rgen";
	d.miss = {"test.rmiss"};
	d.hit_groups = {{"test.rchit", "", ""}};
	Pipeline &p = shaders.ray_tracing("rt test", d);
	check(p.ready(), "a ray tracing pipeline and its binding table build (" + p.error() + ")");
	if (!p.ready()) return;
	ImageDesc id;
	id.width = 8;
	id.height = 8;
	id.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	id.storage = true;
	auto image = Image::create(id);
	const uint32_t index = image->storage_index();
	rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
		p.bind(cb, index);
		p.trace(cb, 8, 8);
	});
	const auto pixels = read_image(*image);
	check(near(pixels[(3 * 8 + 4) * 4], 0.5f, 1e-5f) && near(pixels[2], 1, 1e-5f), "the ray generation shader ran once per pixel");
}

void test_mesh_shaders(ShaderLibrary &shaders, const fs::path &folder)
{
	if (!rimgui::vulkan::context()->features.mesh_shader) {
		std::printf("  (no mesh shaders on this GPU: not tested)\n");
		return;
	}
	write_text(folder / "test.mesh", R"(#version 460
#extension GL_EXT_mesh_shader : require
#include "rimgui_vk.glsl"
layout(local_size_x = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;
void main() {
    SetMeshOutputsEXT(3, 1);
    gl_MeshVerticesEXT[0].gl_Position = rimgui_vk_clip(vec4(-1.0, -1.0, 0.5, 1.0));
    gl_MeshVerticesEXT[1].gl_Position = rimgui_vk_clip(vec4( 3.0, -1.0, 0.5, 1.0));
    gl_MeshVerticesEXT[2].gl_Position = rimgui_vk_clip(vec4(-1.0,  3.0, 0.5, 1.0));
    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
)");
	write_text(folder / "green.frag", "#version 460\nlayout(location = 0) out vec4 o;\nvoid main() { o = vec4(0.0, 1.0, 0.0, 1.0); }\n");
	RenderTarget target({VK_FORMAT_R16G16B16A16_SFLOAT}, VK_FORMAT_UNDEFINED, "mesh target");
	target.resize(16, 16);
	GraphicsDesc d;
	d.mesh = "test.mesh";
	d.fragment = "green.frag";
	d.colors = target.color_formats();
	d.cull = VK_CULL_MODE_NONE;
	Pipeline &p = shaders.graphics("mesh test", d);
	check(p.ready(), "a mesh shader pipeline builds (" + p.error() + ")");
	if (!p.ready()) return;
	rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
		const float clear[4]{0, 0, 0, 0};
		target.begin(cb, clear);
		p.bind(cb);
		vkCmdDrawMeshTasksEXT(cb, 1, 1, 1);
		target.end(cb);
	});
	const auto pixels = read_image(*target.color());
	check(near(pixels[(8 * 16 + 8) * 4 + 1], 1, 1e-3f), "the mesh shader's triangle covers the target");
}

// An equirectangular HDR image: one brightness everywhere, or that and one blinding pixel (a photographed sun).
bool write_sky(const fs::path &path, float brightness, float sun)
{
	const int w = 64, h = 32;
	std::vector<float> pixels(std::size_t(w * h * 3), brightness);
	if (sun > 0) {
		const std::size_t i = std::size_t((8 * w + 20) * 3);
		pixels[i] = pixels[i + 1] = pixels[i + 2] = sun;
	}
	return rimgui::rt::write_hdr_bottom_up(path, w, h, pixels.data());
}

// One level of one layer of an image, as floats (RGBA32F, or RG16F widened to four channels with zeros).
std::vector<float> read_level(Image &image, uint32_t mip, uint32_t layer)
{
	const uint32_t w = std::max(image.width() >> mip, 1u), h = std::max(image.height() >> mip, 1u);
	const uint32_t size = format_size(image.format());
	auto buffer = Buffer::create(VkDeviceSize(w) * h * size, 0, Memory::Readback, "level readback");
	rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
		barrier(cb);
		VkBufferImageCopy copy{};
		copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
		copy.imageExtent = {w, h, 1};
		vkCmdCopyImageToBuffer(cb, image.image(), VK_IMAGE_LAYOUT_GENERAL, buffer->buffer(), 1, &copy);
	});
	std::vector<float> out(std::size_t(w) * h * 4, 0.0f);
	if (image.format() == VK_FORMAT_R32G32B32A32_SFLOAT) {
		std::memcpy(out.data(), buffer->mapped(), out.size() * 4);
	} else if (image.format() == VK_FORMAT_R16G16_SFLOAT) {
		const auto *half = static_cast<const uint16_t *>(buffer->mapped());
		for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
			for (int c = 0; c < 2; ++c) {
				const uint16_t v = half[i * 2 + std::size_t(c)];
				const int exponent = (v >> 10) & 31, mantissa = v & 1023;
				out[i * 4 + std::size_t(c)] = exponent == 0 ? std::ldexp(float(mantissa), -24) : std::ldexp(float(mantissa + 1024), exponent - 25);
			}
		}
	}
	return out;
}

std::array<float, 4> middle_texel(Image &cube, uint32_t mip, uint32_t face)
{
	const auto texels = read_level(cube, mip, face);
	const uint32_t size = std::max(cube.width() >> mip, 1u);
	const std::size_t m = (std::size_t(size / 2) * size + size / 2) * 4;
	return {texels[m], texels[m + 1], texels[m + 2], texels[m + 3]};
}

// The image-based lighting maps, built by compute shaders, against what they must be (as the OpenGL kit's test).
void test_environment(ShaderLibrary &shaders, const fs::path &folder)
{
	Environment env;
	std::string error;
	check(write_sky(folder / "grey.hdr", 0.5f, 0), "test sky written");
	check(env.load(folder / "grey.hdr", shaders, &error), "an HDR sky loads (" + error + ")");
	if (!env.loaded()) {
		return;
	}
	bool irradiance_ok = true, prefiltered_ok = true;
	for (uint32_t face = 0; face < 6; ++face) {
		const auto i = middle_texel(*env.irradiance(), 0, face);
		irradiance_ok = irradiance_ok && near(i[0], 0.5f, 0.02f) && near(i[2], 0.5f, 0.02f);
		for (uint32_t level = 0; level < uint32_t(env.prefiltered_levels()); ++level) {
			const auto p = middle_texel(*env.prefiltered(), level, face);
			prefiltered_ok = prefiltered_ok && near(p[1], 0.5f, 0.02f);
		}
	}
	check(irradiance_ok, "a constant sky's irradiance / pi equals its brightness on every face");
	check(prefiltered_ok, "every prefiltered level of a constant sky equals its brightness");
	Image &lut = *env.brdf_lut();
	const auto table = read_level(lut, 0, 0);
	const uint32_t w = lut.width(), h = lut.height();
	const auto at = [&](uint32_t x, uint32_t y) { return std::array<float, 2>{table[(y * w + x) * 4], table[(y * w + x) * 4 + 1]}; };
	const auto head_on = at(w - 1, 0);
	check(near(head_on[0], 1, 0.03f) && near(head_on[1], 0, 0.03f), "BRDF table: smooth, head-on is F0 (scale 1, bias 0)");
	const auto rough = at(w - 1, h - 1);
	check(rough[0] + rough[1] < 0.95f && rough[0] + rough[1] > 0.3f, "BRDF table: rough surfaces lose energy to single scattering");

	check(write_sky(folder / "sun.hdr", 0.5f, 2.0e6f), "test sun written");
	check(env.load(folder / "sun.hdr", shaders, &error), "a sky with a blinding sun loads (" + error + ")");
	bool finite = true;
	for (uint32_t face = 0; face < 6; ++face) {
		for (const float value : middle_texel(*env.irradiance(), 0, face)) finite = finite && std::isfinite(value);
		for (uint32_t level = 0; level < uint32_t(env.prefiltered_levels()); ++level) {
			for (const float value : middle_texel(*env.prefiltered(), level, face)) finite = finite && std::isfinite(value);
		}
	}
	check(finite, "a blinding sun gives finite irradiance and reflections (no infinity, no NaN)");
	check(!env.load(folder / "missing.hdr", shaders, &error) && env.loaded(), "a missing file is refused and the last environment stays");
}

// Acceleration structures against answers known exactly: a ray from (0, 0, 5) down -z hits a unit cube at the origin
// at t = 4.5 and finds its object; hidden, the ray misses; moved to z = 2, the top level is rebuilt and t = 2.5.
void test_acceleration_structures(ShaderLibrary &shaders, const fs::path &folder)
{
	if (!AccelerationStructures::supported() || !rimgui::vulkan::context()->features.ray_query) {
		std::printf("  (no ray queries on this GPU: not tested)\n");
		return;
	}
	write_text(folder / "query.comp", R"(#version 460
#extension GL_EXT_ray_query : require
#define RIMGUI_RAY_QUERY
#include "rimgui_vk.glsl"
layout(local_size_x = 1) in;
layout(push_constant, scalar) uniform Constants { uint image; } pc;
void main() {
    rayQueryEXT q;
    rayQueryInitializeEXT(q, u_scene, gl_RayFlagsNoneEXT, 0xFF, vec3(0.0, 0.0, 5.0), 0.0, vec3(0.0, 0.0, -1.0), 100.0);
    while (rayQueryProceedEXT(q)) {}
    bool hit = rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
    float t = hit ? rayQueryGetIntersectionTEXT(q, true) : -1.0;
    float object = hit ? float(rayQueryGetIntersectionInstanceCustomIndexEXT(q, true)) : -1.0;
    imageStore(u_images[pc.image], ivec2(0, 0), vec4(t, object, 0.0, 1.0));
}
)");
	Pipeline &p = shaders.compute("ray query", "query.comp");
	check(p.ready(), "a ray query compute shader builds (" + p.error() + ")");
	if (!p.ready()) return;
	ImageDesc d;
	d.width = d.height = 1;
	d.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	d.storage = true;
	auto out = Image::create(d);
	const auto trace = [&] {
		rimgui::vulkan::submit_now([&](VkCommandBuffer cb) {
			p.bind(cb, out->storage_index());
			vkCmdDispatch(cb, 1, 1, 1);
		});
		return read_image(*out);
	};
	Scene scene;
	Object floor_far, cube;
	floor_far.mesh = std::make_shared<Mesh>(rimgui::rt::make_plane(), "plane");
	floor_far.position = {0, -10, 0};
	cube.mesh = std::make_shared<Mesh>(rimgui::rt::make_cube(), "cube");
	scene.objects = {floor_far, cube};
	AccelerationStructures accel;
	check(accel.update(scene), "the acceleration structures build");
	check(accel.bottom_levels() == 2 && accel.instances() == 2, "one bottom level per mesh, one instance per visible object");
	auto hit = trace();
	check(near(hit[0], 4.5f, 1e-4f) && near(hit[1], 1, 1e-6f), "the ray hits the cube's face at t = 4.5 and finds object 1");
	const int builds = accel.builds();
	accel.update(scene);
	check(accel.builds() == builds, "an unchanged scene is not rebuilt");
	scene.objects[1].visible = false;
	accel.update(scene);
	hit = trace();
	check(hit[0] < 0, "a hidden object is not hit");
	scene.objects[1].visible = true;
	scene.objects[1].position = {0, 0, 2};
	accel.update(scene);
	hit = trace();
	check(near(hit[0], 2.5f, 1e-4f), "a moved object is hit where it now is (t = 2.5)");
}

} // namespace

int main(int argc, char **argv)
{
	rimgui::AppConfig config;
	config.title = "rimgui_vk_tests";
	config.backend = rimgui::Backend::Vulkan;
	config.vulkan_validation = true;
	rimgui::vulkan::on_message([](int severity, const std::string &text) {
		if (severity >= 2) {
			validation.push_back(text);
			std::printf("  VALIDATION: %s\n", text.c_str());
		}
	});
	const fs::path folder = fs::temp_directory_path() / "rimgui_vk_tests";
	std::error_code ignored;
	fs::remove_all(folder, ignored);
	fs::create_directories(folder);
	std::unique_ptr<ShaderLibrary> shaders;
	std::unique_ptr<GpuTimers> timers;
	std::shared_ptr<Image> timed_image;
	// Everything on the GPU goes before the kit, and the kit before the device.
	config.on_exit = [&] {
		timed_image.reset();
		timers.reset();
		shaders.reset();
		Gpu::stop();
	};
	config.on_start = [&] {
		const auto &c = *rimgui::vulkan::context();
		std::printf("%s, Vulkan %u.%u, ray tracing %s, ray queries %s, mesh shaders %s, validation %s\n", c.device_name.c_str(),
			VK_API_VERSION_MAJOR(c.api_version), VK_API_VERSION_MINOR(c.api_version), c.features.ray_tracing_pipeline ? "yes" : "no",
			c.features.ray_query ? "yes" : "no", c.features.mesh_shader ? "yes" : "no", c.features.validation ? "on" : "off");
		Gpu::start();
		shaders = std::make_unique<ShaderLibrary>(std::vector<fs::path>{folder, RIMGUI_KIT_SHADERS});
		timers = std::make_unique<GpuTimers>();
		const std::pair<const char *, std::function<void()>> tests[]{
			{"buffers", test_buffers},
			{"compute (GLSL and HLSL)", [&] { test_compute(*shaders, folder); }},
			{"shader errors and reload", [&] { test_errors(*shaders, folder); }},
			{"graphics", [&] { test_graphics(*shaders, folder); }},
			{"textures and mips", [&] { test_textures(*shaders, folder); }},
			{"ray tracing pipeline", [&] { test_ray_tracing(*shaders, folder); }},
			{"mesh shaders", [&] { test_mesh_shaders(*shaders, folder); }},
			{"image-based lighting", [&] { test_environment(*shaders, folder); }},
			{"acceleration structures", [&] { test_acceleration_structures(*shaders, folder); }},
		};
		for (const auto &[name, run] : tests) {
			current = name;
			const int before = failures;
			try {
				run();
			} catch (const std::exception &e) {
				check(false, std::string("threw: ") + e.what());
			}
			std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", name);
		}
	};
	int frame = 0;
	const int code = rimgui::run(config, [&] {
		// Timestamps need real frames: a few with one timed dispatch each.
		VkCommandBuffer cb = rimgui::vulkan::context()->command_buffer;
		timers->begin_frame();
		{
			auto t = timers->scope(cb, "dispatch");
			Pipeline &p = shaders->compute("fill fill.comp", "fill.comp");
			if (p.ready()) {
				if (!timed_image) {
					ImageDesc d;
					d.width = d.height = 256;
					d.format = VK_FORMAT_R32G32B32A32_SFLOAT;
					d.storage = true;
					timed_image = Image::create(d);
				}
				p.bind(cb, Fill{timed_image->storage_index(), 1, 1, 1, 1});
				vkCmdDispatch(cb, 32, 32, 1);
			}
		}
		timers->end_frame();
		if (++frame == 12) {
			current = "timestamps";
			const bool measured = !timers->stats().empty() && !timers->stats()[0].gpu_history.empty() && timers->stats()[0].gpu_history.back() > 0;
			check(measured, "GPU time arrives for a timed pass a frame or two later");
			std::printf("%s timestamps\n", measured ? "PASS" : "FAIL");
			rimgui::quit();
		}
	}, argc, argv);
	current = "validation";
	check(validation.empty(), std::to_string(validation.size()) + " validation messages");
	std::printf("%s validation (%zu messages)\n", validation.empty() ? "PASS" : "FAIL", validation.size());
	std::printf("%d checks, %d failed\n", checks, failures);
	return code != 0 ? code : (failures == 0 ? 0 : 1);
}
