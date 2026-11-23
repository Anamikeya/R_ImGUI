#include <rimgui/rt/settings.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rimgui::rt {

namespace {

std::string trim(const std::string &s)
{
	const auto first = s.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

std::string number(double v)
{
	char text[64];
	std::snprintf(text, sizeof text, "%.9g", v);
	return text;
}

bool to_double(const std::string &text, double &out)
{
	char *end = nullptr;
	const double v = std::strtod(text.c_str(), &end);
	if (end == text.c_str() || *end != '\0') {
		return false;
	}
	out = v;
	return true;
}

} // namespace

std::string Settings::key(const std::string &name) const
{
	return prefix_.empty() ? name : prefix_ + "." + name;
}

bool Settings::has(const std::string &name) const
{
	return values_.count(key(name)) != 0;
}

void Settings::push(const std::string &prefix)
{
	prefix_ = prefix_.empty() ? prefix : prefix_ + "." + prefix;
}

void Settings::pop()
{
	const auto dot = prefix_.rfind('.');
	prefix_ = dot == std::string::npos ? std::string() : prefix_.substr(0, dot);
}

void Settings::value(const std::string &name, double &v)
{
	if (saving()) {
		values_[key(name)] = number(v);
		return;
	}
	const auto found = values_.find(key(name));
	double read = 0;
	if (found != values_.end() && to_double(found->second, read)) {
		v = read;
	}
}

void Settings::value(const std::string &name, float &v)
{
	double d = v;
	value(name, d);
	v = float(d);
}

void Settings::value(const std::string &name, int &v)
{
	double d = v;
	value(name, d);
	v = int(d);
}

void Settings::value(const std::string &name, bool &v)
{
	int i = v ? 1 : 0;
	value(name, i);
	v = i != 0;
}

void Settings::value(const std::string &name, vec3 &v)
{
	value(name + ".x", v.x);
	value(name + ".y", v.y);
	value(name + ".z", v.z);
}

void Settings::value(const std::string &name, std::string &v)
{
	if (saving()) {
		// One line each: line breaks in a value are dropped.
		std::string flat = v;
		for (char &c : flat) {
			if (c == '\n' || c == '\r') c = ' ';
		}
		values_[key(name)] = flat;
		return;
	}
	const auto found = values_.find(key(name));
	if (found != values_.end()) {
		v = found->second;
	}
}

std::string Settings::text() const
{
	std::string out;
	for (const auto &[k, v] : values_) {
		out += k + " = " + v + "\n";
	}
	return out;
}

void Settings::parse(const std::string &text)
{
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		const auto equals = line.find('=');
		if (equals == std::string::npos || trim(line).rfind('#', 0) == 0) {
			continue;
		}
		const std::string k = trim(line.substr(0, equals));
		if (!k.empty()) {
			values_[k] = trim(line.substr(equals + 1));
		}
	}
}

bool Settings::read_file(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	std::ostringstream text;
	text << in.rdbuf();
	parse(text.str());
	return true;
}

bool Settings::write_file(const std::filesystem::path &path) const
{
	std::filesystem::path temporary = path;
	temporary += ".tmp";
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		if (!out) {
			return false;
		}
		out << "# Written by R_ImGUI's RenderLab: the lab's state when it last ran. Delete this file to start afresh.\n";
		out << text();
		if (!out) {
			return false;
		}
	}
	std::error_code error;
	std::filesystem::rename(temporary, path, error);
	if (error) {
		std::filesystem::remove(temporary, error);
		return false;
	}
	return true;
}

} // namespace rimgui::rt
