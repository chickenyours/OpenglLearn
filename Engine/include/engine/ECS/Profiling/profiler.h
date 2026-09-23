#pragma once

// Lightweight, main-thread-only frame profiling for the ECS runtime.
//
// Enabled automatically in Debug/Development builds and compiled away entirely
// when disabled, so Release pays nothing. Override explicitly with
// -DECS_PROFILING_ENABLED=0 or =1.
//
// Usage:
//   Profiler::Get().BeginFrame();
//   { ScopedTimer t("MySystem"); ...work... }   // or time manually
//   Profiler::Get().Count("Things", n);
//   Profiler::Get().EndFrame();
//   Profiler::Get().Report("title");
//
// All recording must happen on one thread. Systems run on the main thread, so
// this is safe; worker-side values are aggregated on the main thread instead.

#if !defined(ECS_PROFILING_ENABLED)
#  if defined(NDEBUG)
#    define ECS_PROFILING_ENABLED 0
#  else
#    define ECS_PROFILING_ENABLED 1
#  endif
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if ECS_PROFILING_ENABLED

namespace ECS::Profiling {

struct MetricStats {
    std::size_t samples = 0;
    double average = 0.0;
    double p95 = 0.0;
    double max = 0.0;
};

class Profiler {
public:
    static Profiler& Get() {
        static Profiler instance;
        return instance;
    }

    void BeginFrame() { current_.clear(); }

    // Adds to the current frame's total for `name` (time metrics are in ms).
    void Accumulate(std::string_view name, double value) {
        const std::string key(name);
        if (current_.find(key) == current_.end()) RegisterName(key);
        current_[key] += value;
    }

    // Counter metric: same storage, different intent.
    void Count(std::string_view name, double value) { Accumulate(name, value); }

    void EndFrame() {
        for (const std::string& name : names_) {
            const auto found = current_.find(name);
            history_[name].push_back(found == current_.end() ? 0.0 : found->second);
        }
        ++frames_;
    }

    void Reset() {
        current_.clear();
        history_.clear();
        names_.clear();
        frames_ = 0;
    }

    std::size_t FrameCount() const noexcept { return frames_; }
    const std::vector<std::string>& MetricNames() const noexcept { return names_; }

    MetricStats Stats(std::string_view name) const {
        MetricStats stats;
        const auto found = history_.find(std::string(name));
        if (found == history_.end() || found->second.empty()) return stats;

        std::vector<double> values = found->second;
        stats.samples = values.size();

        double sum = 0.0;
        for (const double value : values) {
            sum += value;
            if (value > stats.max) stats.max = value;
        }
        stats.average = sum / static_cast<double>(values.size());

        std::sort(values.begin(), values.end());
        const std::size_t index = static_cast<std::size_t>(
            0.95 * static_cast<double>(values.size() - 1));
        stats.p95 = values[index];
        return stats;
    }

    void Report(const char* title) const {
        std::printf("\n=== %s ===\n", title);
        std::printf("frames measured: %zu\n", frames_);
        std::printf("%-34s %12s %12s %12s\n", "metric", "avg", "p95", "max");
        for (const std::string& name : names_) {
            const MetricStats stats = Stats(name);
            std::printf("%-34s %12.4f %12.4f %12.4f\n",
                        name.c_str(), stats.average, stats.p95, stats.max);
        }
    }

private:
    void RegisterName(const std::string& name) {
        for (const std::string& existing : names_) {
            if (existing == name) return;
        }
        names_.push_back(name);
        history_.try_emplace(name);
    }

    std::vector<std::string> names_;
    std::unordered_map<std::string, double> current_;
    std::unordered_map<std::string, std::vector<double>> history_;
    std::size_t frames_ = 0;
};

// RAII timer that records its lifetime into the profiler on destruction.
class ScopedTimer {
public:
    explicit ScopedTimer(std::string_view name) noexcept
        : name_(name), start_(Clock::now()) {}

    ~ScopedTimer() { Stop(); }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    void Stop() noexcept {
        if (!active_) return;
        active_ = false;
        const double ms = std::chrono::duration<double, std::milli>(
            Clock::now() - start_).count();
        Profiler::Get().Accumulate(name_, ms);
    }

private:
    using Clock = std::chrono::steady_clock;
    std::string_view name_;
    Clock::time_point start_;
    bool active_ = true;
};

} // namespace ECS::Profiling

#else // ECS_PROFILING_ENABLED

namespace ECS::Profiling {
class ScopedTimer {
public:
    explicit ScopedTimer(std::string_view) noexcept {}
    void Stop() noexcept {}
};
} // namespace ECS::Profiling

#endif // ECS_PROFILING_ENABLED
