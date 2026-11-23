#include <rimgui/rt/vk/timer.h>

#include <stdexcept>

namespace rimgui::rt::vk {

GpuTimers::GpuTimers()
{
	const auto &c = *vulkan::context();
	period_ns_ = c.properties.limits.timestampPeriod;
	slots_.resize(c.frames_in_flight);
	for (auto &s : slots_) {
		VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = max_passes * 2;
		if (vkCreateQueryPool(c.device, &info, nullptr, &s.pool) != VK_SUCCESS) {
			throw std::runtime_error("vkCreateQueryPool failed");
		}
		vkResetQueryPool(c.device, s.pool, 0, max_passes * 2);
		s.pass_of.assign(max_passes, -1);
	}
}

GpuTimers::~GpuTimers()
{
	const auto *c = vulkan::context();
	if (!c) {
		return;
	}
	for (auto &s : slots_) {
		VkDevice device = c->device;
		VkQueryPool pool = s.pool;
		vulkan::defer([device, pool] { vkDestroyQueryPool(device, pool, nullptr); });
	}
}

void GpuTimers::begin_frame()
{
	const auto &c = *vulkan::context();
	if (!started_) {
		// The first window starts with the first frame, not with the setup before it (shaders building).
		started_ = true;
		window_ = std::chrono::steady_clock::now();
	}
	current_ = c.frame_slot;
	Slot &s = slots_[current_];
	if (s.used) {
		std::vector<uint64_t> results(std::size_t(s.used) * 4); // value, availability pairs
		vkGetQueryPoolResults(c.device, s.pool, 0, s.used * 2, results.size() * sizeof(uint64_t), results.data(), sizeof(uint64_t) * 2,
			VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		for (uint32_t i = 0; i < s.used; ++i) {
			const int pass = s.pass_of[i];
			const uint64_t begin = results[std::size_t(i) * 4], begin_ok = results[std::size_t(i) * 4 + 1];
			const uint64_t end = results[std::size_t(i) * 4 + 2], end_ok = results[std::size_t(i) * 4 + 3];
			if (pass < 0 || !begin_ok || !end_ok || end < begin) {
				continue;
			}
			const double ms = double(end - begin) * period_ns_ / 1e6;
			sums_[std::size_t(pass)].gpu += ms;
			++sums_[std::size_t(pass)].gpu_count;
			auto &history = stats_[std::size_t(pass)].gpu_history;
			history.push_back(float(ms));
			if (history.size() > 120) {
				history.erase(history.begin());
			}
		}
		vkResetQueryPool(c.device, s.pool, 0, s.used * 2);
	}
	s.used = 0;
}

GpuTimers::Scope GpuTimers::scope(VkCommandBuffer cb, const std::string &name)
{
	std::size_t pass = 0;
	while (pass < stats_.size() && stats_[pass].name != name) {
		++pass;
	}
	if (pass == stats_.size()) {
		stats_.push_back({name, 0, 0, {}});
		sums_.push_back({});
	}
	return Scope(*this, cb, pass);
}

GpuTimers::Scope::Scope(GpuTimers &timers, VkCommandBuffer cb, std::size_t pass)
	: timers_(timers), cb_(cb), pass_(pass), start_(std::chrono::steady_clock::now())
{
	Slot &s = timers_.slots_[timers_.current_];
	if (s.used < max_passes) {
		// The pair is taken now, so scopes inside scopes each get their own.
		query_ = s.used++;
		s.pass_of[query_] = int(pass);
		vkCmdWriteTimestamp2(cb_, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, s.pool, query_ * 2);
		timed_ = true; // Past the limit, a pass this frame gets its CPU time only.
	}
}

GpuTimers::Scope::~Scope()
{
	Slot &s = timers_.slots_[timers_.current_];
	if (timed_) {
		vkCmdWriteTimestamp2(cb_, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, s.pool, query_ * 2 + 1);
	}
	if (pass_ < timers_.sums_.size()) {
		timers_.sums_[pass_].cpu += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
		++timers_.sums_[pass_].cpu_count;
	}
}

void GpuTimers::end_frame()
{
	const auto now = std::chrono::steady_clock::now();
	if (now - window_ < std::chrono::milliseconds(500)) {
		return;
	}
	// GPU times arrive a frame or two late: a window closes once they have (or after two seconds without any).
	bool any_gpu = false;
	for (const Sum &sum : sums_) {
		any_gpu = any_gpu || sum.gpu_count > 0;
	}
	if (!any_gpu && !sums_.empty() && now - window_ < std::chrono::seconds(2)) {
		return;
	}
	window_ = now;
	for (std::size_t i = 0; i < stats_.size(); ++i) {
		Sum &sum = sums_[i];
		stats_[i].cpu_ms = sum.cpu_count ? sum.cpu / sum.cpu_count : 0;
		stats_[i].gpu_ms = sum.gpu_count ? sum.gpu / sum.gpu_count : 0;
		sum = {};
	}
}

double GpuTimers::total_gpu_ms() const
{
	double total = 0;
	for (const auto &s : stats_) {
		total += s.gpu_ms;
	}
	return total;
}

} // namespace rimgui::rt::vk
