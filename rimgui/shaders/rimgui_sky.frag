#version 330 core
#include "rimgui_lighting.glsl"
// The background, in linear HDR: the environment image when one is loaded and shown as the sky, else a gradient sky
// with a sun disc from the Scene panel's sun and sky colours.
in vec2 v_uv;
out vec4 o_color;
uniform mat4 u_inverse_view_projection;
uniform vec3 u_camera_position;
uniform int u_environment_as_sky;

void main()
{
    vec4 far_point = u_inverse_view_projection * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far_point.xyz / far_point.w - u_camera_position);
    if (u_has_environment == 1 && u_environment_as_sky == 1) {
        o_color = vec4(textureLod(u_environment, environment_direction(dir), 0.0).rgb * u_environment_intensity, 1.0);
        return;
    }
    vec3 colour = dir.y > 0.0 ? mix(u_horizon, u_sky, pow(dir.y, 0.5)) : mix(u_horizon, u_ground, pow(-dir.y, 0.4));
    float sun = smoothstep(0.9995, 0.9998, dot(dir, u_sun_direction));
    o_color = vec4(colour + u_sun_color * sun * 8.0, 1.0);
}
