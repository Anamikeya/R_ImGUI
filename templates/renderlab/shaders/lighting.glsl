// The Scene panel's sun and sky, for including: #include "lighting.glsl"
uniform vec3 u_sun_direction; // Towards the sun.
uniform vec3 u_sun_color;     // Colour times intensity.
uniform vec3 u_sky, u_horizon, u_ground;
uniform float u_ambient;

// What the sky gives from all round a direction: a hemisphere light, from the ground below to the sky above.
vec3 sky_light(vec3 dir)
{
    float up = clamp(dir.y, -1.0, 1.0);
    return up > 0.0 ? mix(u_horizon, u_sky, up) : mix(u_horizon, u_ground, -up);
}
