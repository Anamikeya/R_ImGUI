#pragma once
// Render targets: a framebuffer with colour attachments and a depth texture, all sampleable, resized on demand.
//
//   RenderTarget gbuffer({Format::RGBA16F, Format::RGBA8}, true, "GBuffer");
//   gbuffer.resize(width, height);   // Does nothing when the size is the same.
//   gbuffer.bind();                  // Draw into it: viewport set, every attachment drawn to.
//   program.texture("u_normal", 0, gbuffer.color(0));
#include <rimgui/rt/gl.h>

#include <string>
#include <vector>

namespace rimgui::rt {

enum class Format { RGBA8, RGBA16F, RGBA32F, R8, R16F, R32F, RG16F, RG32F, R11G11B10F };

const char *to_string(Format);

class RenderTarget {
public:
	RenderTarget(std::vector<Format> colors, bool depth, std::string name = {});
	~RenderTarget();
	RenderTarget(const RenderTarget &) = delete;
	RenderTarget &operator=(const RenderTarget &) = delete;

	// Makes the textures for this size; true when they were (re)made.
	bool resize(int width, int height);
	// Binds the framebuffer and sets the viewport to its size.
	void bind() const;
	// Clears colour attachment `index` to `rgba`, and depth to 1 if there is depth.
	void clear(const float rgba[4], bool clear_depth = true) const;

	GLuint framebuffer() const { return fbo_; }
	GLuint color(std::size_t index = 0) const { return index < colors_.size() ? colors_[index] : 0; }
	GLuint depth() const { return depth_; }
	std::size_t color_count() const { return colors_.size(); }
	Format format(std::size_t index) const { return formats_[index]; }
	int width() const { return width_; }
	int height() const { return height_; }
	const std::string &name() const { return name_; }

private:
	void release();
	std::vector<Format> formats_;
	bool has_depth_;
	std::string name_;
	GLuint fbo_ = 0, depth_ = 0;
	std::vector<GLuint> colors_;
	int width_ = 0, height_ = 0;
};

// A texture made with no framebuffer, for compute shaders to write (glBindImageTexture) or for lookup tables.
GLuint make_texture(Format format, int width, int height, GLenum filter = GL_LINEAR);

} // namespace rimgui::rt
