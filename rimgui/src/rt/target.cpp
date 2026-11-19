#include <rimgui/rt/target.h>

#include <algorithm>

namespace rimgui::rt {

namespace {

struct FormatInfo {
	GLenum internal, format, type;
};

FormatInfo info(Format f)
{
	switch (f) {
	case Format::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
	case Format::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
	case Format::RGBA32F: return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
	case Format::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
	case Format::R16F: return {GL_R16F, GL_RED, GL_HALF_FLOAT};
	case Format::R32F: return {GL_R32F, GL_RED, GL_FLOAT};
	case Format::RG16F: return {GL_RG16F, GL_RG, GL_HALF_FLOAT};
	case Format::RG32F: return {GL_RG32F, GL_RG, GL_FLOAT};
	case Format::R11G11B10F: return {GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT};
	}
	return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

} // namespace

const char *to_string(Format f)
{
	switch (f) {
	case Format::RGBA8: return "RGBA8";
	case Format::RGBA16F: return "RGBA16F";
	case Format::RGBA32F: return "RGBA32F";
	case Format::R8: return "R8";
	case Format::R16F: return "R16F";
	case Format::R32F: return "R32F";
	case Format::RG16F: return "RG16F";
	case Format::RG32F: return "RG32F";
	case Format::R11G11B10F: return "R11G11B10F";
	}
	return "?";
}

GLuint make_texture(Format format, int width, int height, GLenum filter)
{
	const FormatInfo f = info(format);
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GLint(f.internal), std::max(width, 1), std::max(height, 1), 0, f.format, f.type, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(filter));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(filter));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	return texture;
}

RenderTarget::RenderTarget(std::vector<Format> colors, bool depth, std::string name)
	: formats_(std::move(colors)), has_depth_(depth), name_(std::move(name))
{
}

RenderTarget::~RenderTarget()
{
	release();
}

void RenderTarget::release()
{
	if (fbo_) {
		glDeleteFramebuffers(1, &fbo_);
	}
	if (!colors_.empty()) {
		glDeleteTextures(GLsizei(colors_.size()), colors_.data());
	}
	if (depth_) {
		glDeleteTextures(1, &depth_);
	}
	fbo_ = depth_ = 0;
	colors_.clear();
}

bool RenderTarget::resize(int width, int height)
{
	width = std::max(width, 1);
	height = std::max(height, 1);
	if (fbo_ && width == width_ && height == height_) {
		return false;
	}
	release();
	width_ = width;
	height_ = height;
	glGenFramebuffers(1, &fbo_);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	std::vector<GLenum> draw_buffers;
	for (std::size_t i = 0; i < formats_.size(); ++i) {
		const GLuint texture = make_texture(formats_[i], width, height);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GLenum(GL_COLOR_ATTACHMENT0 + i), GL_TEXTURE_2D, texture, 0);
		colors_.push_back(texture);
		draw_buffers.push_back(GLenum(GL_COLOR_ATTACHMENT0 + i));
	}
	if (has_depth_) {
		glGenTextures(1, &depth_);
		glBindTexture(GL_TEXTURE_2D, depth_);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	if (draw_buffers.empty()) {
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
	} else {
		glDrawBuffers(GLsizei(draw_buffers.size()), draw_buffers.data());
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return true;
}

void RenderTarget::bind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glViewport(0, 0, width_, height_);
}

void RenderTarget::clear(const float rgba[4], bool clear_depth) const
{
	for (std::size_t i = 0; i < colors_.size(); ++i) {
		glClearBufferfv(GL_COLOR, GLint(i), rgba);
	}
	if (clear_depth && has_depth_) {
		const float one = 1;
		glDepthMask(GL_TRUE);
		glClearBufferfv(GL_DEPTH, 0, &one);
	}
}

} // namespace rimgui::rt
