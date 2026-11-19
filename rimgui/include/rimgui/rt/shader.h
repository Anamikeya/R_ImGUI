#pragma once
// GLSL programs from files that reload when saved.
//
//   ShaderLibrary shaders({"D:/MyLab/shaders"});
//   Program &pbr = shaders.graphics("pbr", "mesh.vert", "pbr.frag");
//   ...each frame: shaders.poll();  pbr.use();  pbr.set("u_roughness", 0.4f);
//
// A file may `#include "brdf.glsl"` (found beside it, then in the library's folders); editing an included file
// reloads every program that uses it. A program that fails to compile keeps its last good version, so the picture
// stays up while you fix the shader; the error is reported with the file and line it is on.
#include <rimgui/rt/gl.h>
#include <rimgui/rt/math.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rimgui::rt {

class Program {
public:
	Program() = default;
	~Program();
	Program(const Program &) = delete;
	Program &operator=(const Program &) = delete;

	// False until it has compiled once.
	bool ready() const { return id_ != 0; }
	GLuint id() const { return id_; }
	const std::string &name() const { return name_; }
	// The latest compile or link error, with file names and lines; empty when the current files built.
	const std::string &error() const { return error_; }
	// How many times it has been built from changed files.
	int version() const { return version_; }

	void use() const;
	GLint location(const char *uniform);
	void set(const char *uniform, int value);
	void set(const char *uniform, float value);
	void set(const char *uniform, vec2 value);
	void set(const char *uniform, vec3 value);
	void set(const char *uniform, vec4 value);
	void set(const char *uniform, const mat4 &value);
	// Binds `texture` to texture unit `unit` and points the sampler uniform at it.
	void texture(const char *uniform, int unit, GLuint texture, GLenum target = GL_TEXTURE_2D);

private:
	friend class ShaderLibrary;
	std::string name_;
	std::vector<std::pair<GLenum, std::filesystem::path>> stages_;
	std::vector<std::filesystem::path> depends_on_; // Every file read, includes too.
	GLuint id_ = 0;
	std::string error_;
	int version_ = 0;
	std::unordered_map<std::string, GLint> locations_;
};

class ShaderLibrary {
public:
	// Folders searched for shader files, in order.
	explicit ShaderLibrary(std::vector<std::filesystem::path> folders = {});
	void add_folder(std::filesystem::path folder);

	// A vertex and fragment program; built now, and again whenever one of its files changes. The same name returns
	// the same program.
	Program &graphics(const std::string &name, const std::string &vertex_file, const std::string &fragment_file);
	// A compute program (OpenGL 4.3).
	Program &compute(const std::string &name, const std::string &compute_file);

	// Checks the files' times (a few times a second at most) and rebuilds what changed. True when anything rebuilt.
	bool poll();
	// Rebuilds every program now.
	void reload_all();
	// Where messages go: a rebuild that worked, or one that failed with its errors.
	void on_message(std::function<void(bool ok, const std::string &text)> sink) { sink_ = std::move(sink); }

	const std::vector<std::unique_ptr<Program>> &programs() const { return programs_; }

private:
	Program &add(const std::string &name, std::vector<std::pair<GLenum, std::string>> stages);
	bool build(Program &program);
	std::filesystem::path find(const std::string &file, const std::filesystem::path &beside) const;

	std::vector<std::filesystem::path> folders_;
	std::vector<std::unique_ptr<Program>> programs_;
	std::unordered_map<std::string, std::filesystem::file_time_type> times_;
	double last_poll_ = 0;
	std::function<void(bool, const std::string &)> sink_;
};

} // namespace rimgui::rt
