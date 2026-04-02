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
        void Start(std::size_t threadCount = std::thread::hardware_concurrency());
        void Stop();

        bool Submit(Scene* scene, JobSystem* ownerJobSystem, const Task& task);
        bool Submit(Scene* scene, JobSystem* ownerJobSystem, ExecuteTask func);

        void WaitIdle();

        bool IsRunning() const noexcept;
        std::size_t GetWorkerCount() const noexcept;
        std::size_t GetPendingTaskCount() const;

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
    };

} // namespace ECS::Core