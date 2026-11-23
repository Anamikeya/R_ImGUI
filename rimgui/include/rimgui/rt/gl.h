#pragma once
// OpenGL for the research kit: core 3.3 through GLFW's bundled glad loader, plus the OpenGL 4.3 calls realtime
// research reaches for (compute shaders, image load/store, storage buffers, immutable textures) when the driver has
// them. Include this before any other OpenGL header.
#include <glad/gl.h>

#include <string>

namespace rimgui::rt {

// Loads the functions for the current context. Safe to call again; false when OpenGL 3.3 is not there.
bool load_gl();
// What the context is: "4.6 NVIDIA ..." and so on.
std::string gl_description();
// Whether the 4.3 calls below can be used.
bool has_gl43();

namespace gl43 {
using DispatchCompute = void(GLAD_API_PTR *)(GLuint x, GLuint y, GLuint z);
using MemoryBarrier = void(GLAD_API_PTR *)(GLbitfield barriers);
using BindImageTexture = void(GLAD_API_PTR *)(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer,
	GLenum access, GLenum format);
using TexStorage2D = void(GLAD_API_PTR *)(GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height);
extern DispatchCompute dispatch_compute;
extern MemoryBarrier memory_barrier;
extern BindImageTexture bind_image_texture;
extern TexStorage2D tex_storage_2d;
} // namespace gl43

} // namespace rimgui::rt

// The 4.3 names, as in the OpenGL headers, so shaders and samples read like the specification.
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_TEXTURE_FETCH_BARRIER_BIT
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#endif
#ifndef GL_TEXTURE_UPDATE_BARRIER_BIT
#define GL_TEXTURE_UPDATE_BARRIER_BIT 0x00000100
#endif
#ifndef GL_ALL_BARRIER_BITS
#define GL_ALL_BARRIER_BITS 0xFFFFFFFF
#endif
#ifndef GL_DEBUG_OUTPUT_SYNCHRONOUS
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#endif
#define glDispatchCompute ::rimgui::rt::gl43::dispatch_compute
#define glMemoryBarrier ::rimgui::rt::gl43::memory_barrier
#define glBindImageTexture ::rimgui::rt::gl43::bind_image_texture
#define glTexStorage2D ::rimgui::rt::gl43::tex_storage_2d
