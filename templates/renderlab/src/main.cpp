// RenderLab: three techniques to start from, side by side.
//
//   Forward Blinn-Phong   the classic: Lambert diffuse and a Blinn-Phong highlight
//   Forward PBR           GGX microfacets (Walter et al. 2007) with height-correlated Smith visibility (Heitz 2014),
//                         Schlick Fresnel, and a sun shadow map with PCF
//   Deferred + SSAO       a G-buffer, screen-space ambient occlusion (a normal-oriented hemisphere kernel), a blur and
//                         a lighting pass
//
// Add your own: derive from rimgui::rt::Technique, write its shaders in shaders/, and lab.add() it in main(). Each
// technique's settings appear in the Technique panel, its passes in Timings, and what it exposes in Textures.
#include <rimgui/rt/lab.h>

#include <imgui.h>

#include <algorithm>
#include <memory>

using namespace rimgui::rt;

namespace {

// ---- Forward Blinn-Phong -------------------------------------------------------------------------------------------

class BlinnPhong : public Technique {
public:
	std::string name() const override { return "Forward Blinn-Phong"; }
	std::string description() const override { return "Lambert diffuse, a Blinn-Phong highlight and a hemisphere ambient."; }
	void settings() override
	{
		ImGui::SliderFloat("Shininess", &shininess_, 1, 512, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Specular", &specular_, 0, 2, "%.2f");
	}
	void render(Frame &frame) override
	{
		auto timed = frame.scope("Forward");
		Program &p = frame.shaders->graphics("blinn_phong", "mesh.vert", "blinn_phong.frag");
		if (p.ready()) {
			p.use();
			frame.set_camera(p);
			frame.scene->set_lighting(p);
			p.set("u_shininess", shininess_);
			p.set("u_specular", specular_);
			frame.scene->draw(p);
		}
		frame.draw_sky();
	}

private:
	float shininess_ = 64, specular_ = 0.5f;
};

// ---- Forward PBR with a shadow map ---------------------------------------------------------------------------------

class ForwardPbr : public Technique {
public:
	std::string name() const override { return "Forward PBR"; }
	std::string description() const override
	{
		return "GGX with height-correlated Smith visibility and Schlick Fresnel; a sun shadow map with PCF.";
	}
	void settings() override
	{
		ImGui::Combo("Show", &view_, "Final\0Diffuse only\0Specular only\0Shadow\0Normals\0");
		ImGui::Checkbox("Shadows", &shadows_);
		ImGui::SliderInt("Shadow map", &shadow_size_, 256, 4096, "%d px", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Shadow bias", &bias_, 0.0001f, 0.02f, "%.4f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderInt("PCF radius", &pcf_, 0, 3);
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
				glDisable(GL_CULL_FACE); // The floor is one-sided: its back must cast too.
				frame.scene->draw(depth);
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
				frame.scene->set_lighting(p);
				p.set("u_view_mode", view_);
				p.set("u_use_shadows", shadows_ ? 1 : 0);
				p.set("u_light_view_projection", light_view_projection);
				p.set("u_shadow_bias", bias_);
				p.set("u_pcf_radius", pcf_);
				p.texture("u_shadow_map", 0, shadow_map_.depth());
				frame.scene->draw(p);
			}
		}
		frame.draw_sky();
	}

private:
	RenderTarget shadow_map_{{}, true, "Shadow map"};
	int view_ = 0, shadow_size_ = 2048, pcf_ = 1;
	bool shadows_ = true;
	float bias_ = 0.0015f;
};

// ---- Deferred with SSAO --------------------------------------------------------------------------------------------

class DeferredSsao : public Technique {
public:
	std::string name() const override { return "Deferred + SSAO"; }
	std::string description() const override
	{
		return "G-buffer, normal-oriented hemisphere SSAO with a range check, a blur, and GGX lighting.";
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
			gbuffer_.clear(clear);
			glEnable(GL_DEPTH_TEST);
			if (g.ready()) {
				g.use();
				frame.set_camera(g);
				frame.scene->draw(g);
			}
			glDisable(GL_DEPTH_TEST);
		}
		frame.expose("G-buffer albedo (a: roughness)", gbuffer_, 0);
		frame.expose("G-buffer view normal (a: metallic)", gbuffer_, 1);
		frame.expose("G-buffer emissive", gbuffer_, 2);
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
		if (ao_on_ && blur_) {
			auto timed = frame.scope("SSAO blur");
			Program &b = frame.shaders->graphics("ssao_blur", "rimgui_fullscreen.vert", "ssao_blur.frag");
			ao_blurred_.bind();
			if (b.ready()) {
				b.use();
				b.texture("u_ao", 0, ao_.color());
				draw_fullscreen_triangle();
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
				frame.scene->set_lighting(l);
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
		// The G-buffer's depth into the output, so the sky is drawn only where nothing was.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer_.framebuffer());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frame.output->framebuffer());
		glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		frame.output->bind();
		if (!(ao_on_ && ao_only_)) {
			frame.draw_sky();
		}
	}

private:
	RenderTarget gbuffer_{{Format::RGBA8, Format::RGBA16F, Format::R11G11B10F}, true, "G-buffer"};
	RenderTarget ao_{{Format::R16F}, false, "SSAO"};
	RenderTarget ao_blurred_{{Format::R16F}, false, "SSAO blurred"};
	bool ao_on_ = true, blur_ = true, ao_only_ = false;
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
