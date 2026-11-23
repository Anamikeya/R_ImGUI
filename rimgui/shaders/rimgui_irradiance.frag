#version 330 core
#include "rimgui_cube.glsl"
// Diffuse light from every direction round a normal: the cosine-weighted integral of the environment over the
// hemisphere, divided by pi so that a white Lambert surface shows it directly.
in vec2 v_uv;
out vec4 o_color;
uniform samplerCube u_environment;
uniform float u_source_size;

void main()
{
    vec3 n = cube_direction(v_uv);
    vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    // Each sample covers about (2 pi / 64) x (pi / 2 / 16) of the sphere: read a mip level that wide, so small bright
    // spots (the sun in the image) are not missed between samples.
    float texel_angle = 4.0 * 3.14159265359 / (6.0 * u_source_size * u_source_size);
    float sample_angle = (6.28318530718 / 64.0) * (1.5707963 / 16.0);
    float level = max(0.5 * log2(sample_angle / texel_angle) + 1.0, 0.0);
    vec3 sum = vec3(0.0);
    float count = 0.0;
    for (int i = 0; i < 64; ++i) {
        float phi = (float(i) + 0.5) / 64.0 * 6.28318530718;
        for (int j = 0; j < 16; ++j) {
            float theta = (float(j) + 0.5) / 16.0 * 1.5707963;
            vec3 local = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 d = t * local.x + b * local.y + n * local.z;
            sum += textureLod(u_environment, d, level).rgb * cos(theta) * sin(theta);
            count += 1.0;
        }
    }
    o_color = vec4(3.14159265359 * sum / count, 1.0);
}
