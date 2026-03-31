// 非线程安全: Start/Stop/析构等生命周期接口需要外部顺序调用
// 线程安全: Submit 可多线程调用, 工作线程内部并发消费任务

#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstddef>
#include <stdexcept>

namespace ECS::Core
{
    using ExecuteTask = void(*)();

    class Task
    {
    public:
        Task() noexcept
            : executeFunc_(nullptr)
        {
        }

        explicit Task(ExecuteTask func) noexcept
            : executeFunc_(func)
        {
        }

        bool IsValid() const noexcept
        {
            return executeFunc_ != nullptr;
        }

        void Execute() const noexcept
        {
            if (executeFunc_ != nullptr)
            {
                executeFunc_();
            }
        }

    private:
        ExecuteTask executeFunc_;
    };

    // 简单线程池
    class JobSystem
    {
    public:
        JobSystem() noexcept
            : running_(false),
              stopping_(false),
              activeWorkers_(0)
        {
        }

        explicit JobSystem(std::size_t threadCount)
            : JobSystem()
        {
            Start(threadCount);
        }

        ~JobSystem()
        {
            Stop();
        }

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        JobSystem(JobSystem&&) = delete;
        JobSystem& operator=(JobSystem&&) = delete;

    public:
        // 非线程安全: 需要外部顺序调用
        void Start(std::size_t threadCount = std::thread::hardware_concurrency())
        {
            if (running_)
            {
                return;
            }

            if (threadCount == 0)
            {
                threadCount = 1;
            }

            stopping_ = false;
            running_ = true;

            workers_.reserve(threadCount);
            for (std::size_t i = 0; i < threadCount; ++i)
            {
                workers_.emplace_back(&JobSystem::WorkerLoop, this);
            }
        }

        // 非线程安全: 需要外部顺序调用
        void Stop()
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
            running_ = false;

            // 此时所有工作线程都退出了, activeWorkers_ 应该归零
            activeWorkers_ = 0;
        }

        // 线程安全: 可以并发提交
        bool Submit(ExecuteTask func)
        {
            return Submit(Task(func));
        }

        // 线程安全: 可以并发提交
        bool Submit(const Task& task)
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

                tasks_.push(task);
            }

            queueCv_.notify_one();
            return true;
        }

        // 等待队列清空且当前无工作线程执行任务
        void WaitIdle()
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            idleCv_.wait(lock, [this]()
            {
                return tasks_.empty() && activeWorkers_ == 0;
            });
        }

        bool IsRunning() const noexcept
        {
            return running_;
        }

        std::size_t GetWorkerCount() const noexcept
        {
            return workers_.size();
        }

        std::size_t GetPendingTaskCount() const
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            return tasks_.size();
        }

    private:
        void WorkerLoop()
        {
            while (true)
            {
                Task task;

                {
                    std::unique_lock<std::mutex> lock(queueMutex_);

                    queueCv_.wait(lock, [this]()
                    {
                        return stopping_ || !tasks_.empty();
                    });

                    // 停止且任务全空 -> 退出线程
                    if (stopping_ && tasks_.empty())
                    {
                        return;
                    }

                    task = tasks_.front();
                    tasks_.pop();
                    ++activeWorkers_;
                }

                task.Execute();

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

    private:
        std::vector<std::thread> workers_;

        mutable std::mutex queueMutex_;
        std::condition_variable queueCv_;
        std::condition_variable idleCv_;
        std::queue<Task> tasks_;

        std::atomic<bool> running_;
        bool stopping_;

        std::size_t activeWorkers_;
    };
}