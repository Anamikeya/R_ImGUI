#version 330 core
#include "rimgui_cube.glsl"
// Specular light for one roughness: the environment convolved with GGX by importance sampling, with the source mip
// level chosen from each sample's probability so few samples give no fireflies (Colbert and Krivanek 2007).
in vec2 v_uv;
out vec4 o_color;
uniform samplerCube u_environment;
uniform float u_source_size;
uniform float u_roughness;

void main()
{
    vec3 n = cube_direction(v_uv);
    if (u_roughness < 0.001) {
        o_color = vec4(textureLod(u_environment, n, 0.0).rgb, 1.0);
        return;
    }
    vec3 v = n; // The split sum's assumption: view along the normal.
    float a = u_roughness * u_roughness;
    const uint count = 256u;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    float texel_angle = 4.0 * 3.14159265359 / (6.0 * u_source_size * u_source_size);
    for (uint i = 0u; i < count; ++i) {
        vec3 h = importance_sample_ggx(hammersley(i, count), n, a);
        vec3 l = normalize(2.0 * dot(v, h) * h - v);
        float NoL = dot(n, l);
        if (NoL <= 0.0) continue;
        float NoH = max(dot(n, h), 0.0);
        float a2 = a * a;
        float d = NoH * NoH * (a2 - 1.0) + 1.0;
        float D = a2 / (3.14159265359 * d * d);
        float pdf = D * NoH / (4.0 * max(dot(h, v), 1e-4)) + 1e-4;
        float sample_angle = 1.0 / (float(count) * pdf);
        float level = max(0.5 * log2(sample_angle / texel_angle) + 1.0, 0.0);
        sum += textureLod(u_environment, l, level).rgb * NoL;
        weight += NoL;
    }
    o_color = vec4(sum / max(weight, 1e-4), 1.0);
}
