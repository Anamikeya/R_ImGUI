#version 330 core
// The shadow map: depth from the sun. Texture coordinates go on for alpha-masked materials (leaves, fences).
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_model, u_light_view_projection;
out vec2 v_uv;
void main()
{
    v_uv = a_uv;
    gl_Position = u_light_view_projection * u_model * vec4(a_position, 1.0);
}
