#version 330 core
// The full-screen triangle at the far plane: drawn with depth LEQUAL, it shows only where nothing else was drawn.
out vec2 v_uv;
void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 1.0, 1.0);
}
