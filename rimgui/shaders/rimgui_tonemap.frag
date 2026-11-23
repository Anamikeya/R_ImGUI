#version 330 core
// Linear HDR to the display: exposure, a tone curve, then sRGB's gamma. NaN and infinite pixels show magenta.
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_hdr;
uniform float u_exposure;
uniform int u_mode; // 0 ACES (Narkowicz's fit), 1 Reinhard, 2 clamp

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 to_srgb(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main()
{
    vec3 raw = texture(u_hdr, v_uv).rgb;
    // NaN and infinity are bugs in a technique: shown magenta, never hidden as black.
    if (any(isnan(raw)) || any(isinf(raw))) {
        o_color = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }
    vec3 c = max(raw * u_exposure, vec3(0.0));
    if (u_mode == 0) c = aces(c);
    else if (u_mode == 1) c = c / (1.0 + c);
    else c = clamp(c, 0.0, 1.0);
    o_color = vec4(to_srgb(c), 1.0);
}
