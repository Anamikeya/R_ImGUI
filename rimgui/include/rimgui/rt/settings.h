#pragma once
// Settings kept between runs, as "key = value" lines. One function both saves and loads, so the two never disagree:
//
//   void serialize(Settings &s) override {
//       s.value("radius", radius_);      // Loading: read if the file has it. Saving: written.
//       s.value("samples", samples_);
//   }
#include <rimgui/rt/math.h>

#include <filesystem>
#include <map>
#include <string>

namespace rimgui::rt {

class Settings {
public:
	enum class Mode { Load, Save };
	explicit Settings(Mode mode) : mode_(mode) {}

	bool loading() const { return mode_ == Mode::Load; }
	bool saving() const { return mode_ == Mode::Save; }

	// Loading: sets `value` from the key when it is there and reads; otherwise leaves it. Saving: writes it.
	void value(const std::string &key, float &value);
	void value(const std::string &key, double &value);
	void value(const std::string &key, int &value);
	void value(const std::string &key, bool &value);
	void value(const std::string &key, vec3 &value);
	void value(const std::string &key, std::string &value);
	bool has(const std::string &key) const;

	// Keys from here on start with `prefix` and a dot, until the matching pop().
	void push(const std::string &prefix);
	void pop();

	// "key = value" lines, sorted by key; and back. Unreadable lines are skipped.
	std::string text() const;
	void parse(const std::string &text);
	bool read_file(const std::filesystem::path &path);
	// Written to a temporary file first and then moved over the old one, so a crash never leaves half a file.
	bool write_file(const std::filesystem::path &path) const;

	const std::map<std::string, std::string> &values() const { return values_; }

private:
	std::string key(const std::string &name) const;
	Mode mode_;
	std::map<std::string, std::string> values_;
	std::string prefix_;
};

} // namespace rimgui::rt
