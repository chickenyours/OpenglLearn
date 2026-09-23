#pragma once

#include "engine/ECS/Context/context.h"

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstddef>
#include <functional>

namespace ECS::Core
{
    class Scene;
    class JobSystem;

    using ExecuteTask = std::function<void()>;

    class Task
    {
    public:
        Task() noexcept;
        explicit Task(ExecuteTask func) noexcept;

        bool IsValid() const noexcept;
        void Execute() const noexcept;

    private:
        ExecuteTask executeFunc_;
    };

    class JobSystemSchedule
    {
    public:
        struct ScheduledTask
        {
            Scene* scene = nullptr;
            JobSystem* ownerJobSystem = nullptr;
            Task task{};
        };

    public:
        JobSystemSchedule() noexcept;
        explicit JobSystemSchedule(std::size_t threadCount);
        ~JobSystemSchedule();

        JobSystemSchedule(const JobSystemSchedule&) = delete;
        JobSystemSchedule& operator=(const JobSystemSchedule&) = delete;

        JobSystemSchedule(JobSystemSchedule&&) = delete;
        JobSystemSchedule& operator=(JobSystemSchedule&&) = delete;

    public:
        // threadCount == 0 means "auto": the count is resolved by
        // ResolveWorkerCount(), which applies the DefaultWorkerCount() policy and
        // the optional ECS_JOB_WORKERS environment override. Any explicit value is
        // honored as-is (clamped to >= 1). Worker policy lives here, not in the
        // gameplay/terrain modules.
        void Start(std::size_t threadCount = 0);
        void Stop();

        // Default worker policy for a given hardware thread count. Pure function so
        // it can be configured/tested independently of the current machine.
        // Upper bound for the automatic (threadCount == 0) worker count. Explicit
        // Start(N) may still exceed it; the cap only tames the default on large
        // desktops, where hardware-1 over-provisions a shared machine.
        static constexpr std::size_t kMaxDefaultWorkers = 8;
        static std::size_t DefaultWorkerCount(std::size_t hardwareThreads);
        // Resolves a requested count: explicit value wins; 0 -> ECS_JOB_WORKERS if
        // set and valid; otherwise DefaultWorkerCount(hardware_concurrency()).
        static std::size_t ResolveWorkerCount(std::size_t requested);

        bool Submit(Scene* scene, JobSystem* ownerJobSystem, const Task& task);
        bool Submit(Scene* scene, JobSystem* ownerJobSystem, ExecuteTask func);

        void WaitIdle();

        bool IsRunning() const noexcept;
        std::size_t GetWorkerCount() const noexcept;
        std::size_t GetPendingTaskCount() const;

        // Profiling: highest number of workers that were executing a task at the
        // same time since Start(). Read-safe via the existing queue mutex.
        std::size_t GetPeakActiveWorkers() const;

    private:
        void WorkerLoop();

    private:
        std::vector<std::thread> workers_;

        mutable std::mutex queueMutex_;
        std::condition_variable queueCv_;
        std::condition_variable idleCv_;
        std::queue<ScheduledTask> tasks_;

        std::atomic<bool> running_;
        bool stopping_;
        std::size_t activeWorkers_;
        std::size_t peakActiveWorkers_ = 0;
    };

} // namespace ECS::Core