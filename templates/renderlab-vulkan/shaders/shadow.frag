#version 460
// Depth only; masked materials cut holes in their shadows as they do in themselves.
#include "rimgui_vk_scene.glsl"
layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; mat4 light_view_projection; } pc;
layout(location = 0) in vec2 v_uv;
void main()
{
    ObjectData o = pc.object;
    if (o.alpha_mode == 1) {
        float alpha = o.alpha * ((o.maps & 1) != 0 ? sample_material(o.albedo_map, v_uv).a : 1.0);
        if (alpha < o.alpha_cutoff) discard;
    }
}
