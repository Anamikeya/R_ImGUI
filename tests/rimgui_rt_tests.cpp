// Tests for the research kit, on a real OpenGL context in a window that is never shown.
//
//   rimgui_rt_tests                     runs every test; exit code 0 when all pass
//   rimgui_rt_tests --fixtures <folder> writes the files the RenderLab checks use (an HDR sky), then exits
//
// What they hold the kit to: answers that can be known exactly. A sky of one constant brightness must give that
// brightness back from the irradiance and every prefiltered level; the BRDF table must give F0 untouched for a smooth
// surface seen head-on; a shader error inside an included file must be reported at that file's line.
#include <rimgui/rt/environment.h>
#include <rimgui/rt/gl.h>
#include <rimgui/rt/math.h>
#include <rimgui/rt/mesh.h>
#include <rimgui/rt/meshlets.h>
#include <rimgui/rt/model.h>
#include <rimgui/rt/scene.h>
#include <rimgui/rt/settings.h>
#include <rimgui/rt/shader.h>
#include <rimgui/rt/target.h>
#include <rimgui/rt/texture.h>
#include <rimgui/rt/timer.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace rimgui::rt;
namespace fs = std::filesystem;

namespace {

int failures = 0, checks = 0;
std::string current;

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

fs::path temp_folder()
{
	const fs::path folder = fs::temp_directory_path() / "rimgui_rt_tests";
	std::error_code ignored;
	fs::remove_all(folder, ignored);
	fs::create_directories(folder);
	return folder;
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
	return stbi_write_hdr(path.string().c_str(), w, h, 3, pixels.data()) != 0;
}

void test_math()
{
	mat4 m = translate({1, 2, 3}) * rotate(0.7f, {0.3f, 1, 0.2f}) * scale({2, 0.5f, 1.5f});
	const mat4 identity = inverse(m) * m;
	bool ok = true;
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			ok = ok && near(identity.at(r, c), r == c ? 1.0f : 0.0f, 1e-4f);
		}
	}
	check(ok, "inverse(m) * m is the identity");
	const mat4 view = look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
	const vec4 p = view * vec4{0, 0, 0, 1};
	check(near(p.z, -5, 1e-5f), "look_at puts the target 5 m in front (-z)");
	const vec4 clip = perspective(1.0f, 1.0f, 1.0f, 100.0f) * vec4{0, 0, -1, 1};
	check(near(clip.z / clip.w, -1, 1e-5f), "perspective maps the near plane to -1");
}

void test_settings(const fs::path &folder)
{
	Settings save(Settings::Mode::Save);
	float f = 1.25f;
	int i = 7;
	bool b = true;
	vec3 v{1, 2, 3};
	std::string text = "a = b with spaces";
	save.push("outer");
	save.push("inner");
	save.value("f", f);
	save.pop();
	save.value("i", i);
	save.pop();
	save.value("b", b);
	save.value("v", v);
	save.value("text", text);
	const fs::path file = folder / "lab.settings";
	check(save.write_file(file), "settings write");
	check(!fs::exists(file.string() + ".tmp"), "no temporary file is left behind");
	Settings load(Settings::Mode::Load);
	check(load.read_file(file), "settings read");
	float f2 = 0;
	int i2 = 0;
	bool b2 = false;
	vec3 v2;
	std::string text2;
	int untouched = 42;
	load.push("outer");
	load.push("inner");
	load.value("f", f2);
	load.pop();
	load.value("i", i2);
	load.value("missing", untouched);
	load.pop();
	load.value("b", b2);
	load.value("v", v2);
	load.value("text", text2);
	check(f2 == f && i2 == i && b2 == b && v2.x == 1 && v2.y == 2 && v2.z == 3, "values come back as saved");
	check(text2 == text, "text with '=' and spaces comes back whole");
	check(untouched == 42, "a missing key leaves the value alone");
	Settings junk(Settings::Mode::Load);
	junk.parse("# comment\nnot a setting\nx = notanumber\n = empty key\n");
	float x = 3;
	junk.value("x", x);
	check(x == 3, "an unreadable number leaves the value alone");
}

void test_shaders(const fs::path &folder)
{
	write_text(folder / "common.glsl", "// line 1\nfloat brightness() { return 1.0; }\n");
	write_text(folder / "broken.glsl", "// line 1\n// line 2\nfloat oops() { return undefined_thing; }\n");
	write_text(folder / "test.vert", "#version 330 core\nvoid main() { gl_Position = vec4(0.0); }\n");
	write_text(folder / "good.frag", "#version 330 core\n#include \"common.glsl\"\nout vec4 o;\nvoid main() { o = vec4(brightness()); }\n");
	write_text(folder / "bad.frag", "#version 330 core\n#include \"common.glsl\"\n#include \"broken.glsl\"\nout vec4 o;\nvoid main() { o = vec4(oops()); }\n");
	ShaderLibrary shaders({folder});
	std::vector<std::string> messages;
	shaders.on_message([&](bool, const std::string &text) { messages.push_back(text); });
	Program &good = shaders.graphics("good", "test.vert", "good.frag");
	check(good.ready() && good.error().empty(), "a program with an include builds");
	Program &bad = shaders.graphics("bad", "test.vert", "bad.frag");
	check(!bad.ready(), "a program with an error does not build");
	check(bad.error().find("broken.glsl:3") != std::string::npos, "the error names the included file and its line (got: " + bad.error() + ")");
	Program &missing = shaders.graphics("missing", "test.vert", "nowhere.frag");
	check(!missing.ready() && missing.error().find("nowhere.frag") != std::string::npos, "a missing file is named");
	// Fixing the file and polling builds it; the version counts up.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	write_text(folder / "broken.glsl", "// line 1\n// line 2\nfloat oops() { return 0.5; }\n");
	fs::last_write_time(folder / "broken.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(2));
	check(shaders.poll(true), "poll sees the changed include");
	check(bad.ready() && bad.version() == 1, "the fixed program builds");
	// Breaking a working program keeps the last good one.
	const GLuint before = good.id();
	write_text(folder / "common.glsl", "float brightness() { return nonsense; }\n");
	fs::last_write_time(folder / "common.glsl", fs::file_time_type::clock::now() + std::chrono::seconds(4));
	shaders.poll(true);
	check(good.ready() && good.id() == before && !good.error().empty(), "a program broken by an edit keeps its last good build");
	if (has_gl43()) {
		write_text(folder / "fill.comp",
			"#version 430 core\nlayout(local_size_x = 8, local_size_y = 8) in;\nlayout(rgba16f, binding = 0) uniform writeonly image2D u_out;\n"
			"void main() { imageStore(u_out, ivec2(gl_GlobalInvocationID.xy), vec4(0.25, 0.5, 0.75, 1.0)); }\n");
		Program &fill = shaders.compute("fill", "fill.comp");
		check(fill.ready(), "a compute shader builds (" + fill.error() + ")");
		const GLuint texture = make_texture(Format::RGBA16F, 16, 16);
		fill.use();
		glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute(2, 2, 1);
		glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
		std::vector<float> pixels(16 * 16 * 4);
		glBindTexture(GL_TEXTURE_2D, texture);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
		check(near(pixels[0], 0.25f, 1e-3f) && near(pixels[(15 * 16 + 15) * 4 + 2], 0.75f, 1e-3f), "the compute shader wrote every pixel");
		glDeleteTextures(1, &texture);
	} else {
		std::printf("  (OpenGL 4.3 is not here: compute shaders not tested)\n");
	}
}

void test_targets()
{
	const Format all[]{Format::RGBA8, Format::RGBA16F, Format::RGBA32F, Format::R8, Format::R16F, Format::R32F, Format::RG16F, Format::RG32F, Format::R11G11B10F};
	for (const Format f : all) {
		RenderTarget target({f}, true, to_string(f));
		target.resize(33, 17);
		glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
		check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, std::string(to_string(f)) + " with depth is a complete framebuffer");
	}
	RenderTarget depth_only({}, true, "depth only");
	depth_only.resize(64, 64);
	glBindFramebuffer(GL_FRAMEBUFFER, depth_only.framebuffer());
	check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "a depth-only target is complete");
	RenderTarget mrt({Format::RGBA8, Format::RGBA16F, Format::RGBA16F}, true, "G-buffer");
	check(mrt.resize(10, 10) && !mrt.resize(10, 10) && mrt.resize(0, -5), "resize remakes only on a change, and survives a zero size");
	check(mrt.width() == 1 && mrt.height() == 1, "a zero size becomes 1x1");
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void test_meshes_and_models(const fs::path &folder)
{
	write_text(folder / "quad.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n");
	std::string error;
	const MeshData quad = load_obj(folder / "quad.obj", &error);
	check(quad.indices.size() == 6, "an OBJ quad becomes two triangles");
	check(!quad.vertices.empty() && near(quad.vertices[0].uv.y, 1, 1e-6f), "OBJ texture v is turned round (0 at the image's top)");
	check(!quad.vertices.empty() && near(quad.vertices[0].normal.z, 1, 1e-5f), "missing OBJ normals are made");

	// A minimal glTF: one triangle in a base64 buffer, a material with factors and an albedo image beside it.
	const unsigned char pixels[]{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
	check(stbi_write_png((folder / "albedo.png").string().c_str(), 2, 2, 4, pixels, 8) != 0, "test image written");
	// Positions (3 x vec3 float) then indices (3 x uint16, padded): 36 + 8 bytes.
	const float positions[9]{0, 0, 0, 1, 0, 0, 0, 2, 0};
	const unsigned short indices[4]{0, 1, 2, 0};
	std::string bytes(reinterpret_cast<const char *>(positions), sizeof positions);
	bytes.append(reinterpret_cast<const char *>(indices), sizeof indices);
	static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string base64;
	for (std::size_t i = 0; i < bytes.size(); i += 3) {
		const unsigned n = (unsigned(static_cast<unsigned char>(bytes[i])) << 16) |
						   (i + 1 < bytes.size() ? unsigned(static_cast<unsigned char>(bytes[i + 1])) << 8 : 0u) |
						   (i + 2 < bytes.size() ? unsigned(static_cast<unsigned char>(bytes[i + 2])) : 0u);
		base64 += alphabet[(n >> 18) & 63];
		base64 += alphabet[(n >> 12) & 63];
		base64 += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
		base64 += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
	}
	const std::string gltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0, "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0, "translation": [0, 1, 0]}],
  "meshes": [{"name": "tri", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
  "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.25, 1, 1], "metallicFactor": 0.75, "roughnessFactor": 0.3,
                 "baseColorTexture": {"index": 0}}, "alphaMode": "MASK", "alphaCutoff": 0.4, "doubleSided": true}],
  "textures": [{"source": 0}], "images": [{"uri": "albedo.png"}],
  "buffers": [{"byteLength": 44, "uri": "data:application/octet-stream;base64,)" + base64 + R"("}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 2, 0]},
                {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}]
})";
	write_text(folder / "tri.gltf", gltf);
	const Model model = load_model(folder / "tri.gltf", &error);
	check(model.parts.size() == 1, "a glTF triangle loads as one part (" + error + ")");
	if (!model.parts.empty()) {
		const Object &o = model.parts[0];
		const Material &m = o.material;
		check(near(m.albedo.y, 0.25f, 1e-6f) && near(m.metallic, 0.75f, 1e-6f) && near(m.roughness, 0.3f, 1e-6f), "material factors are read");
		check(m.alpha_mode == AlphaMode::Mask && near(m.alpha_cutoff, 0.4f, 1e-6f) && m.double_sided, "alpha mode, cutoff and double-sided are read");
		check(m.albedo_map && m.albedo_map->width() == 2 && m.albedo_map->height() == 2, "the albedo image beside the file is loaded");
		check(near(o.base.at(1, 3), 1, 1e-6f), "the node's translation is kept");
		check(near(model.bounds_max.y, 3, 1e-5f) && near(model.bounds_min.y, 1, 1e-5f), "bounds include the node transform");
		check(o.mesh && o.mesh->triangles() == 1, "one triangle");
	}
	// The same image loaded again is the same texture.
	const auto t1 = load_texture(folder / "albedo.png", true), t2 = load_texture(folder / "albedo.png", true);
	check(t1 && t1 == t2, "a texture loaded twice is shared");
	check(!load_texture(folder / "nothing.png", true, &error) && error.find("nothing.png") != std::string::npos, "a missing image is named");

	// A compressed file is refused by name instead of loading as nothing.
	std::string draco = gltf;
	draco.replace(draco.find("\"asset\""), 7, "\"extensionsUsed\": [\"KHR_draco_mesh_compression\"], \"extensionsRequired\": [\"KHR_draco_mesh_compression\"], \"asset\"");
	write_text(folder / "draco.gltf", draco);
	error.clear();
	const Model refused = load_model(folder / "draco.gltf", &error);
	check(refused.parts.empty() && error.find("KHR_draco_mesh_compression") != std::string::npos, "a Draco-compressed file is refused by name");
	write_text(folder / "garbage.gltf", "{ this is not json");
	error.clear();
	check(load_model(folder / "garbage.gltf", &error).parts.empty() && !error.empty(), "a broken file is refused with a reason");
}

// One texel of a cubemap face, as floats.
std::array<float, 4> cube_texel(GLuint cube, int face, int level)
{
	glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
	GLint size = 0;
	glGetTexLevelParameteriv(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), level, GL_TEXTURE_WIDTH, &size);
	std::vector<float> pixels(std::size_t(size * size * 4));
	glGetTexImage(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), level, GL_RGBA, GL_FLOAT, pixels.data());
	const std::size_t middle = std::size_t(((size / 2) * size + size / 2) * 4);
	return {pixels[middle], pixels[middle + 1], pixels[middle + 2], pixels[middle + 3]};
}

void test_environment(const fs::path &folder, const fs::path &kit_shaders)
{
	ShaderLibrary shaders({kit_shaders});
	Environment env;
	std::string error;
	check(write_sky(folder / "grey.hdr", 0.5f, 0), "test sky written");
	check(env.load(folder / "grey.hdr", shaders, &error), "an HDR sky loads (" + error + ")");
	if (!env.loaded()) {
		return;
	}
	// A sky of one brightness L: irradiance / pi is L everywhere, and so is every prefiltered level.
	bool irradiance_ok = true, prefiltered_ok = true;
	for (int face = 0; face < 6; ++face) {
		const auto i = cube_texel(env.irradiance(), face, 0);
		irradiance_ok = irradiance_ok && near(i[0], 0.5f, 0.02f) && near(i[2], 0.5f, 0.02f);
		for (int level = 0; level < env.prefiltered_levels(); ++level) {
			const auto p = cube_texel(env.prefiltered(), face, level);
			prefiltered_ok = prefiltered_ok && near(p[1], 0.5f, 0.02f);
		}
	}
	check(irradiance_ok, "a constant sky's irradiance / pi equals its brightness on every face");
	check(prefiltered_ok, "every prefiltered level of a constant sky equals its brightness");
	// The BRDF table: head-on and smooth reflects F0 untouched (scale 1, bias 0); grazing and smooth, all of it (bias 1).
	glBindTexture(GL_TEXTURE_2D, env.brdf_lut());
	GLint w = 0, h = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
	std::vector<float> lut(std::size_t(w * h * 2));
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_FLOAT, lut.data());
	const auto at = [&](int x, int y) { return std::array<float, 2>{lut[std::size_t((y * w + x) * 2)], lut[std::size_t((y * w + x) * 2 + 1)]}; };
	const auto head_on = at(w - 1, 0);
	check(near(head_on[0], 1, 0.03f) && near(head_on[1], 0, 0.03f), "BRDF table: smooth, head-on is F0 (scale 1, bias 0)");
	const auto rough = at(w - 1, h - 1);
	check(rough[0] + rough[1] < 0.95f && rough[0] + rough[1] > 0.3f, "BRDF table: rough surfaces lose energy to single scattering");

	// A photographed sun is far brighter than half floats hold: nothing made from it may be infinite or NaN.
	check(write_sky(folder / "sun.hdr", 0.5f, 2.0e6f), "test sun written");
	check(env.load(folder / "sun.hdr", shaders, &error), "a sky with a blinding sun loads (" + error + ")");
	bool finite = true;
	for (int face = 0; face < 6; ++face) {
		for (const auto value : cube_texel(env.irradiance(), face, 0)) finite = finite && std::isfinite(value);
		for (int level = 0; level < env.prefiltered_levels(); ++level) {
			for (const auto value : cube_texel(env.prefiltered(), face, level)) finite = finite && std::isfinite(value);
		}
	}
	check(finite, "a blinding sun gives finite irradiance and reflections (no infinity, no NaN)");
	check(!env.load(folder / "missing.hdr", shaders, &error) && env.loaded(), "a missing file is refused and the last environment stays");
}

// Meshlets: every triangle in exactly one meshlet with its own three vertices, within the limits; a flat floor's
// meshlets have cones straight up that cull from below and not from above.
void test_meshlets()
{
	for (const auto &[name, mesh] : {std::pair<const char *, MeshData>{"sphere", make_sphere()}, {"torus", make_torus()}, {"plane", make_plane()}}) {
		const MeshletData m = build_meshlets(mesh);
		std::vector<std::array<unsigned, 3>> rebuilt;
		bool within = true;
		for (const Meshlet &ml : m.meshlets) {
			within = within && ml.vertex_count <= meshlet_max_vertices && ml.triangle_count <= meshlet_max_triangles && ml.triangle_count > 0;
			for (uint32_t t = 0; t < ml.triangle_count; ++t) {
				const uint32_t packed = m.triangles[ml.triangle_offset + t];
				std::array<unsigned, 3> tri{};
				for (int k = 0; k < 3; ++k) {
					const uint32_t local = (packed >> (8 * k)) & 0xFF;
					within = within && local < ml.vertex_count;
					tri[std::size_t(k)] = m.vertices[ml.vertex_offset + local];
				}
				rebuilt.push_back(tri);
			}
		}
		bool same = rebuilt.size() * 3 == mesh.indices.size();
		for (std::size_t t = 0; same && t < rebuilt.size(); ++t) {
			same = rebuilt[t][0] == mesh.indices[t * 3] && rebuilt[t][1] == mesh.indices[t * 3 + 1] && rebuilt[t][2] == mesh.indices[t * 3 + 2];
		}
		check(within, std::string(name) + ": meshlets keep to 64 vertices and 124 triangles");
		check(same, std::string(name) + ": every triangle is in a meshlet once, with its own vertices, in order");
	}
	const MeshletData floor = build_meshlets(make_plane());
	bool up = !floor.meshlets.empty();
	for (const Meshlet &ml : floor.meshlets) {
		up = up && near(ml.cone_axis.y, 1, 1e-4f) && ml.cone_cutoff < 1e-3f;
		// Seen from below every triangle faces away (culled); from above none does.
		const float away = ml.radius + 3;
		const vec3 below = ml.centre - vec3{0, away, 0}, above = ml.centre + vec3{0, away, 0};
		const auto culled = [&](vec3 eye) { return dot(ml.centre - eye, ml.cone_axis) >= ml.cone_cutoff * length(ml.centre - eye) + ml.radius; };
		up = up && culled(below) && !culled(above);
	}
	check(up, "a floor's meshlets face up, and their cones cull them from below only");
}

void test_scene()
{
	Scene scene;
	Object o;
	o.mesh = std::make_shared<Mesh>(make_cube(), "cube");
	scene.objects.push_back(o);
	const std::size_t before = scene.fingerprint();
	scene.objects[0].material.roughness = 0.9f;
	check(scene.fingerprint() != before, "a material change changes the fingerprint");
	const std::size_t after = scene.fingerprint();
	check(scene.fingerprint() == after, "the fingerprint is stable");
	vec3 centre;
	float radius = 0;
	scene.bounds(centre, radius);
	check(near(radius, std::sqrt(3.0f) * 0.5f, 1e-4f), "bounds of a unit cube");
}

void test_timers()
{
	GpuTimers timers;
	RenderTarget target({Format::RGBA8}, false, "t");
	target.resize(256, 256);
	for (int frame = 0; frame < 12; ++frame) {
		{
			auto t = timers.scope("Clear");
			target.bind();
			const float c[4]{1, 0, 0, 1};
			target.clear(c);
		}
		timers.end_frame();
		glFinish();
	}
	check(timers.stats().size() == 1 && timers.stats()[0].name == "Clear", "a timed pass is listed");
	check(!timers.stats()[0].gpu_history.empty(), "GPU times arrive a few frames later");
}

} // namespace

// Two pictures of the same view, from the OpenGL and the Vulkan lab: the mean difference in levels (0 to 255) and the
// share of pixels that differ by more than 16 levels in any channel, against the limits given.
int compare_pictures(const fs::path &a, const fs::path &b, double max_mean, double max_share)
{
	std::string error;
	const auto x = decode_image(a, &error), y = decode_image(b, &error);
	if (!x || !y) {
		std::printf("FAIL: %s\n", error.c_str());
		return 1;
	}
	if (x->width != y->width || x->height != y->height || x->channels != y->channels) {
		std::printf("FAIL: %dx%d and %dx%d pictures\n", x->width, x->height, y->width, y->height);
		return 1;
	}
	const int c = x->channels;
	double total = 0;
	std::size_t different = 0;
	const std::size_t pixels = std::size_t(x->width) * std::size_t(x->height);
	for (std::size_t i = 0; i < pixels; ++i) {
		int most = 0;
		for (int k = 0; k < std::min(c, 3); ++k) {
			const int d = std::abs(int(x->bytes[i * std::size_t(c) + std::size_t(k)]) - int(y->bytes[i * std::size_t(c) + std::size_t(k)]));
			total += d;
			most = std::max(most, d);
		}
		different += most > 16 ? 1 : 0;
	}
	const double mean = total / double(pixels * std::size_t(std::min(c, 3)));
	const double share = double(different) / double(pixels);
	const bool ok = mean <= max_mean && share <= max_share;
	std::printf("%s: mean difference %.3f levels (limit %.3f), %.4f%% of pixels differ by more than 16 levels (limit %.4f%%)\n",
		ok ? "PASS" : "FAIL", mean, max_mean, share * 100, max_share * 100);
	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc == 6 && std::string(argv[1]) == "--compare") {
		return compare_pictures(argv[2], argv[3], std::atof(argv[4]), std::atof(argv[5]));
	}
	std::string fixtures;
	for (int i = 1; i + 1 < argc; ++i) {
		if (std::string(argv[i]) == "--fixtures") {
			fixtures = argv[i + 1];
		}
	}
	if (!fixtures.empty()) {
		fs::create_directories(fixtures);
		const bool ok = write_sky(fs::path(fixtures) / "test_sky.hdr", 0.6f, 4.0e5f);
		std::printf("%s %s\n", ok ? "wrote" : "could not write", (fs::path(fixtures) / "test_sky.hdr").string().c_str());
		return ok ? 0 : 1;
	}
	if (!glfwInit()) {
		std::printf("FAIL: the window system did not start\n");
		return 1;
	}
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	GLFWwindow *window = glfwCreateWindow(64, 64, "rimgui_rt_tests", nullptr, nullptr);
	if (!window) {
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		window = glfwCreateWindow(64, 64, "rimgui_rt_tests", nullptr, nullptr);
	}
	if (!window) {
		std::printf("FAIL: no OpenGL 3.3 context\n");
		return 1;
	}
	glfwMakeContextCurrent(window);
	if (!load_gl()) {
		std::printf("FAIL: OpenGL could not be loaded\n");
		return 1;
	}
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	std::printf("%s\n", gl_description().c_str());
	const fs::path folder = temp_folder();
	const fs::path kit_shaders = RIMGUI_KIT_SHADERS;
	const std::pair<const char *, std::function<void()>> tests[]{
		{"math", test_math},
		{"settings", [&] { test_settings(folder); }},
		{"shaders", [&] { test_shaders(folder); }},
		{"render targets", test_targets},
		{"meshes and models", [&] { test_meshes_and_models(folder); }},
		{"environment", [&] { test_environment(folder, kit_shaders); }},
		{"scene", test_scene},
		{"meshlets", test_meshlets},
		{"timers", test_timers},
	};
	for (const auto &[name, run] : tests) {
		current = name;
		const int before = failures;
		run();
		while (glGetError() != GL_NO_ERROR) {
			check(false, "OpenGL reported an error");
		}
		std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", name);
	}
	std::printf("%d checks, %d failed\n", checks, failures);
	glfwDestroyWindow(window);
	glfwTerminate();
	return failures == 0 ? 0 : 1;
}
