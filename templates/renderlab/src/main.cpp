// RenderLab: three techniques to start from, side by side.
//
//   Forward PBR           GGX microfacets (Walter et al. 2007) with height-correlated Smith visibility (Heitz 2014),
//                         Schlick Fresnel, a sun shadow map with PCF, and image-based lighting with multiple
//                         scattering compensation (Fdez-Aguera 2019); textured glTF materials, alpha mask and blend
//   Forward Blinn-Phong   the classic: Lambert diffuse and a Blinn-Phong highlight
//   Deferred + SSAO       a G-buffer, screen-space ambient occlusion (a normal-oriented hemisphere kernel), a blur in a
//                         compute shader or a fragment shader, a lighting pass, and blended objects drawn forward on top
//
// Add your own: derive from rimgui::rt::Technique, write its shaders in shaders/ (the kit's rimgui_material.glsl,
// rimgui_lighting.glsl and rimgui_brdf.glsl can be included), and lab.add() it in main(). Each technique's settings
// appear in the Technique panel and are kept between runs through serialize(), its passes in Timings, and what it
// exposes in Textures.
#include <rimgui/rt/lab.h>

#include <imgui.h>

#include <algorithm>
#include <memory>

using namespace rimgui::rt;

namespace {

// ---- Forward PBR with a shadow map ---------------------------------------------------------------------------------

class ForwardPbr : public Technique {
public:
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
		mat4 light_view_projection;
		if (shadows_) {
			auto timed = frame.scope("Shadow map");
			shadow_map_.resize(shadow_size_, shadow_size_);
			vec3 centre;
			float radius = 1;
			frame.scene->bounds(centre, radius);
			const vec3 to_sun = frame.scene->sun.direction();
			const vec3 up = std::fabs(to_sun.y) > 0.99f ? vec3{0, 0, 1} : vec3{0, 1, 0};
			const mat4 view = look_at(centre + to_sun * (radius * 2), centre, up);
			light_view_projection = orthographic(-radius, radius, -radius, radius, radius * 0.5f, radius * 3.5f) * view;
			Program &depth = frame.shaders->graphics("shadow", "shadow.vert", "shadow.frag");
			shadow_map_.bind();
			const float none[4]{0, 0, 0, 0};
			shadow_map_.clear(none);
			if (depth.ready()) {
				depth.use();
				depth.set("u_light_view_projection", light_view_projection);
				glEnable(GL_POLYGON_OFFSET_FILL);
				glPolygonOffset(1.5f, 2.0f);
				glDisable(GL_CULL_FACE); // One-sided surfaces like the floor must cast from their backs too.
				frame.scene->draw(depth, DrawFilter::Opaque);
				glDisable(GL_POLYGON_OFFSET_FILL);
				glEnable(GL_CULL_FACE);
			}
			frame.expose_depth("Shadow map", shadow_map_);
			frame.output->bind();
		}
		{
			auto timed = frame.scope("Forward");
			Program &p = frame.shaders->graphics("pbr", "mesh.vert", "pbr.frag");
			if (p.ready()) {
				p.use();
				frame.set_camera(p);
				frame.set_lighting(p);
				p.set("u_view_mode", view_);
				p.set("u_use_shadows", shadows_ ? 1 : 0);
				p.set("u_light_view_projection", light_view_projection);
				p.set("u_shadow_bias", bias_);
				p.set("u_pcf_radius", pcf_);
				p.texture("u_shadow_map", 0, shadow_map_.depth());
				frame.scene->draw(p, DrawFilter::Opaque);
				frame.draw_sky();
				// Blended surfaces last, over the sky, back to front.
				p.use();
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
				frame.scene->draw(p, DrawFilter::Transparent, frame.camera_position);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);
			} else {
				frame.draw_sky();
			}
		}
	}

private:
	RenderTarget shadow_map_{{}, true, "Shadow map"};
	int view_ = 0, shadow_size_ = 2048, pcf_ = 1;
	bool shadows_ = true;
	float bias_ = 0.0015f;
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
		Program &p = frame.shaders->graphics("blinn_phong", "mesh.vert", "blinn_phong.frag");
		if (p.ready()) {
			p.use();
			frame.set_camera(p);
			frame.set_lighting(p);
			p.set("u_shininess", shininess_);
			p.set("u_specular", specular_);
			frame.scene->draw(p, DrawFilter::Opaque);
		}
		frame.draw_sky();
		if (p.ready()) {
			// Blended surfaces last, over the sky, back to front.
			p.use();
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
			frame.scene->draw(p, DrawFilter::Transparent, frame.camera_position);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
		}
	}

private:
	float shininess_ = 64, specular_ = 0.5f;
};

// ---- Deferred with SSAO --------------------------------------------------------------------------------------------

class DeferredSsao : public Technique {
public:
	std::string name() const override { return "Deferred + SSAO"; }
	std::string description() const override
	{
		return "G-buffer, normal-oriented hemisphere SSAO with a range check, a blur (compute or fragment), GGX lighting "
			   "with image-based light; blended objects drawn forward on top.";
	}
	void settings() override
	{
		ImGui::Checkbox("Ambient occlusion", &ao_on_);
		ImGui::SliderFloat("Radius", &radius_, 0.05f, 2, "%.2f m");
		ImGui::SliderFloat("Bias", &bias_, 0, 0.1f, "%.3f");
		ImGui::SliderInt("Samples", &samples_, 4, 64);
		ImGui::SliderFloat("Strength", &strength_, 0.2f, 4, "%.2f");
		ImGui::Checkbox("Blur", &blur_);
		ImGui::BeginDisabled(!has_gl43());
		ImGui::Checkbox("Blur in a compute shader", &compute_blur_);
		ImGui::EndDisabled();
		if (!has_gl43()) {
			ImGui::SetItemTooltip("Needs OpenGL 4.3");
		}
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
		s.value("compute_blur", compute_blur_);
		s.value("ao_only", ao_only_);
		samples_ = std::clamp(samples_, 4, 64);
	}
	void render(Frame &frame) override
	{
		const int w = frame.width, h = frame.height;
		gbuffer_.resize(w, h);
		ao_.resize(w, h);
		ao_blurred_.resize(w, h);
		const float clear[4]{0, 0, 0, 0};
		{
			auto timed = frame.scope("G-buffer");
			Program &g = frame.shaders->graphics("gbuffer", "mesh.vert", "gbuffer.frag");
			gbuffer_.bind();
			const float no_occlusion[4]{0, 0, 0, 1};
			gbuffer_.clear(clear);
			glClearBufferfv(GL_COLOR, 2, no_occlusion);
			glEnable(GL_DEPTH_TEST);
			if (g.ready()) {
				g.use();
				frame.set_camera(g);
				frame.scene->draw(g, DrawFilter::Opaque);
			}
			glDisable(GL_DEPTH_TEST);
		}
		frame.expose("G-buffer albedo (a: roughness)", gbuffer_, 0);
		frame.expose("G-buffer view normal (a: metallic)", gbuffer_, 1);
		frame.expose("G-buffer emissive (a: occlusion)", gbuffer_, 2);
		frame.expose_depth("G-buffer depth", gbuffer_);
		if (ao_on_) {
			auto timed = frame.scope("SSAO");
			Program &s = frame.shaders->graphics("ssao", "rimgui_fullscreen.vert", "ssao.frag");
			ao_.bind();
			if (s.ready()) {
				s.use();
				frame.set_camera(s);
				s.texture("u_depth", 0, gbuffer_.depth());
				s.texture("u_normal", 1, gbuffer_.color(1));
				s.set("u_radius", radius_);
				s.set("u_bias", bias_);
				s.set("u_samples", samples_);
				s.set("u_strength", strength_);
				draw_fullscreen_triangle();
			}
			frame.expose("SSAO", ao_, 0);
		}
		const bool compute = compute_blur_ && has_gl43();
		if (ao_on_ && blur_) {
			auto timed = frame.scope(compute ? "SSAO blur (compute)" : "SSAO blur");
			if (compute) {
				Program &b = frame.shaders->compute("ssao_blur_compute", "ssao_blur.comp");
				if (b.ready()) {
					b.use();
					b.texture("u_ao", 0, ao_.color());
					glBindImageTexture(0, ao_blurred_.color(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
					glDispatchCompute(GLuint((w + 7) / 8), GLuint((h + 7) / 8), 1);
					glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
				}
			} else {
				Program &b = frame.shaders->graphics("ssao_blur", "rimgui_fullscreen.vert", "ssao_blur.frag");
				ao_blurred_.bind();
				if (b.ready()) {
					b.use();
					b.texture("u_ao", 0, ao_.color());
					draw_fullscreen_triangle();
				}
			}
			frame.expose("SSAO blurred", ao_blurred_, 0);
		}
		{
			auto timed = frame.scope("Lighting");
			frame.output->bind();
			glDisable(GL_DEPTH_TEST); // A full-screen pass; the G-buffer's depth is copied in below.
			Program &l = frame.shaders->graphics("deferred_light", "rimgui_fullscreen.vert", "deferred_light.frag");
			if (l.ready()) {
				l.use();
				frame.set_camera(l);
				frame.set_lighting(l);
				l.texture("u_albedo", 0, gbuffer_.color(0));
				l.texture("u_normal", 1, gbuffer_.color(1));
				l.texture("u_emissive", 2, gbuffer_.color(2));
				l.texture("u_depth", 3, gbuffer_.depth());
				l.texture("u_ao", 4, blur_ ? ao_blurred_.color() : ao_.color());
				l.set("u_use_ao", ao_on_ ? 1 : 0);
				l.set("u_ao_only", ao_on_ && ao_only_ ? 1 : 0);
				draw_fullscreen_triangle();
			}
		}
		// The G-buffer's depth into the output, so the sky is drawn only where nothing was, and blended objects are
		// hidden behind opaque ones.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer_.framebuffer());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frame.output->framebuffer());
		glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		frame.output->bind();
		if (!(ao_on_ && ao_only_)) {
			frame.draw_sky();
			// Blended objects cannot go into a G-buffer: forward, on top, with the forward PBR shader.
			Program &forward = frame.shaders->graphics("pbr", "mesh.vert", "pbr.frag");
			if (forward.ready()) {
				auto timed = frame.scope("Blended (forward)");
				forward.use();
				frame.set_camera(forward);
				frame.set_lighting(forward);
				forward.set("u_view_mode", 0);
				forward.set("u_use_shadows", 0);
				glEnable(GL_DEPTH_TEST);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
				frame.scene->draw(forward, DrawFilter::Transparent, frame.camera_position);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);
			}
		}
	}

private:
	RenderTarget gbuffer_{{Format::RGBA8, Format::RGBA16F, Format::RGBA16F}, true, "G-buffer"};
	RenderTarget ao_{{Format::R16F}, false, "SSAO"};
	RenderTarget ao_blurred_{{Format::R16F}, false, "SSAO blurred"};
	bool ao_on_ = true, blur_ = true, compute_blur_ = true, ao_only_ = false;
	float radius_ = 0.5f, bias_ = 0.02f, strength_ = 1.4f;
	int samples_ = 16;
};

} // namespace

int main(int argc, char **argv)
{
	Lab lab;
	lab.add(std::make_unique<ForwardPbr>());
	lab.add(std::make_unique<BlinnPhong>());
	lab.add(std::make_unique<DeferredSsao>());
	LabConfig config;
	config.title = "RenderLab";
	config.shader_folders = {RIMGUI_SHADERS, "shaders"};
	return lab.run(config, argc, argv);
}
