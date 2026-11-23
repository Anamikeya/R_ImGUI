#include <rimgui/rt/scene.h>

#include <algorithm>
#include <cstring>
#include <functional>

namespace rimgui::rt {

int Material::maps() const
{
	return scene_core::maps(*this);
}

mat4 Object::model() const
{
	return object_transform(position, rotation_degrees, scale, base);
}

namespace {

void draw_object(Program &program, const Object &o)
{
	const mat4 model = o.model();
	const Material &m = o.material;
	program.set("u_model", model);
	program.set("u_normal_matrix", transpose(inverse(model)));
	program.set("u_albedo", m.albedo);
	program.set("u_alpha", m.alpha);
	program.set("u_roughness", m.roughness);
	program.set("u_metallic", m.metallic);
	program.set("u_emissive", m.emissive);
	program.set("u_normal_scale", m.normal_scale);
	program.set("u_occlusion_strength", m.occlusion_strength);
	program.set("u_alpha_mode", int(m.alpha_mode));
	program.set("u_alpha_cutoff", m.alpha_cutoff);
	const int maps = m.maps();
	program.set("u_maps", maps);
	const auto bind = [&](const char *name, int bit, const std::shared_ptr<Texture> &t, int unit) {
		if ((maps & bit) && t && program.location(name) >= 0) {
			program.texture(name, material_texture_unit + unit, t->id());
		}
	};
	bind("u_albedo_map", map_albedo, m.albedo_map, 0);
	bind("u_normal_map", map_normal, m.normal_map, 1);
	bind("u_metal_rough_map", map_metal_rough, m.metal_rough_map, 2);
	bind("u_occlusion_map", map_occlusion, m.occlusion_map, 3);
	bind("u_emissive_map", map_emissive, m.emissive_map, 4);
	if (m.double_sided) {
		glDisable(GL_CULL_FACE);
	}
	o.mesh->draw();
	if (m.double_sided) {
		glEnable(GL_CULL_FACE);
	}
}

} // namespace

void Scene::draw(Program &program, DrawFilter filter, vec3 camera_position) const
{
	GLboolean culling = GL_FALSE;
	glGetBooleanv(GL_CULL_FACE, &culling);
	if (filter != DrawFilter::Transparent) {
		for (const auto &o : objects) {
			if (o.visible && o.mesh && o.material.alpha_mode != AlphaMode::Blend) {
				draw_object(program, o);
			}
		}
	}
	if (filter != DrawFilter::Opaque) {
		std::vector<const Object *> blended;
		for (const auto &o : objects) {
			if (o.visible && o.mesh && o.material.alpha_mode == AlphaMode::Blend) {
				blended.push_back(&o);
			}
		}
		std::sort(blended.begin(), blended.end(), [&](const Object *a, const Object *b) {
			return length(a->position - camera_position) > length(b->position - camera_position);
		});
		if (!blended.empty() && filter == DrawFilter::All) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
		}
		for (const Object *o : blended) {
			draw_object(program, *o);
		}
		if (!blended.empty() && filter == DrawFilter::All) {
			glDisable(GL_BLEND);
			glDepthMask(GL_TRUE);
		}
	}
	if (culling) {
		glEnable(GL_CULL_FACE);
	} else {
		glDisable(GL_CULL_FACE);
	}
	glActiveTexture(GL_TEXTURE0);
}

void Scene::bounds(vec3 &centre, float &radius) const
{
	scene_core::bounds(objects, centre, radius);
}

void Scene::set_lighting(Program &program) const
{
	program.set("u_sun_direction", sun.direction());
	program.set("u_sun_color", sun.color * sun.intensity);
	program.set("u_sky", sun.sky);
	program.set("u_horizon", sun.horizon);
	program.set("u_ground", sun.ground);
	program.set("u_ambient", sun.ambient);
}

std::size_t Scene::fingerprint() const
{
	return scene_core::fingerprint(objects, sun);
}

} // namespace rimgui::rt
