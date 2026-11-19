#version 330 core
// The shadow map: depth from the sun.
layout(location = 0) in vec3 a_position;
uniform mat4 u_model, u_light_view_projection;
void main()
{
    gl_Position = u_light_view_projection * u_model * vec4(a_position, 1.0);
}
