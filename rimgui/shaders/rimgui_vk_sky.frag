#version 460
// The background, in linear HDR: the environment image when one is loaded and shown as the sky, else a gradient sky
// with a sun disc. As rimgui_sky.frag.
#include "rimgui_vk_lighting.glsl"
layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main()
{
    vec4 far_point = pc.frame.inverse_view_projection * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far_point.xyz / far_point.w - pc.frame.camera_position);
    o_color = vec4(sky_colour(pc.frame, dir), 1.0);
}
