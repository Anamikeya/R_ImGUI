// The Vulkan kit's shared bindings (rimgui/rt/vk/gpu.h), for including in every Vulkan GLSL shader:
//   #include "rimgui_vk.glsl"
// Textures are picked by index: sample_2d(index, uv), sample_cube(index, dir, lod); storage images by index:
// u_images[index]. Buffers are read by address (GL_EXT_buffer_reference) from the push constants. Declare those
// layout(push_constant, scalar) so they match the C++ struct (and HLSL, compiled with scalar layout).
#ifndef RIMGUI_VK_GLSL
#define RIMGUI_VK_GLSL
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_shader_image_load_formatted : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 1) uniform textureCube u_cubes[];
layout(set = 0, binding = 2) uniform image2D u_images[];
layout(set = 0, binding = 3) uniform sampler u_samplers[4];

#define SAMPLER_LINEAR_REPEAT 0
#define SAMPLER_LINEAR_CLAMP 1
#define SAMPLER_NEAREST_CLAMP 2
#define SAMPLER_SHADOW 3

// A texture by its index (Image::sampled_index()): clamped (render targets, tables) or repeating (materials).
vec4 sample_2d(uint index, vec2 uv) { return texture(sampler2D(u_textures[nonuniformEXT(index)], u_samplers[SAMPLER_LINEAR_CLAMP]), uv); }
vec4 sample_2d_lod(uint index, vec2 uv, float lod) { return textureLod(sampler2D(u_textures[nonuniformEXT(index)], u_samplers[SAMPLER_LINEAR_CLAMP]), uv, lod); }
vec4 sample_material(uint index, vec2 uv) { return texture(sampler2D(u_textures[nonuniformEXT(index)], u_samplers[SAMPLER_LINEAR_REPEAT]), uv); }
vec4 fetch_2d(uint index, ivec2 texel) { return texelFetch(u_textures[nonuniformEXT(index)], texel, 0); }
ivec2 size_2d(uint index) { return textureSize(u_textures[nonuniformEXT(index)], 0); }
vec4 sample_cube(uint index, vec3 dir, float lod) { return textureLod(samplerCube(u_cubes[nonuniformEXT(index)], u_samplers[SAMPLER_LINEAR_CLAMP]), dir, lod); }
// Depth-comparison lookup in a shadow map: 1 lit, 0 shadowed, filtered between.
float sample_shadow(uint index, vec3 uv_depth) { return texture(sampler2DShadow(u_textures[nonuniformEXT(index)], u_samplers[SAMPLER_SHADOW]), uv_depth); }

// The kit keeps OpenGL's clip space (the same matrices in both labs); Vulkan wants depth in 0..1. Every vertex shader
// writes gl_Position = rimgui_vk_clip(view_projection * world). The depth buffer then holds what OpenGL's would.
vec4 rimgui_vk_clip(vec4 p) { return vec4(p.xy, (p.z + p.w) * 0.5, p.w); }

#ifdef RIMGUI_RAY_TRACING
#extension GL_EXT_ray_tracing : require
layout(set = 0, binding = 4) uniform accelerationStructureEXT u_scene;
#endif
#ifdef RIMGUI_RAY_QUERY
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 4) uniform accelerationStructureEXT u_scene;
#endif
#endif
