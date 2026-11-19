#version 330 core
// One texture as data: a channel, mapped from [u_min, u_max] to black and white. Depth is turned into metres first.
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_texture;
uniform int u_channel; // 0 RGB, 1 R, 2 G, 3 B, 4 A
uniform float u_min, u_max;
uniform int u_depth;
uniform int u_channels; // 1: a one-channel texture, shown grey
uniform float u_near, u_far;

void main()
{
    vec4 t = texture(u_texture, v_uv);
    if (u_depth == 1) {
        float z = t.r * 2.0 - 1.0;
        t = vec4(2.0 * u_near * u_far / (u_far + u_near - z * (u_far - u_near)));
    }
    if (u_channels == 1) t = vec4(t.r, t.r, t.r, 1.0);
    vec3 c = u_channel == 0 ? t.rgb : u_channel == 1 ? t.rrr : u_channel == 2 ? t.ggg : u_channel == 3 ? t.bbb : t.aaa;
    c = clamp((c - u_min) / max(u_max - u_min, 1e-6), 0.0, 1.0);
    o_color = vec4(c, 1.0);
}
