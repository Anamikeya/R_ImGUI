#pragma once
// How long each pass takes, on the GPU and the CPU, without stalling: GPU times are read a few frames late from
// timer queries.
//
//   { auto t = timers.scope("Shadows"); ...draw shadows... }   // GPU and CPU time of the block
//   timers.end_frame();                                        // once a frame
//   for (auto &s : timers.stats()) ...                         // averaged over the last half second
#include <rimgui/rt/gl.h>

#include <chrono>
#include <string>
#include <vector>

namespace rimgui::rt {

struct PassTime {
	std::string name;
	double gpu_ms = 0, cpu_ms = 0; // Averaged.
	std::vector<float> gpu_history;  // The last 120 frames, oldest first, for a graph.
};

class GpuTimers {
public:
	GpuTimers() = default;
	~GpuTimers();
	GpuTimers(const GpuTimers &) = delete;
	GpuTimers &operator=(const GpuTimers &) = delete;

	class Scope {
	public:
		Scope(GpuTimers &timers, std::size_t index);
		~Scope();
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	private:
		GpuTimers &timers_;
		std::size_t index_;
		std::chrono::steady_clock::time_point start_;
	};
	// Times until the returned object goes out of scope. Scopes do not nest (OpenGL allows one timer query at a time).
	Scope scope(const std::string &name);
	void end_frame();
	const std::vector<PassTime> &stats() const { return stats_; }
	// The whole frame's GPU time: the sum of the passes.
	double total_gpu_ms() const;

private:
	static constexpr int latency = 4;
	struct Pass {
		std::string name;
		GLuint queries[latency]{};
		bool pending[latency]{};
		double cpu_sum = 0, gpu_sum = 0;
		int cpu_count = 0, gpu_count = 0;
		bool used = false;
	};
	std::vector<Pass> passes_;
	std::vector<PassTime> stats_;
	int frame_ = 0;
	std::chrono::steady_clock::time_point window_start_ = std::chrono::steady_clock::now();
};

} // namespace rimgui::rt
