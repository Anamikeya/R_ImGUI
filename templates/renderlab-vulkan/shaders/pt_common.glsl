// The path tracer's push constants and payloads (PathTracer in main.cpp), shared by its ray tracing stages.
#extension GL_EXT_ray_tracing : require
#define RIMGUI_RAY_TRACING
#include "rimgui_vk_raytracing.glsl"

layout(push_constant, scalar) uniform Constants {
    FrameData frame;
    uint accumulation;  // RGBA32F storage image: the sum of samples (rgb) and their count (a)
    uint target;        // The output's colour, as a storage image
    int max_bounces;
    int sun_nee;        // Next-event estimation to the sun: a shadow ray at every vertex
    float clamp_value;  // Each sample's radiance clamped to this (0: no clamp), against fireflies
    int samples;        // Paths per pixel per frame
    int restart;        // 1: the accumulation starts over this frame
} pc;

// What a ray found: t < 0 for nothing.
struct Hit {
    float t;
    uint object;
    uint primitive;
    vec2 bary;
};
