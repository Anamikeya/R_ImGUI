// RenderLabVulkan: RenderLab on Vulkan. Techniques written in Vulkan through the research kit (rimgui/rt/vk/lab.h),
// in the same application as the OpenGL RenderLab, so the two can be checked against each other.
//
//   Forward PBR           GGX with height-correlated Smith visibility and Schlick Fresnel, a sun shadow map with PCF,
//                         image-based lighting with multiple scattering compensation; alpha mask and blend
//   Forward PBR (HLSL)    the same with its shaders in HLSL through DXC: the two languages side by side
//   Forward Blinn-Phong   Lambert diffuse and a Blinn-Phong highlight
//   Deferred + SSAO       a G-buffer, SSAO, a blur and the lighting as compute passes, blended objects forward on top
//   Hybrid RT shadows + AO  the same G-buffer with the sun's shadows and ambient occlusion traced by ray queries
//   Path-traced reference   hardware ray tracing with accumulation: the ground truth for the others
//   Mesh shaders (meshlets) Forward PBR through task and mesh shaders, culling meshlets by frustum and normal cone
//   Sky and environment   the background alone
//
// Add your own: derive from rimgui::rt::vk::Technique, record into frame.cb, write its shaders in shaders/ (include
// "rimgui_vk_scene.glsl", "rimgui_vk_lighting.glsl", "rimgui_vk_material.glsl"), and lab.add() it in main().
#include <rimgui/rt/vk/lab.h>
#include <rimgui/rt/vk/meshlets.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>

using namespace rimgui::rt;
using namespace rimgui::rt::vk;

namespace {

uint32_t groups(int n)
{
	return uint32_t((n + 7) / 8);
}

// A scene pass's pipeline into the frame's output: the mesh vertex shader and `fragment`, opaque or alpha-blended.
GraphicsDesc output_pass(const Frame &frame, const char *fragment, bool blended)
{
	GraphicsDesc d;
	d.vertex = "mesh.vert";
	d.fragment = fragment;
	d.colors = frame.output->color_formats();
	d.depth = frame.output->depth_format();
	if (blended) {
		d.blend = Blend::Alpha;
		d.depth_write = false;
	}
	return d;
}

// ---- Forward PBR with a shadow map ---------------------------------------------------------------------------------

class ForwardPbr : public Technique {
public:
	// pbr_constants.glsl
	struct Constants {
		DrawConstants draw;
		mat4 light_view_projection;
		uint32_t shadow_map = UINT32_MAX;
		int32_t view_mode = 0, use_shadows = 0;
		float shadow_bias = 0;
		int32_t pcf_radius = 0;
	};

	std::string name() const override { return "Forward PBR"; }
	std::string description() const override
	{
		return "GGX with height-correlated Smith visibility and Schlick Fresnel; a sun shadow map with PCF; image-based "
			   "lighting with multiple scattering compensation.";
	}
	void settings() override
	{
		ImGui::Combo("Show", &view_, "Final\0Sun, diffuse\0Sun, specular\0Shadow\0Normals\0Ambient only\0");
		ImGui::Checkbox("Shadows", &shadows_);
		ImGui::SliderInt("Shadow map", &shadow_size_, 256, 4096, "%d px", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Shadow bias", &bias_, 0.0001f, 0.02f, "%.4f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderInt("PCF radius", &pcf_, 0, 3);
	}
	void serialize(Settings &s) override
	{
		s.value("view", view_);
		s.value("shadows", shadows_);
		s.value("shadow_size", shadow_size_);
		s.value("bias", bias_);
		s.value("pcf", pcf_);
		shadow_size_ = std::clamp(shadow_size_, 256, 4096);
	}
	void render(Frame &frame) override
	{
		VkCommandBuffer cb = frame.cb;
		const Constants c = prepare_constants(frame);
		auto timed = frame.scope("Forward");
		Pipeline &opaque = frame.shaders->graphics("pbr", output_pass(frame, "pbr.frag", false));
		Pipeline &blended = frame.shaders->graphics("pbr blended", output_pass(frame, "pbr.frag", true));
		frame.begin_output();
		frame.scene->draw(cb, opaque, DrawFilter::Opaque, frame.camera_position, c);
		frame.draw_sky();
		// Blended surfaces last, over the sky, back to front.
		frame.scene->draw(cb, blended, DrawFilter::Transparent, frame.camera_position, c);
		frame.end_output();
	}

protected:
	// The settings as push constants, and the shadow map drawn when shadows are on.
	Constants prepare_constants(Frame &frame)
	{
		VkCommandBuffer cb = frame.cb;
		Constants c;
		c.draw = frame.constants();
		c.view_mode = view_;
		c.use_shadows = shadows_ ? 1 : 0;
		c.shadow_bias = bias_;
		c.pcf_radius = pcf_;
		if (shadows_) {
			auto timed = frame.scope("Shadow map");
			shadow_map_.resize(uint32_t(shadow_size_), uint32_t(shadow_size_));
			vec3 centre;
			float radius = 1;
			frame.scene->bounds(centre, radius);
			const vec3 to_sun = frame.scene->sun.direction();
			const vec3 up = std::fabs(to_sun.y) > 0.99f ? vec3{0, 0, 1} : vec3{0, 1, 0};
			const mat4 view = look_at(centre + to_sun * (radius * 2), centre, up);
			c.light_view_projection = orthographic(-radius, radius, -radius, radius, radius * 0.5f, radius * 3.5f) * view;
			GraphicsDesc d;
			d.vertex = "shadow.vert";
			d.fragment = "shadow.frag";
			d.depth = shadow_map_.depth_format();
			d.cull = VK_CULL_MODE_NONE; // One-sided surfaces like the floor must cast from their backs too.
			d.depth_bias_constant = 2.0f;
			d.depth_bias_slope = 1.5f;
			Pipeline &depth = frame.shaders->graphics("shadow", d);
			shadow_map_.begin(cb);
			struct {
				DrawConstants draw;
				mat4 light_view_projection;
			} sc{frame.constants(), c.light_view_projection};
			frame.scene->draw(cb, depth, DrawFilter::Opaque, frame.camera_position, sc);
			shadow_map_.end(cb);
			barrier(cb);
			c.shadow_map = shadow_map_.depth()->sampled_index();
			frame.expose_depth("Shadow map", shadow_map_);
		}
		return c;
	}

	RenderTarget shadow_map_{{}, VK_FORMAT_D32_SFLOAT, "Shadow map"};
	int view_ = 0, shadow_size_ = 2048, pcf_ = 1;
	bool shadows_ = true;
	float bias_ = 0.0015f;
};

// ---- Forward PBR in HLSL -------------------------------------------------------------------------------------------

class ForwardPbrHlsl : public ForwardPbr {
public:
	std::string name() const override { return "Forward PBR (HLSL)"; }
	std::string description() const override
	{
		return "Forward PBR with its shaders in HLSL (shaders/pbr.hlsl, compiled by DXC) instead of GLSL: the same "
			   "push constants, frame and object data. Compare it with Forward PBR: the difference should be black.";
	}
	void render(Frame &frame) override
	{
		VkCommandBuffer cb = frame.cb;
		const Constants c = prepare_constants(frame);
		auto timed = frame.scope("Forward (HLSL)");
		GraphicsDesc d = output_pass(frame, "pbr.hlsl:PSMain", false);
		d.vertex = "pbr.hlsl:VSMain";
		Pipeline &opaque = frame.shaders->graphics("pbr hlsl", d);
		d.blend = Blend::Alpha;
		d.depth_write = false;
		Pipeline &blended = frame.shaders->graphics("pbr hlsl blended", d);
		frame.begin_output();
		frame.scene->draw(cb, opaque, DrawFilter::Opaque, frame.camera_position, c);
		frame.draw_sky();
		frame.scene->draw(cb, blended, DrawFilter::Transparent, frame.camera_position, c);
		frame.end_output();
	}
};

// ---- Forward Blinn-Phong -------------------------------------------------------------------------------------------

class BlinnPhong : public Technique {
public:
	std::string name() const override { return "Forward Blinn-Phong"; }
	std::string description() const override { return "Lambert diffuse, a Blinn-Phong highlight and the environment's diffuse light."; }
	void settings() override
	{
		ImGui::SliderFloat("Shininess", &shininess_, 1, 512, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Specular", &specular_, 0, 2, "%.2f");
	}
	void serialize(Settings &s) override
	{
		s.value("shininess", shininess_);
		s.value("specular", specular_);
	}
	void render(Frame &frame) override
	{
		auto timed = frame.scope("Forward");
		struct {
			DrawConstants draw;
			float shininess, specular;
		} c{frame.constants(), shininess_, specular_};
		Pipeline &opaque = frame.shaders->graphics("blinn_phong", output_pass(frame, "blinn_phong.frag", false));
		Pipeline &blended = frame.shaders->graphics("blinn_phong blended", output_pass(frame, "blinn_phong.frag", true));
		frame.begin_output();
		frame.scene->draw(frame.cb, opaque, DrawFilter::Opaque, frame.camera_position, c);
		frame.draw_sky();
		frame.scene->draw(frame.cb, blended, DrawFilter::Transparent, frame.camera_position, c);
		frame.end_output();
	}

private:
	float shininess_ = 64, specular_ = 0.5f;
};

// ---- What the deferred techniques share ----------------------------------------------------------------------------

// A G-buffer: albedo and roughness; view-space normal and metallic; emissive and occlusion; depth.
class GBuffer {
public:
	void draw(Frame &frame)
	{
		VkCommandBuffer cb = frame.cb;
		target_.resize(uint32_t(frame.width), uint32_t(frame.height));
		auto timed = frame.scope("G-buffer");
		GraphicsDesc d;
		d.vertex = "mesh.vert";
		d.fragment = "gbuffer.frag";
		d.colors = target_.color_formats();
		d.depth = target_.depth_format();
		Pipeline &g = frame.shaders->graphics("gbuffer", d);
		const float clear[4]{0, 0, 0, 0};
		target_.begin(cb, clear);
		frame.scene->draw(cb, g, DrawFilter::Opaque, frame.camera_position, frame.constants());
		target_.end(cb);
		barrier(cb);
		frame.expose("G-buffer albedo (a: roughness)", target_, 0);
		frame.expose("G-buffer view normal (a: metallic)", target_, 1);
		frame.expose("G-buffer emissive (a: occlusion)", target_, 2);
		frame.expose_depth("G-buffer depth", target_);
	}
	uint32_t albedo() const { return target_.color(0)->sampled_index(); }
	uint32_t normal() const { return target_.color(1)->sampled_index(); }
	uint32_t emissive() const { return target_.color(2)->sampled_index(); }
	uint32_t depth() const { return target_.depth()->sampled_index(); }
	vk::Image &depth_image() const { return *target_.depth(); }

private:
	RenderTarget target_{{VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT}, VK_FORMAT_D32_SFLOAT, "G-buffer"};
};

// deferred_light.comp's constants.
struct LightingConstants {
	VkDeviceAddress frame;
	uint32_t albedo, normal, emissive, depth, ao, target;
	int32_t width, height;
	int32_t use_ao, ao_only;
	uint32_t shadow = 0;
	int32_t use_shadow = 0;
};

// The lighting pass into the output, then the G-buffer's depth under it, the sky, and blended objects forward on top.
void light_and_finish(Frame &frame, const GBuffer &gbuffer, LightingConstants c, bool show_sky_and_blended)
{
	VkCommandBuffer cb = frame.cb;
	const int w = frame.width, h = frame.height;
	{
		auto timed = frame.scope("Lighting");
		Pipeline &l = frame.shaders->compute("deferred_light", "deferred_light.comp");
		if (l.ready()) {
			c.frame = frame.frame_data;
			c.albedo = gbuffer.albedo();
			c.normal = gbuffer.normal();
			c.emissive = gbuffer.emissive();
			c.depth = gbuffer.depth();
			c.target = frame.output->color()->storage_index();
			c.width = w;
			c.height = h;
			l.bind(cb, c);
			vkCmdDispatch(cb, groups(w), groups(h), 1);
		}
		// The G-buffer's depth into the output, so the sky is drawn only where nothing was, and blended objects are
		// hidden behind opaque ones.
		VkImageCopy copy{};
		copy.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
		copy.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
		copy.extent = {uint32_t(w), uint32_t(h), 1};
		vkCmdCopyImage(cb, gbuffer.depth_image().image(), VK_IMAGE_LAYOUT_GENERAL, frame.output->depth()->image(), VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
		barrier(cb);
	}
	if (show_sky_and_blended) {
		auto timed = frame.scope("Sky and blended (forward)");
		// Blended objects cannot go into a G-buffer: forward, on top, with Forward PBR's shader (no shadow map).
		ForwardPbr::Constants forward_constants;
		forward_constants.draw = frame.constants();
		Pipeline &forward = frame.shaders->graphics("pbr blended", output_pass(frame, "pbr.frag", true));
		frame.begin_output();
		frame.draw_sky();
		frame.scene->draw(cb, forward, DrawFilter::Transparent, frame.camera_position, forward_constants);
		frame.end_output();
	}
}

// ---- Deferred with SSAO --------------------------------------------------------------------------------------------

class DeferredSsao : public Technique {
public:
	std::string name() const override { return "Deferred + SSAO"; }
	std::string description() const override
	{
		return "G-buffer, normal-oriented hemisphere SSAO with a range check, a blur and GGX lighting with image-based "
			   "light, all three as compute passes; blended objects drawn forward on top.";
	}
	void settings() override
	{
		ImGui::Checkbox("Ambient occlusion", &ao_on_);
		ImGui::SliderFloat("Radius", &radius_, 0.05f, 2, "%.2f m");
		ImGui::SliderFloat("Bias", &bias_, 0, 0.1f, "%.3f");
		ImGui::SliderInt("Samples", &samples_, 4, 64);
		ImGui::SliderFloat("Strength", &strength_, 0.2f, 4, "%.2f");
		ImGui::Checkbox("Blur", &blur_);
		ImGui::Checkbox("Show AO only", &ao_only_);
	}
	void serialize(Settings &s) override
	{
		s.value("ao", ao_on_);
		s.value("radius", radius_);
		s.value("bias", bias_);
		s.value("samples", samples_);
		s.value("strength", strength_);
		s.value("blur", blur_);
		s.value("ao_only", ao_only_);
		samples_ = std::clamp(samples_, 4, 64);
	}
	void render(Frame &frame) override
	{
		VkCommandBuffer cb = frame.cb;
		const int w = frame.width, h = frame.height;
		ao_.resize(uint32_t(w), uint32_t(h));
		ao_blurred_.resize(uint32_t(w), uint32_t(h));
		gbuffer_.draw(frame);
		if (ao_on_) {
			auto timed = frame.scope("SSAO");
			Pipeline &s = frame.shaders->compute("ssao", "ssao.comp");
			if (s.ready()) {
				struct {
					VkDeviceAddress frame;
					uint32_t depth, normal, target;
					int32_t width, height;
					float radius, bias, strength;
					int32_t samples;
				} c{frame.frame_data, gbuffer_.depth(), gbuffer_.normal(), ao_.color()->storage_index(), w, h, radius_, bias_, strength_, samples_};
				s.bind(cb, c);
				vkCmdDispatch(cb, groups(w), groups(h), 1);
				barrier(cb);
			}
			frame.expose("SSAO", ao_, 0);
		}
		if (ao_on_ && blur_) {
			auto timed = frame.scope("SSAO blur");
			Pipeline &b = frame.shaders->compute("ssao_blur", "ssao_blur.comp");
			if (b.ready()) {
				struct {
					uint32_t source, target;
					int32_t width, height;
				} c{ao_.color()->sampled_index(), ao_blurred_.color()->storage_index(), w, h};
				b.bind(cb, c);
				vkCmdDispatch(cb, groups(w), groups(h), 1);
				barrier(cb);
			}
			frame.expose("SSAO blurred", ao_blurred_, 0);
		}
		LightingConstants c{};
		c.ao = (blur_ ? ao_blurred_ : ao_).color()->sampled_index();
		c.use_ao = ao_on_ ? 1 : 0;
		c.ao_only = ao_on_ && ao_only_ ? 1 : 0;
		light_and_finish(frame, gbuffer_, c, !(ao_on_ && ao_only_));
	}

private:
	GBuffer gbuffer_;
	RenderTarget ao_{{VK_FORMAT_R16_SFLOAT}, VK_FORMAT_UNDEFINED, "SSAO"};
	RenderTarget ao_blurred_{{VK_FORMAT_R16_SFLOAT}, VK_FORMAT_UNDEFINED, "SSAO blurred"};
	bool ao_on_ = true, blur_ = true, ao_only_ = false;
	float radius_ = 0.5f, bias_ = 0.02f, strength_ = 1.4f;
	int samples_ = 16;
};

// ---- Hybrid: a raster G-buffer with ray-traced shadows and AO --------------------------------------------------------

class HybridRt : public Technique {
public:
	std::string name() const override { return "Hybrid RT shadows + AO"; }
	std::string description() const override
	{
		return "The deferred G-buffer, with the sun's shadows and ambient occlusion traced by ray queries instead of a "
			   "shadow map and SSAO: soft shadows from the sun's size, occlusion rays of a set length, averaged over the "
			   "frames while nothing moves. Compare it with Forward PBR and Deferred + SSAO.";
	}
	void settings() override
	{
		if (!supported_) {
			text_unsupported();
		}
		ImGui::Checkbox("Ray-traced shadows", &shadows_);
		ImGui::SliderFloat("Sun size", &sun_degrees_, 0, 5, "%.2f deg");
		ImGui::SetItemTooltip("The sun's angular radius: 0.27 degrees for the real one");
		ImGui::Checkbox("Ray-traced AO", &ao_);
		ImGui::SliderInt("AO rays", &ao_rays_, 1, 16);
		ImGui::SliderFloat("AO radius", &ao_radius_, 0.05f, 4, "%.2f m");
		ImGui::Checkbox("Show AO only", &ao_only_);
	}
	void serialize(Settings &s) override
	{
		s.value("shadows", shadows_);
		s.value("sun_degrees", sun_degrees_);
		s.value("ao", ao_);
		s.value("ao_rays", ao_rays_);
		s.value("ao_radius", ao_radius_);
		s.value("ao_only", ao_only_);
		ao_rays_ = std::clamp(ao_rays_, 1, 16);
	}
	void render(Frame &frame) override
	{
		VkCommandBuffer cb = frame.cb;
		const int w = frame.width, h = frame.height;
		supported_ = rimgui::vulkan::context()->features.ray_query && frame.prepare_ray_tracing();
		gbuffer_.draw(frame);
		shadow_.resize(uint32_t(w), uint32_t(h));
		ao_image_.resize(uint32_t(w), uint32_t(h));
		const bool resized = history_.resize(uint32_t(w), uint32_t(h));
		LightingConstants c{};
		if (supported_ && (shadows_ || ao_)) {
			auto timed = frame.scope("Ray queries: shadows and AO");
			Pipeline &rq = frame.shaders->compute("rt_shadows_ao", "rt_shadows_ao.comp");
			if (rq.ready()) {
				struct {
					VkDeviceAddress frame;
					uint32_t depth, normal, shadow, ao, history;
					int32_t width, height;
					float sun_angle, ao_radius;
					int32_t ao_rays, shadows_on, ao_on, restart;
				} q{frame.frame_data, gbuffer_.depth(), gbuffer_.normal(), shadow_.color()->storage_index(), ao_image_.color()->storage_index(),
					history_.color()->storage_index(), w, h, sun_degrees_ * 3.14159265f / 180, ao_radius_, ao_rays_, shadows_ ? 1 : 0, ao_ ? 1 : 0,
					frame.accumulation == 0 || resized ? 1 : 0};
				rq.bind(cb, q);
				vkCmdDispatch(cb, groups(w), groups(h), 1);
				barrier(cb);
			}
			frame.expose("Ray-traced sun visibility", shadow_, 0);
			frame.expose("Ray-traced AO", ao_image_, 0);
			c.shadow = shadow_.color()->sampled_index();
			c.use_shadow = shadows_ ? 1 : 0;
			c.ao = ao_image_.color()->sampled_index();
			c.use_ao = ao_ ? 1 : 0;
			c.ao_only = ao_ && ao_only_ ? 1 : 0;
		}
		light_and_finish(frame, gbuffer_, c, c.ao_only == 0);
	}
	void statistics() override
	{
		ImGui::TextDisabled("%s", supported_ ? "Ray queries: one shadow ray and the AO rays per pixel a frame" : "No ray queries on this GPU");
	}

private:
	static void text_unsupported() { ImGui::TextColored({1, 0.6f, 0.4f, 1}, "This GPU has no ray queries: shown without ray-traced light."); }
	GBuffer gbuffer_;
	RenderTarget shadow_{{VK_FORMAT_R16_SFLOAT}, VK_FORMAT_UNDEFINED, "RT shadow"};
	RenderTarget ao_image_{{VK_FORMAT_R16_SFLOAT}, VK_FORMAT_UNDEFINED, "RT AO"};
	RenderTarget history_{{VK_FORMAT_R32G32_SFLOAT}, VK_FORMAT_UNDEFINED, "RT history"};
	bool shadows_ = true, ao_ = true, ao_only_ = false, supported_ = true;
	float sun_degrees_ = 0.5f, ao_radius_ = 0.75f;
	int ao_rays_ = 2;
};

// ---- The path-traced reference -------------------------------------------------------------------------------------

class PathTracer : public Technique {
public:
	std::string name() const override { return "Path-traced reference"; }
	std::string description() const override
	{
		return "Hardware ray tracing: paths with next-event estimation to the sun, the sky's and the environment's light "
			   "where they leave the scene, GGX and Lambert sampled by their weights, Russian roulette; summed over the "
			   "frames while nothing changes. The ground truth to compare the raster techniques with.";
	}
	void settings() override
	{
		if (!supported_) {
			ImGui::TextColored({1, 0.6f, 0.4f, 1}, "This GPU has no ray tracing pipelines: nothing is traced.");
		}
		ImGui::SliderInt("Bounces", &bounces_, 1, 16);
		ImGui::SliderInt("Paths a frame", &samples_, 1, 16);
		ImGui::Checkbox("Sample the sun directly", &sun_nee_);
		ImGui::SetItemTooltip("Next-event estimation: a shadow ray to the sun at every bounce. Off: only paths that happen to hit the "
							  "sun's disc see it (far noisier)");
		ImGui::SliderFloat("Clamp", &clamp_, 0, 100, clamp_ > 0 ? "%.0f" : "off");
		ImGui::SetItemTooltip("Each path's radiance clamped to this, against fireflies (biased); 0 leaves it unbiased");
		ImGui::Text("%d paths a pixel so far", accumulated_);
	}
	void serialize(Settings &s) override
	{
		s.value("bounces", bounces_);
		s.value("samples", samples_);
		s.value("sun_nee", sun_nee_);
		s.value("clamp", clamp_);
		bounces_ = std::clamp(bounces_, 1, 16);
		samples_ = std::clamp(samples_, 1, 16);
	}
	void render(Frame &frame) override
	{
		VkCommandBuffer cb = frame.cb;
		supported_ = rimgui::vulkan::context()->features.ray_tracing_pipeline && frame.prepare_ray_tracing();
		if (!supported_) {
			frame.begin_output();
			frame.draw_sky();
			frame.end_output();
			return;
		}
		// A new size: the sum starts over now (the lab sees the change a frame later).
		const bool resized = accumulation_.resize(uint32_t(frame.width), uint32_t(frame.height));
		RayTracingDesc d;
		d.raygen = "pt.rgen";
		d.miss = {"pt.rmiss", "pt_shadow.rmiss"};
		d.hit_groups = {{"pt.rchit", "pt.rahit", ""}};
		d.max_recursion = 1;
		Pipeline &p = frame.shaders->ray_tracing("path tracer", d);
		if (!p.ready()) {
			return;
		}
		auto timed = frame.scope("Path tracing");
		const bool restart = frame.accumulation == 0 || resized;
		accumulated_ = restart ? samples_ : accumulated_ + samples_;
		struct {
			VkDeviceAddress frame;
			uint32_t accumulation, target;
			int32_t max_bounces, sun_nee;
			float clamp_value;
			int32_t samples, restart;
		} c{frame.frame_data, accumulation_.color()->storage_index(), frame.output->color()->storage_index(), bounces_, sun_nee_ ? 1 : 0, clamp_,
			samples_, restart ? 1 : 0};
		p.bind(cb, c);
		p.trace(cb, uint32_t(frame.width), uint32_t(frame.height));
		barrier(cb);
		frame.expose("Path tracer sum (a: paths)", accumulation_, 0);
	}
	void statistics() override
	{
		ImGui::TextDisabled("Path tracer: %d paths a pixel since the last change", accumulated_);
	}

private:
	RenderTarget accumulation_{{VK_FORMAT_R32G32B32A32_SFLOAT}, VK_FORMAT_UNDEFINED, "Path tracer sum"};
	int bounces_ = 6, samples_ = 1, accumulated_ = 0;
	bool sun_nee_ = true, supported_ = true;
	float clamp_ = 0;
};

// ---- Forward PBR through mesh shaders ------------------------------------------------------------------------------

class MeshletPbr : public ForwardPbr {
public:
	// meshlet_constants.glsl: Forward PBR's constants, then the meshlets and the culling.
	struct Constants {
		ForwardPbr::Constants pbr;
		VkDeviceAddress meshlets = 0, meshlet_vertices = 0, meshlet_triangles = 0, counters = 0;
		uint32_t meshlet_count = 0, cull = 0;
		vec4 planes[6];
	};
	static_assert(offsetof(Constants, meshlets) == 104 && sizeof(Constants) == 240, "MeshletPbr::Constants must match meshlet_constants.glsl");

	std::string name() const override { return "Mesh shaders (meshlets)"; }
	std::string description() const override
	{
		return "Forward PBR drawn by task and mesh shaders: every mesh cut into meshlets of at most 64 vertices and 124 "
			   "triangles; a task shader culls whole meshlets outside the view and facing away (normal cones) before the "
			   "mesh shader emits the rest. The same picture as Forward PBR.";
	}
	void settings() override
	{
		if (!supported_) {
			ImGui::TextColored({1, 0.6f, 0.4f, 1}, "This GPU has no mesh shaders: nothing is drawn.");
		}
		ImGui::Checkbox("Frustum culling", &frustum_);
		ImGui::Checkbox("Normal cone culling", &cone_);
		ImGui::SetItemTooltip("Meshlets whose triangles all face away from the camera; not for double-sided materials");
		ImGui::Separator();
		ForwardPbr::settings();
	}
	void serialize(Settings &s) override
	{
		ForwardPbr::serialize(s);
		s.value("frustum_culling", frustum_);
		s.value("cone_culling", cone_);
	}
	void render(Frame &frame) override
	{
		VkCommandBuffer cb = frame.cb;
		supported_ = rimgui::vulkan::context()->features.mesh_shader;
		if (!supported_) {
			frame.begin_output();
			frame.draw_sky();
			frame.end_output();
			return;
		}
		read_counters(cb);
		Constants c;
		c.pbr = prepare_constants(frame);
		c.counters = counters_[slot_]->address();
		frustum_planes(frame.view_projection, c.planes);
		auto timed = frame.scope("Forward (mesh shaders)");
		GraphicsDesc d;
		d.task = "meshlet.task";
		d.mesh = "meshlet.mesh";
		d.fragment = "pbr.frag";
		d.colors = frame.output->color_formats();
		d.depth = frame.output->depth_format();
		Pipeline &opaque = frame.shaders->graphics("meshlet pbr", d);
		d.blend = Blend::Alpha;
		d.depth_write = false;
		Pipeline &blended = frame.shaders->graphics("meshlet pbr blended", d);
		submitted_ = 0;
		frame.begin_output();
		draw(frame, opaque, DrawFilter::Opaque, c);
		frame.draw_sky();
		draw(frame, blended, DrawFilter::Transparent, c);
		frame.end_output();
		cache_.prune();
	}
	void statistics() override
	{
		if (!supported_) {
			ImGui::TextDisabled("No mesh shaders on this GPU");
			return;
		}
		const uint32_t culled = shown_frustum_ + shown_cone_;
		ImGui::TextDisabled("Meshlets: %u in view's objects, %u culled outside the view, %u facing away, %u drawn (%.0f%%)", shown_total_,
			shown_frustum_, shown_cone_, shown_total_ > culled ? shown_total_ - culled : 0,
			shown_total_ ? 100.0 * double(shown_total_ - std::min(culled, shown_total_)) / double(shown_total_) : 0.0);
	}

private:
	void draw(Frame &frame, const Pipeline &pipeline, DrawFilter filter, Constants c)
	{
		if (!pipeline.ready()) {
			return;
		}
		VkCommandBuffer cb = frame.cb;
		const Scene &scene = *frame.scene;
		pipeline.bind(cb, c);
		VkCullModeFlags culling = pipeline.cull();
		for (const Object *o : scene_core::draw_order(scene.objects, filter, frame.camera_position)) {
			const MeshletBuffers &b = cache_.get(o->mesh);
			if (b.count == 0) {
				continue;
			}
			c.pbr.draw.object = scene.object_address(std::size_t(o - scene.objects.data()));
			c.meshlets = b.meshlets->address();
			c.meshlet_vertices = b.vertices->address();
			c.meshlet_triangles = b.triangles->address();
			c.meshlet_count = b.count;
			// The cone test holds under rotation and uniform scale only.
			const mat4 m = o->model();
			const float sx = length(vec3{m.at(0, 0), m.at(1, 0), m.at(2, 0)}), sy = length(vec3{m.at(0, 1), m.at(1, 1), m.at(2, 1)}),
						sz = length(vec3{m.at(0, 2), m.at(1, 2), m.at(2, 2)});
			const bool uniform = std::max({sx, sy, sz}) <= std::min({sx, sy, sz}) * 1.01f;
			c.cull = (frustum_ ? 1u : 0u) | (cone_ && uniform && !o->material.double_sided ? 2u : 0u);
			vkCmdPushConstants(cb, Gpu::get()->pipeline_layout(), VK_SHADER_STAGE_ALL, 0, sizeof c, &c);
			const VkCullModeFlags wanted = o->material.double_sided ? VK_CULL_MODE_NONE : pipeline.cull();
			if (wanted != culling) {
				vkCmdSetCullMode(cb, wanted);
				culling = wanted;
			}
			vkCmdDrawMeshTasksEXT(cb, (b.count + 31) / 32, 1, 1);
			submitted_ += b.count;
		}
		if (culling != pipeline.cull()) {
			vkCmdSetCullMode(cb, pipeline.cull());
		}
	}

	// The counts this frame slot's last frame made (the shell waited for it), then the counters cleared for this one.
	void read_counters(VkCommandBuffer cb)
	{
		const auto &ctx = *rimgui::vulkan::context();
		if (counters_.size() != ctx.frames_in_flight) {
			counters_.clear();
			totals_.assign(ctx.frames_in_flight, 0);
			for (uint32_t i = 0; i < ctx.frames_in_flight; ++i) {
				counters_.push_back(Buffer::create(16, 0, Memory::Readback, "Meshlet cull counters"));
				std::memset(counters_.back()->mapped(), 0, 16);
			}
		}
		totals_[slot_] = submitted_;
		slot_ = ctx.frame_slot;
		const auto *counts = static_cast<const uint32_t *>(counters_[slot_]->mapped());
		shown_frustum_ = counts[0];
		shown_cone_ = counts[1];
		shown_total_ = totals_[slot_];
		vkCmdFillBuffer(cb, counters_[slot_]->buffer(), 0, 16, 0);
		barrier(cb);
	}

	// The view frustum's six planes in world space, facing inwards, from OpenGL's clip space (-w <= x, y, z <= w).
	static void frustum_planes(const mat4 &vp, vec4 planes[6])
	{
		const auto row = [&](int r) { return vec4{vp.at(r, 0), vp.at(r, 1), vp.at(r, 2), vp.at(r, 3)}; };
		const vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
		const auto add = [](vec4 a, vec4 b, float s) { return vec4{a.x + b.x * s, a.y + b.y * s, a.z + b.z * s, a.w + b.w * s}; };
		const vec4 p[6]{add(r3, r0, 1), add(r3, r0, -1), add(r3, r1, 1), add(r3, r1, -1), add(r3, r2, 1), add(r3, r2, -1)};
		for (int i = 0; i < 6; ++i) {
			const float l = std::sqrt(p[i].x * p[i].x + p[i].y * p[i].y + p[i].z * p[i].z);
			planes[i] = l > 0 ? vec4{p[i].x / l, p[i].y / l, p[i].z / l, p[i].w / l} : p[i];
		}
	}

	MeshletCache cache_;
	std::vector<std::shared_ptr<Buffer>> counters_;
	std::vector<uint32_t> totals_;
	uint32_t slot_ = 0, submitted_ = 0;
	uint32_t shown_total_ = 0, shown_frustum_ = 0, shown_cone_ = 0;
	bool frustum_ = true, cone_ = true, supported_ = true;
};

// ---- The background alone ------------------------------------------------------------------------------------------

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
	lab.add(std::make_unique<ForwardPbr>());
	lab.add(std::make_unique<ForwardPbrHlsl>());
	lab.add(std::make_unique<BlinnPhong>());
	lab.add(std::make_unique<DeferredSsao>());
	lab.add(std::make_unique<HybridRt>());
	lab.add(std::make_unique<PathTracer>());
	lab.add(std::make_unique<MeshletPbr>());
	lab.add(std::make_unique<SkyOnly>());
	LabConfig config;
	config.title = "RenderLabVulkan";
	config.shader_folders = {RIMGUI_SHADERS, "shaders"};
	return lab.run(config, argc, argv);
}
