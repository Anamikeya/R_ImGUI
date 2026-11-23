#version 330 core
#include "rimgui_cube.glsl"
// The split sum's BRDF half (Karis 2013): for NoV (u) and roughness (v), the scale and bias to apply to F0 under
// uniform white light, with height-correlated Smith visibility to match rimgui_brdf.glsl.
in vec2 v_uv;
out vec2 o_scale_bias;

float V_SmithGGXCorrelated_lut(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float ggx_v = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggx_l = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggx_v + ggx_l, 1e-6);
}

void main()
{
    float NoV = max(v_uv.x, 1e-3);
    float roughness = v_uv.y;
    float a = max(roughness * roughness, 1e-3);
    vec3 v = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    vec3 n = vec3(0.0, 0.0, 1.0);
    const uint count = 512u;
    float scale = 0.0, bias = 0.0;
    for (uint i = 0u; i < count; ++i) {
        vec3 h = importance_sample_ggx(hammersley(i, count), n, a);
        vec3 l = normalize(2.0 * dot(v, h) * h - v);
        float NoL = clamp(l.z, 0.0, 1.0);
        float NoH = clamp(h.z, 0.0, 1.0);
        float VoH = clamp(dot(v, h), 0.0, 1.0);
        if (NoL > 0.0) {
            // Weight = BRDF * NoL / pdf, with pdf = D * NoH / (4 VoH): D cancels.
            float vis = V_SmithGGXCorrelated_lut(NoV, NoL, a) * 4.0 * NoL * VoH / max(NoH, 1e-4);
            float fc = pow(1.0 - VoH, 5.0);
            scale += (1.0 - fc) * vis;
            bias += fc * vis;
        }
    }
    o_scale_bias = vec2(scale, bias) / float(count);
}
