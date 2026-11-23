// Cubemap faces drawn one at a time as full-screen triangles: the direction of a texel of face `u_face`
// (0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z), by OpenGL's cubemap conventions.
uniform int u_face;

vec3 cube_direction(vec2 uv)
{
    vec2 p = uv * 2.0 - 1.0;
    if (u_face == 0) return normalize(vec3(1.0, -p.y, -p.x));
    if (u_face == 1) return normalize(vec3(-1.0, -p.y, p.x));
    if (u_face == 2) return normalize(vec3(p.x, 1.0, p.y));
    if (u_face == 3) return normalize(vec3(p.x, -1.0, -p.y));
    if (u_face == 4) return normalize(vec3(p.x, -p.y, 1.0));
    return normalize(vec3(-p.x, -p.y, -1.0));
}

// Low-discrepancy points for importance sampling (Hammersley).
vec2 hammersley(uint i, uint count)
{
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
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
