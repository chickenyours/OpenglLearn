#pragma once

#include "engine/ECS/JobSystem/job_system_schedule.h"

#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstddef>

namespace ECS::Core
{
    class Scene;
    class JobSystemSchedule;

    class JobSystem
    {
    public:
        JobSystem() noexcept;
        JobSystem(Scene* ownerScene, JobSystemSchedule* schedule) noexcept;

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        JobSystem(JobSystem&&) = delete;
        JobSystem& operator=(JobSystem&&) = delete;

    public:
        void BindScene(Scene* scene) noexcept;
        void BindSchedule(JobSystemSchedule* schedule) noexcept;

        Scene* GetScene() const noexcept;
        JobSystemSchedule* GetSchedule() const noexcept;

        bool Submit(ExecuteTask func);
        bool Submit(const Task& task);

        std::size_t DispatchAll();

        void NotifyTaskFinished();
        void WaitIdle();

        std::size_t GetPendingTaskCount() const;
        std::size_t GetDispatchedTaskCount() const;

    private:
        Scene* ownerScene_;
        JobSystemSchedule* schedule_;

        mutable std::mutex queueMutex_;
        std::condition_variable idleCv_;
        std::queue<Task> pendingTasks_;
        std::size_t dispatchedTaskCount_;
    };

} // namespace ECS::Core