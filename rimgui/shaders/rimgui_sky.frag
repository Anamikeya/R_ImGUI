#version 330 core
// A gradient sky with a sun disc, in linear HDR, from the Scene panel's sun and sky colours.
in vec2 v_uv;
out vec4 o_color;
uniform mat4 u_inverse_view_projection;
uniform vec3 u_camera_position;
uniform vec3 u_sky, u_horizon, u_ground;
uniform vec3 u_sun_direction, u_sun_color;

void main()
{
    vec4 far_point = u_inverse_view_projection * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far_point.xyz / far_point.w - u_camera_position);
    vec3 colour = dir.y > 0.0 ? mix(u_horizon, u_sky, pow(dir.y, 0.5)) : mix(u_horizon, u_ground, pow(-dir.y, 0.4));
    float sun = smoothstep(0.9995, 0.9998, dot(dir, u_sun_direction));
    o_color = vec4(colour + u_sun_color * sun * 8.0, 1.0);
}
