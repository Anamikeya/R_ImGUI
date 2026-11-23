#pragma once
// How long each pass takes on the GPU (timestamps) and the CPU, read a frame or two later so nothing waits.
//
//   timers.begin_frame();                                  // once a frame, before any scope
//   { auto t = timers.scope(cb, "Shadows"); ...record... }  // GPU and CPU time of what is recorded inside
//   timers.end_frame();
#include <rimgui/vulkan.h>

#include <chrono>
#include <string>
#include <vector>

namespace rimgui::rt::vk {

struct PassTime {
	std::string name;
	double gpu_ms = 0, cpu_ms = 0;  // Averaged over half a second.
	std::vector<float> gpu_history; // The last 120 frames, oldest first.
};

class GpuTimers {
public:
	GpuTimers();
	~GpuTimers();
	GpuTimers(const GpuTimers &) = delete;
	GpuTimers &operator=(const GpuTimers &) = delete;

	class Scope {
	public:
		Scope(GpuTimers &timers, VkCommandBuffer cb, std::size_t pass);
		~Scope();
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	private:
		GpuTimers &timers_;
		VkCommandBuffer cb_;
		std::size_t pass_;
		bool timed_ = false;
		std::chrono::steady_clock::time_point start_;
	};

	// Reads the times of this frame slot's last use (finished: the shell waited for it) and makes its queries ready.
	void begin_frame();
	Scope scope(VkCommandBuffer cb, const std::string &name);
	void end_frame();
	const std::vector<PassTime> &stats() const { return stats_; }
	double total_gpu_ms() const;

private:
	static constexpr uint32_t max_passes = 64;
	struct Slot {
		VkQueryPool pool = VK_NULL_HANDLE;
		std::vector<int> pass_of; // Per query pair, the pass it timed; -1 unused.
		uint32_t used = 0;
	};
	std::vector<Slot> slots_;
	struct Sum {
		double cpu = 0, gpu = 0;
		int cpu_count = 0, gpu_count = 0;
	};
	std::vector<Sum> sums_;
	std::vector<PassTime> stats_;
	uint32_t current_ = 0;
	double period_ns_ = 1;
	std::chrono::steady_clock::time_point window_ = std::chrono::steady_clock::now();
};

} // namespace rimgui::rt::vk
