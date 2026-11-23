#version 460
// Forward PBR: the sun through a shadow map, and image-based lighting from the environment (or the gradient sky). The
// OpenGL lab's pbr.frag, reading the frame and the object from the push constants.
#include "pbr_constants.glsl"
#include "rimgui_vk_material.glsl"
#include "rimgui_vk_lighting.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

float shadow(vec3 world, vec3 n)
{
    if (pc.use_shadows == 0) return 1.0;
    // Pushed out along the normal, more where the light grazes the surface.
    float slope = 1.0 - clamp(dot(n, pc.frame.sun_direction), 0.0, 1.0);
    vec4 p = pc.light_view_projection * vec4(world + n * (0.02 * slope), 1.0);
    vec3 uvz = p.xyz / p.w * 0.5 + 0.5;
    if (uvz.z > 1.0 || any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0)))) return 1.0;
    vec2 texel = 1.0 / vec2(size_2d(pc.shadow_map));
    float lit = 0.0, count = 0.0;
    for (int y = -pc.pcf_radius; y <= pc.pcf_radius; ++y) {
        for (int x = -pc.pcf_radius; x <= pc.pcf_radius; ++x) {
            float stored = textureLod(sampler2D(u_textures[nonuniformEXT(pc.shadow_map)], u_samplers[SAMPLER_NEAREST_CLAMP]),
                uvz.xy + vec2(x, y) * texel, 0.0).r;
            lit += uvz.z - pc.shadow_bias <= stored ? 1.0 : 0.0;
            count += 1.0;
        }
    }
    return lit / count;
}

void main()
{
    FrameData f = pc.frame;
    Surface s = surface(pc.object, v_world, v_normal, v_uv);
    vec3 n = s.normal;
    vec3 v = normalize(f.camera_position - v_world);
    vec3 diffuse, specular;
    brdf(n, v, f.sun_direction, s.albedo, s.roughness, s.metallic, diffuse, specular);
    float sh = shadow(v_world, normalize(v_normal));
    vec3 direct_diffuse = diffuse * f.sun_color * sh, direct_specular = specular * f.sun_color * sh;
    vec3 ambient = ambient_light(f, n, v, s.albedo, s.roughness, s.metallic, s.occlusion);

    vec3 colour;
    if (pc.view_mode == 1) colour = direct_diffuse;
    else if (pc.view_mode == 2) colour = direct_specular;
    else if (pc.view_mode == 3) colour = vec3(sh);
    else if (pc.view_mode == 4) colour = n * 0.5 + 0.5;
    else if (pc.view_mode == 5) colour = ambient;
    else colour = direct_diffuse + direct_specular + ambient + s.emissive;
    o_color = vec4(colour, pc.object.alpha_mode == 2 ? s.alpha : 1.0);
}
