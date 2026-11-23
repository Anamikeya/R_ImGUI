// The Vulkan kit's shared bindings (rimgui/rt/vk/gpu.h) for HLSL shaders compiled with DXC:
//   #include "rimgui_vk.hlsl"
// The same set as rimgui_vk.glsl: textures and storage images by index, buffers by address (vk::RawBufferLoad), the
// pass's constants in [[vk::push_constant]].
#ifndef RIMGUI_VK_HLSL
#define RIMGUI_VK_HLSL

[[vk::binding(0, 0)]] Texture2D u_textures[];
[[vk::binding(1, 0)]] TextureCube u_cubes[];
[[vk::binding(2, 0)]] RWTexture2D<float4> u_images[];
[[vk::binding(3, 0)]] SamplerState u_samplers[4];

#define SAMPLER_LINEAR_REPEAT 0
#define SAMPLER_LINEAR_CLAMP 1
#define SAMPLER_NEAREST_CLAMP 2
#define SAMPLER_SHADOW 3

float4 sample_2d(uint index, float2 uv) { return u_textures[NonUniformResourceIndex(index)].Sample(u_samplers[SAMPLER_LINEAR_CLAMP], uv); }
float4 sample_2d_lod(uint index, float2 uv, float lod) { return u_textures[NonUniformResourceIndex(index)].SampleLevel(u_samplers[SAMPLER_LINEAR_CLAMP], uv, lod); }
float4 sample_material(uint index, float2 uv) { return u_textures[NonUniformResourceIndex(index)].Sample(u_samplers[SAMPLER_LINEAR_REPEAT], uv); }
float4 sample_cube(uint index, float3 dir, float lod) { return u_cubes[NonUniformResourceIndex(index)].SampleLevel(u_samplers[SAMPLER_LINEAR_CLAMP], dir, lod); }

// OpenGL's clip space, as the kit's matrices give it, into Vulkan's (depth 0..1): as rimgui_vk_clip in GLSL.
float4 rimgui_vk_clip(float4 p) { return float4(p.xy, (p.z + p.w) * 0.5, p.w); }

#endif
