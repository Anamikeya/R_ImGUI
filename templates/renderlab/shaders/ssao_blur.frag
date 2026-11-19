#version 330 core
// A 4x4 box over the SSAO, enough to hide its per-pixel noise.
in vec2 v_uv;
out float o_ao;
uniform sampler2D u_ao;
void main()
{
    vec2 texel = 1.0 / vec2(textureSize(u_ao, 0));
    float sum = 0.0;
    for (int y = -2; y < 2; ++y) {
        for (int x = -2; x < 2; ++x) {
            sum += texture(u_ao, v_uv + (vec2(x, y) + 0.5) * texel).r;
        }
    }
    o_ao = sum / 16.0;
}
