// Task 1.3 verification: worker-count policy and explicit Start(N).
//
// Keeps the worker decision in JobSystemSchedule. Prints the required
// hardware/configured/actual triple for the default and explicit configurations.

#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "engine/ECS/JobSystem/job_system_schedule.h"

namespace {

bool CheckDefaultPolicy(std::size_t hardwareThreads, std::size_t expected) {
    const std::size_t actual =
        ECS::Core::JobSystemSchedule::DefaultWorkerCount(hardwareThreads);
    const bool ok = actual == expected;
    std::printf("DefaultWorkerCount(hw=%2zu) = %2zu (expected %2zu)%s\n",
                hardwareThreads, actual, expected, ok ? "" : "  <-- FAIL");
    return ok;
}

bool CheckExplicitStart(std::size_t requested) {
    ECS::Core::JobSystemSchedule schedule;
    schedule.Start(requested);
    const std::size_t actual = schedule.GetWorkerCount();
    schedule.Stop();

    const bool ok = actual == requested;
    std::printf("Start(%2zu): configured=%2zu actual=%2zu%s\n",
                requested, requested, actual, ok ? "" : "  <-- FAIL");
    return ok;
}

} // namespace

int main() {
    bool ok = true;

    const std::size_t hardwareThreads = std::thread::hardware_concurrency();
    std::printf("hardware threads: %zu\n", hardwareThreads);

    // Pure policy table: leave one hardware thread, clamp to [1, kMaxDefaultWorkers].
    ok &= CheckDefaultPolicy(0, 1);
    ok &= CheckDefaultPolicy(1, 1);
    ok &= CheckDefaultPolicy(2, 1);
    ok &= CheckDefaultPolicy(3, 2);
    ok &= CheckDefaultPolicy(4, 3);
    ok &= CheckDefaultPolicy(8, 7);
    ok &= CheckDefaultPolicy(9, 8);
    ok &= CheckDefaultPolicy(16, 8);
    ok &= CheckDefaultPolicy(24, 8);
    ok &= CheckDefaultPolicy(64, 8);

    // Explicit Start(N) must be honored exactly, including above the default cap.
    ok &= CheckExplicitStart(1);
    ok &= CheckExplicitStart(2);
    ok &= CheckExplicitStart(4);
    ok &= CheckExplicitStart(8);
    ok &= CheckExplicitStart(12);
    ok &= CheckExplicitStart(16);

    // Default / auto path (0), including any ECS_JOB_WORKERS override.
    {
        const std::size_t configured =
            ECS::Core::JobSystemSchedule::ResolveWorkerCount(0);
        ECS::Core::JobSystemSchedule schedule;
        schedule.Start(0);
        const std::size_t actual = schedule.GetWorkerCount();
        schedule.Stop();

        const bool autoOk = actual == configured && configured > 0;
        std::printf("Start(auto): configured=%zu actual=%zu%s\n",
                    configured, actual, autoOk ? "" : "  <-- FAIL");
        ok &= autoOk;
    }

    if (const char* env = std::getenv("ECS_JOB_WORKERS")) {
        const std::size_t configured =
            ECS::Core::JobSystemSchedule::ResolveWorkerCount(0);
        const std::size_t expected = static_cast<std::size_t>(std::strtoull(env, nullptr, 10));
        const bool envOk = configured == expected;
        std::printf("ECS_JOB_WORKERS=%s -> configured=%zu%s\n",
                    env, configured, envOk ? "" : "  <-- FAIL");
        ok &= envOk;
    }

    std::printf("%s\n", ok ? "worker config OK" : "worker config FAILED");
    return ok ? 0 : 1;
}
