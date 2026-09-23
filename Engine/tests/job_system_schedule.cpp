#include "engine/ECS/JobSystem/job_system_schedule.h"
#include "engine/ECS/JobSystem/job_system.h"

#include <algorithm>
#include <cstdlib>

namespace ECS::Core
{
    Task::Task() noexcept
        : executeFunc_(nullptr)
    {
    }

    Task::Task(ExecuteTask func) noexcept
        : executeFunc_(func)
    {
    }

    bool Task::IsValid() const noexcept
    {
        return executeFunc_ != nullptr;
    }

    void Task::Execute() const noexcept
    {
        if (executeFunc_ != nullptr)
        {
            executeFunc_();
        }
    }

    JobSystemSchedule::JobSystemSchedule() noexcept
        : running_(false),
          stopping_(false),
          activeWorkers_(0)
    {
    }

    JobSystemSchedule::JobSystemSchedule(std::size_t threadCount)
        : JobSystemSchedule()
    {
        Start(threadCount);
    }

    JobSystemSchedule::~JobSystemSchedule()
    {
        Stop();
    }

    std::size_t JobSystemSchedule::DefaultWorkerCount(std::size_t hardwareThreads)
    {
        // Leave one hardware thread for the caller/main thread, but never return
        // zero and never grow past kMaxDefaultWorkers: on large desktops the
        // hardware-1 figure over-provisions a machine that is also running other
        // desktop applications. Explicit Start(N) / ECS_JOB_WORKERS can exceed it.
        const std::size_t candidate = hardwareThreads > 0 ? hardwareThreads - 1 : 0;
        return std::clamp<std::size_t>(candidate, 1, kMaxDefaultWorkers);
    }

    std::size_t JobSystemSchedule::ResolveWorkerCount(std::size_t requested)
    {
        if (requested != 0)
        {
            return requested;
        }

        if (const char* env = std::getenv("ECS_JOB_WORKERS"))
        {
            char* end = nullptr;
            const unsigned long long value = std::strtoull(env, &end, 10);
            if (end != env && value > 0)
            {
                return static_cast<std::size_t>(value);
            }
        }

        std::size_t hardwareThreads = std::thread::hardware_concurrency();
        if (hardwareThreads == 0)
        {
            hardwareThreads = 1;
        }
        return DefaultWorkerCount(hardwareThreads);
    }

    void JobSystemSchedule::Start(std::size_t threadCount)
    {
        if (running_)
        {
            return;
        }

        threadCount = ResolveWorkerCount(threadCount);
        if (threadCount == 0)
        {
            threadCount = 1;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = false;
            peakActiveWorkers_ = 0;
        }

        workers_.clear();
        workers_.reserve(threadCount);

        running_ = true;

        for (std::size_t i = 0; i < threadCount; ++i)
        {
            workers_.emplace_back(&JobSystemSchedule::WorkerLoop, this);
        }
    }

    void JobSystemSchedule::Stop()
    {
        if (!running_)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
        }

        queueCv_.notify_all();

        for (std::thread& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        workers_.clear();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            while (!tasks_.empty())
            {
                tasks_.pop();
            }
            activeWorkers_ = 0;
        }

        running_ = false;
    }

    bool JobSystemSchedule::Submit(Scene* scene, JobSystem* ownerJobSystem, const Task& task)
    {
        if (!task.IsValid())
        {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (!running_ || stopping_)
            {
                return false;
            }

            tasks_.push(ScheduledTask{ scene, ownerJobSystem, task });
        }

        queueCv_.notify_one();
        return true;
    }

    bool JobSystemSchedule::Submit(Scene* scene, JobSystem* ownerJobSystem, ExecuteTask func)
    {
        return Submit(scene, ownerJobSystem, Task(func));
    }

    void JobSystemSchedule::WaitIdle()
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        idleCv_.wait(lock, [this]()
        {
            return tasks_.empty() && activeWorkers_ == 0;
        });
    }

    bool JobSystemSchedule::IsRunning() const noexcept
    {
        return running_;
    }

    std::size_t JobSystemSchedule::GetWorkerCount() const noexcept
    {
        return workers_.size();
    }

    std::size_t JobSystemSchedule::GetPendingTaskCount() const
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return tasks_.size();
    }

    std::size_t JobSystemSchedule::GetPeakActiveWorkers() const
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return peakActiveWorkers_;
    }

    void JobSystemSchedule::WorkerLoop()
    {
        while (true)
        {
            ScheduledTask scheduledTask;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                queueCv_.wait(lock, [this]()
                {
                    return stopping_ || !tasks_.empty();
                });

                if (stopping_ && tasks_.empty())
                {
                    return;
                }

                scheduledTask = tasks_.front();
                tasks_.pop();
                ++activeWorkers_;
                if (activeWorkers_ > peakActiveWorkers_)
                {
                    peakActiveWorkers_ = activeWorkers_;
                }
            }

            localECSCoreContext.scene = scheduledTask.scene;
            scheduledTask.task.Execute();
            localECSCoreContext.scene = nullptr;

            if (scheduledTask.ownerJobSystem != nullptr)
            {
                scheduledTask.ownerJobSystem->NotifyTaskFinished();
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                --activeWorkers_;

                if (tasks_.empty() && activeWorkers_ == 0)
                {
                    idleCv_.notify_all();
                }
            }
        }
    }

} // namespace ECS::Core