#pragma once

#include <queue>
#include <mutex>
#include <tuple>
#include <utility>

#include "Render/Public/RHICommand/rhi_buffer_command.h"

namespace Render{

    template <typename T>
    class ThreadSafeConsumeQueue{
        private:
            std::queue<T> queue1_;
            std::queue<T> queue2_;
            std::queue<T>* outsideQueue_;
            std::queue<T>* insideQueue_;
            bool switchFlag_;
            mutable std::mutex m_;
        public:
            ThreadSafeConsumeQueue(){
                outsideQueue_ = &queue1_;
                insideQueue_ = &queue2_;
                switchFlag_ = false;
            }
            void threadAny_Push(size_t num, const T* addr){
                std::lock_guard<std::mutex> lock(m_);
                for(size_t i = 0; i < num; i++){
                    outsideQueue_->push(addr[i]);
                }
            }

            void thread_Switch(){
                std::lock_guard<std::mutex> lock(m_);
                if(switchFlag_){
                    outsideQueue_ = &queue1_;
                    insideQueue_ = &queue2_;
                    switchFlag_ = false;
                } else{
                    outsideQueue_ = &queue2_;
                    insideQueue_ = &queue1_;
                    switchFlag_ = true;
                }
            }

            bool HasPendingCommands() const {
                std::lock_guard<std::mutex> lock(m_);
                return !outsideQueue_->empty();
            }

            bool thread_IsConsumeQueueEmpty() const {
                return insideQueue_->empty();
            }

            T thread_Pop(){
                T result;
                result = insideQueue_->front();
                insideQueue_->pop();
                return result;
            }
    };


    template <typename... Commands>
    class RHICommandSystem {
    public:
        template <typename Command>
        using Queue = ThreadSafeConsumeQueue<Command>;
        bool isOutsideQueueHasPedding = false;
    public:
        bool thread_empty() const {
            return std::apply(
                [](const auto&... queues) {
                    return (... && queues.thread_IsConsumeQueueEmpty());
                },
                queues_
            );
        }

        bool hasPendingCommands() const {
            return std::apply(
                [](const auto&... queues) {
                    return (... || queues.HasPendingCommands());
                },
                queues_
            );
        }

        void thread_SwitchQueues() {
            std::apply(
                [](auto&... queues) {
                    (queues.thread_Switch(), ...);
                },
                queues_
            );
        }

        template <typename Command>
        Queue<Command>& GetQueue() {
            return std::get<Queue<Command>>(queues_);
        }

        template <typename Command>
        const Queue<Command>& GetQueue() const {
            return std::get<Queue<Command>>(queues_);
        }

    private:
        std::tuple<Queue<Commands>...> queues_;
    };
}