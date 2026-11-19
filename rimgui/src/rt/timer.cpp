#include <rimgui/rt/timer.h>

namespace rimgui::rt {

GpuTimers::~GpuTimers()
{
	for (auto &p : passes_) {
		glDeleteQueries(latency, p.queries);
	}
}

GpuTimers::Scope GpuTimers::scope(const std::string &name)
{
	std::size_t index = 0;
	while (index < passes_.size() && passes_[index].name != name) {
		++index;
	}
	if (index == passes_.size()) {
		Pass p;
		p.name = name;
		glGenQueries(latency, p.queries);
		passes_.push_back(p);
		stats_.push_back({name, 0, 0, {}});
	}
	return Scope(*this, index);
}

GpuTimers::Scope::Scope(GpuTimers &timers, std::size_t index) : timers_(timers), index_(index), start_(std::chrono::steady_clock::now())
{
	Pass &p = timers_.passes_[index_];
	const int slot = timers_.frame_ % latency;
	if (!p.pending[slot]) {
		glBeginQuery(GL_TIME_ELAPSED, p.queries[slot]);
	}
}

GpuTimers::Scope::~Scope()
{
	Pass &p = timers_.passes_[index_];
	const int slot = timers_.frame_ % latency;
	if (!p.pending[slot]) {
		glEndQuery(GL_TIME_ELAPSED);
		p.pending[slot] = true;
	}
	p.cpu_sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
	++p.cpu_count;
	p.used = true;
}

void GpuTimers::end_frame()
{
	for (std::size_t i = 0; i < passes_.size(); ++i) {
		Pass &p = passes_[i];
		// Every finished query, oldest frames first.
		for (int age = latency - 1; age >= 1; --age) {
			const int slot = ((frame_ - age) % latency + latency) % latency;
			if (!p.pending[slot]) {
				continue;
			}
			GLint available = 0;
			glGetQueryObjectiv(p.queries[slot], GL_QUERY_RESULT_AVAILABLE, &available);
			if (!available) {
				continue;
			}
			GLuint64 ns = 0;
			glGetQueryObjectui64v(p.queries[slot], GL_QUERY_RESULT, &ns);
			p.pending[slot] = false;
			const double ms = double(ns) / 1e6;
			p.gpu_sum += ms;
			++p.gpu_count;
			auto &history = stats_[i].gpu_history;
			history.push_back(float(ms));
			if (history.size() > 120) {
				history.erase(history.begin());
			}
		}
	}
	++frame_;
	const auto now = std::chrono::steady_clock::now();
	if (now - window_start_ >= std::chrono::milliseconds(500)) {
		window_start_ = now;
		for (std::size_t i = 0; i < passes_.size(); ++i) {
			Pass &p = passes_[i];
			stats_[i].cpu_ms = p.cpu_count ? p.cpu_sum / p.cpu_count : 0;
			stats_[i].gpu_ms = p.gpu_count ? p.gpu_sum / p.gpu_count : (p.used ? stats_[i].gpu_ms : 0);
			p.cpu_sum = p.gpu_sum = 0;
			p.cpu_count = p.gpu_count = 0;
			p.used = false;
		}
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

} // namespace rimgui::rt
