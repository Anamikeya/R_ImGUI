#version 330 core
// Screen-space ambient occlusion: a hemisphere of samples round each pixel's normal, in view space, each tested
// against the depth buffer, with a range check so far-off surfaces do not darken edges.
in vec2 v_uv;
out float o_ao;

uniform sampler2D u_depth;
uniform sampler2D u_normal; // View space.
uniform mat4 u_projection, u_inverse_projection;
uniform float u_radius, u_bias, u_strength;
uniform int u_samples;

vec3 view_position(vec2 uv)
{
    float depth = texture(u_depth, uv).r;
    vec4 p = u_inverse_projection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

// Interleaved gradient noise (Jimenez 2014): a per-pixel angle that a small blur removes.
float ign(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float hash(float n)
{
    return fract(sin(n) * 43758.5453);
}

void main()
{
    float depth = texture(u_depth, v_uv).r;
    if (depth >= 1.0) {
        o_ao = 1.0;
        return;
    }
    vec3 p = view_position(v_uv);
    vec3 n = normalize(texture(u_normal, v_uv).xyz);
    float angle = ign(gl_FragCoord.xy) * 6.2831853;
    vec3 random = vec3(cos(angle), sin(angle), 0.0);
    vec3 t = normalize(random - n * dot(random, n));
    mat3 tbn = mat3(t, cross(n, t), n);
    float occlusion = 0.0;
    for (int i = 0; i < u_samples; ++i) {
        float fi = float(i);
        // A point in the unit hemisphere, more of them near the centre.
        vec3 s = normalize(vec3(hash(fi * 3.1 + 0.1) * 2.0 - 1.0, hash(fi * 7.7 + 0.2) * 2.0 - 1.0, hash(fi * 1.3 + 0.3)));
        float scale = (fi + 1.0) / float(u_samples);
        s *= hash(fi * 5.3 + 0.4) * mix(0.1, 1.0, scale * scale);
        vec3 sample_point = p + tbn * s * u_radius;
        vec4 clip = u_projection * vec4(sample_point, 1.0);
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        float scene_z = view_position(uv).z;
        float in_range = smoothstep(0.0, 1.0, u_radius / max(abs(p.z - scene_z), 1e-4));
        occlusion += (scene_z >= sample_point.z + u_bias ? 1.0 : 0.0) * in_range;
    }
    o_ao = pow(clamp(1.0 - occlusion / float(u_samples), 0.0, 1.0), u_strength);
}
