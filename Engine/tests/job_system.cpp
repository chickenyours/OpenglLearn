#include "engine/ECS/JobSystem/job_system.h"

namespace ECS::Core
{
    JobSystem::JobSystem() noexcept
        : ownerScene_(nullptr),
          schedule_(nullptr),
          dispatchedTaskCount_(0)
    {
    }

    JobSystem::JobSystem(Scene* ownerScene, JobSystemSchedule* schedule) noexcept
        : ownerScene_(ownerScene),
          schedule_(schedule),
          dispatchedTaskCount_(0)
    {
    }

    void JobSystem::BindScene(Scene* scene) noexcept
    {
        ownerScene_ = scene;
    }

    void JobSystem::BindSchedule(JobSystemSchedule* schedule) noexcept
    {
        schedule_ = schedule;
    }

    Scene* JobSystem::GetScene() const noexcept
    {
        return ownerScene_;
    }

    JobSystemSchedule* JobSystem::GetSchedule() const noexcept
    {
        return schedule_;
    }

    bool JobSystem::Submit(ExecuteTask func)
    {
        return Submit(Task(func));
    }

    bool JobSystem::Submit(const Task& task)
    {
        if (!task.IsValid())
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingTasks_.push(task);
        return true;
    }

    std::size_t JobSystem::DispatchAll()
    {
        if (schedule_ == nullptr || ownerScene_ == nullptr)
        {
            return 0;
        }

        std::size_t dispatchCount = 0;

        while (true)
        {
            Task task;

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (pendingTasks_.empty())
                {
                    break;
                }

                task = pendingTasks_.front();
                pendingTasks_.pop();
                ++dispatchedTaskCount_;
            }

            if (!schedule_->Submit(ownerScene_, this, task))
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                --dispatchedTaskCount_;
                pendingTasks_.push(task);
                break;
            }

            ++dispatchCount;
        }

        return dispatchCount;
    }

    void JobSystem::NotifyTaskFinished()
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (dispatchedTaskCount_ > 0)
        {
            --dispatchedTaskCount_;
        }

        if (pendingTasks_.empty() && dispatchedTaskCount_ == 0)
        {
            idleCv_.notify_all();
        }
    }

    void JobSystem::WaitIdle()
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        idleCv_.wait(lock, [this]()
        {
            return pendingTasks_.empty() && dispatchedTaskCount_ == 0;
        });
    }

    std::size_t JobSystem::GetPendingTaskCount() const
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return pendingTasks_.size();
    }

    std::size_t JobSystem::GetDispatchedTaskCount() const
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return dispatchedTaskCount_;
    }

} // namespace ECS::Core