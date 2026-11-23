// Cubemap faces written one at a time by compute shaders: the direction of a texel of face `face`
// (0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z), by the cubemap conventions OpenGL and Vulkan share. As rimgui_cube.glsl.
#ifndef RIMGUI_VK_CUBE_GLSL
#define RIMGUI_VK_CUBE_GLSL

vec3 cube_direction(int face, vec2 uv)
{
    vec2 p = uv * 2.0 - 1.0;
    if (face == 0) return normalize(vec3(1.0, -p.y, -p.x));
    if (face == 1) return normalize(vec3(-1.0, -p.y, p.x));
    if (face == 2) return normalize(vec3(p.x, 1.0, p.y));
    if (face == 3) return normalize(vec3(p.x, -1.0, -p.y));
    if (face == 4) return normalize(vec3(p.x, -p.y, 1.0));
    return normalize(vec3(-p.x, -p.y, -1.0));
}

// Low-discrepancy points for importance sampling (Hammersley).
vec2 hammersley(uint i, uint count)
{
    uint bits = bitfieldReverse(i);
    return vec2(float(i) / float(count), float(bits) * 2.3283064365386963e-10);
}

// A half vector drawn from the GGX distribution round `n`, for roughness `a` (roughness squared).
vec3 importance_sample_ggx(vec2 xi, vec3 n, float a)
{
    float phi = 6.28318530718 * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
    vec3 h = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    return normalize(t * h.x + b * h.y + n * h.z);
}

// The texel this invocation writes, and its centre in 0..1; false outside the face.
bool face_texel(int size, out ivec2 texel, out vec2 uv)
{
    texel = ivec2(gl_GlobalInvocationID.xy);
    uv = (vec2(texel) + 0.5) / float(size);
    return texel.x < size && texel.y < size;
}
#endif
