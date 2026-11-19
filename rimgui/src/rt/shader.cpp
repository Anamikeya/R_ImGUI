#include <rimgui/rt/shader.h>

#include <imgui.h>

#include <fstream>
#include <regex>
#include <sstream>

namespace rimgui::rt {

namespace fs = std::filesystem;

Program::~Program()
{
	if (id_) {
		glDeleteProgram(id_);
	}
}

void Program::use() const
{
	glUseProgram(id_);
}

GLint Program::location(const char *uniform)
{
	if (!id_) {
		return -1;
	}
	const auto found = locations_.find(uniform);
	if (found != locations_.end()) {
		return found->second;
	}
	const GLint at = glGetUniformLocation(id_, uniform);
	locations_.emplace(uniform, at);
	return at;
}

void Program::set(const char *uniform, int value) { glUniform1i(location(uniform), value); }
void Program::set(const char *uniform, float value) { glUniform1f(location(uniform), value); }
void Program::set(const char *uniform, vec2 value) { glUniform2f(location(uniform), value.x, value.y); }
void Program::set(const char *uniform, vec3 value) { glUniform3f(location(uniform), value.x, value.y, value.z); }
void Program::set(const char *uniform, vec4 value) { glUniform4f(location(uniform), value.x, value.y, value.z, value.w); }
void Program::set(const char *uniform, const mat4 &value) { glUniformMatrix4fv(location(uniform), 1, GL_FALSE, value.data()); }

void Program::texture(const char *uniform, int unit, GLuint texture, GLenum target)
{
	glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
	glBindTexture(target, texture);
	glUniform1i(location(uniform), unit);
}

ShaderLibrary::ShaderLibrary(std::vector<fs::path> folders) : folders_(std::move(folders)) {}

void ShaderLibrary::add_folder(fs::path folder)
{
	folders_.push_back(std::move(folder));
}

fs::path ShaderLibrary::find(const std::string &file, const fs::path &beside) const
{
	std::error_code ignored;
	if (!beside.empty() && fs::exists(beside / file, ignored)) {
		return beside / file;
	}
	for (const auto &folder : folders_) {
		if (fs::exists(folder / file, ignored)) {
			return folder / file;
		}
	}
	return {};
}

Program &ShaderLibrary::graphics(const std::string &name, const std::string &vertex_file, const std::string &fragment_file)
{
	return add(name, {{GL_VERTEX_SHADER, vertex_file}, {GL_FRAGMENT_SHADER, fragment_file}});
}

Program &ShaderLibrary::compute(const std::string &name, const std::string &compute_file)
{
	return add(name, {{GL_COMPUTE_SHADER, compute_file}});
}

Program &ShaderLibrary::add(const std::string &name, std::vector<std::pair<GLenum, std::string>> stages)
{
	for (auto &p : programs_) {
		if (p->name_ == name) {
			return *p;
		}
	}
	auto program = std::make_unique<Program>();
	program->name_ = name;
	for (auto &[kind, file] : stages) {
		program->stages_.emplace_back(kind, fs::path(file));
	}
	build(*program);
	programs_.push_back(std::move(program));
	return *programs_.back();
}

namespace {

// A file's text with its #includes put in place. Each piece of text starts with a #line naming the piece's index in
// `files`, so a driver's "0(12)" or "12:0" can be turned back into a file and line.
bool expand(const fs::path &path, const std::function<fs::path(const std::string &, const fs::path &)> &find, std::vector<fs::path> &files,
	std::string &out, std::string &error, int depth)
{
	if (depth > 16) {
		error = path.string() + ": includes nest too deep";
		return false;
	}
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = path.string() + ": cannot be read";
		return false;
	}
	const int index = static_cast<int>(files.size());
	files.push_back(path);
	if (depth > 0) {
		out += "#line 1 " + std::to_string(index) + "\n"; // An included file counts its own lines.
	}
	static const std::regex include_line(R"(^\s*#\s*include\s+"([^"]+)\")");
	std::string line;
	int number = 0;
	bool after_version = false;
	while (std::getline(in, line)) {
		++number;
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		std::smatch match;
		if (std::regex_search(line, match, include_line)) {
			const fs::path included = find(match[1].str(), path.parent_path());
			if (included.empty()) {
				error = path.string() + ":" + std::to_string(number) + ": cannot find \"" + match[1].str() + "\"";
				return false;
			}
			if (!expand(included, find, files, out, error, depth + 1)) {
				return false;
			}
			out += "#line " + std::to_string(number + 1) + " " + std::to_string(index) + "\n";
			continue;
		}
		out += line + "\n";
		// #version must come first, so the file's own #line follows it.
		if (!after_version && line.find("#version") != std::string::npos) {
			after_version = true;
			out += "#line " + std::to_string(number + 1) + " " + std::to_string(index) + "\n";
		}
	}
	return true;
}

// Driver messages ("0(12) : error ...", "ERROR: 0:12: ...") with file indices turned into names.
std::string readable(const std::string &log, const std::vector<fs::path> &files)
{
	static const std::regex nvidia(R"((\d+)\((\d+)\))"), others(R"((\d+):(\d+):)");
	std::istringstream lines(log);
	std::string line, out;
	while (std::getline(lines, line)) {
		std::smatch m;
		const auto name_of = [&](const std::string &index) {
			const auto i = static_cast<std::size_t>(std::stoul(index));
			return i < files.size() ? files[i].filename().string() : "?";
		};
		if (std::regex_search(line, m, nvidia)) {
			line = name_of(m[1].str()) + ":" + m[2].str() + ": " + m.suffix().str();
		} else if (std::regex_search(line, m, others)) {
			line = name_of(m[1].str()) + ":" + m[2].str() + ":" + m.suffix().str();
		}
		if (!line.empty()) {
			out += line + "\n";
		}
	}
	return out;
}

} // namespace

bool ShaderLibrary::build(Program &program)
{
	std::vector<GLuint> shaders;
	std::vector<fs::path> all_files;
	std::string errors;
	const auto finder = [this](const std::string &file, const fs::path &beside) { return find(file, beside); };
	for (const auto &[kind, file] : program.stages_) {
		const fs::path path = find(file.string(), {});
		if (path.empty()) {
			errors += file.string() + ": not found in the shader folders\n";
			continue;
		}
		std::vector<fs::path> files;
		std::string source, error;
		if (!expand(path, finder, files, source, error, 0)) {
			errors += error + "\n";
			all_files.insert(all_files.end(), files.begin(), files.end());
			continue;
		}
		all_files.insert(all_files.end(), files.begin(), files.end());
		if (kind == GL_COMPUTE_SHADER && !has_gl43()) {
			errors += path.filename().string() + ": compute shaders need OpenGL 4.3\n";
			continue;
		}
		const GLuint shader = glCreateShader(kind);
		const char *text = source.c_str();
		glShaderSource(shader, 1, &text, nullptr);
		glCompileShader(shader);
		GLint ok = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			GLint length = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
			std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
			glGetShaderInfoLog(shader, length, nullptr, log.data());
			errors += readable(log.c_str(), files);
			glDeleteShader(shader);
			continue;
		}
		shaders.push_back(shader);
	}
	program.depends_on_ = all_files;
	std::error_code ignored;
	for (const auto &file : all_files) {
		times_[file.string()] = fs::last_write_time(file, ignored);
	}
	GLuint id = 0;
	if (errors.empty()) {
		id = glCreateProgram();
		for (const GLuint s : shaders) {
			glAttachShader(id, s);
		}
		glLinkProgram(id);
		GLint ok = 0;
		glGetProgramiv(id, GL_LINK_STATUS, &ok);
		if (!ok) {
			GLint length = 0;
			glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
			std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
			glGetProgramInfoLog(id, length, nullptr, log.data());
			errors = "linking: " + std::string(log.c_str());
			glDeleteProgram(id);
			id = 0;
		}
	}
	for (const GLuint s : shaders) {
		glDeleteShader(s);
	}
	if (!errors.empty()) {
		program.error_ = errors;
		if (sink_) {
			sink_(false, program.name_ + ": " + errors);
		}
		return false;
	}
	if (program.id_) {
		glDeleteProgram(program.id_);
	}
	const bool was_broken = !program.error_.empty();
	program.id_ = id;
	program.error_.clear();
	program.locations_.clear();
	++program.version_;
	if (sink_ && (program.version_ > 1 || was_broken)) {
		sink_(true, program.name_ + (was_broken ? " builds again" : " reloaded"));
	}
	return true;
}

bool ShaderLibrary::poll()
{
	const double now = ImGui::GetTime();
	if (now - last_poll_ < 0.25) {
		return false;
	}
	last_poll_ = now;
	bool rebuilt = false;
	std::error_code ignored;
	for (auto &program : programs_) {
		bool changed = false; // A program whose files were never found waits for reload_all().
		for (const auto &file : program->depends_on_) {
			const auto time = fs::last_write_time(file, ignored);
			if (!ignored && time != times_[file.string()]) {
				changed = true;
			}
		}
		if (changed && !program->stages_.empty()) {
			build(*program);
			rebuilt = true;
		}
	}
	return rebuilt;
}

void ShaderLibrary::reload_all()
{
	for (auto &program : programs_) {
		build(*program);
	}
}

} // namespace rimgui::rt
