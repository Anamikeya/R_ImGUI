#version 330 core
// A triangle that covers the screen, from gl_VertexID alone (draw_fullscreen_triangle). v_uv is 0 to 1 over it.
out vec2 v_uv;
void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
