#pragma once

#include <unordered_map>

#include <deque>
#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/Scene/scene_thread_local_context.h"

namespace ECS::Core{

    enum class FailOption{
        CANCEL, // 失败跳过
        WAIT,   // 失败等待直到获得
    };

    template <typename ComponentT>
    class ChunkArrayRef;

    template <typename ComponentT>
    class ChunkRef;

    template <typename ComponentT>
    class ChunkExecuteHandle;

    


    class ChunkSchedule{
        public:

            template <typename ComponentT>
            bool GetChunk(FailOption option, ArchType* archtype, size_t chunkIndex, ChunkExecuteHandle<ComponentT>* handle = nullptr){
                FixedChunkArray<ComponentT>* chunkArray = archtype->TryCastActiveComponentArray<ComponentT>();
                if(!chunkArray){
                    LOG_ERROR("ChunkSchedule","component not exist");
                    return false;
                }
                if(chunkIndex >= chunkArray->chunkHeads_.size()){
                    LOG_ERROR("ChunkSchedule","over size");
                    return false;
                }
                ChunkHead& info = chunkArray->chunkHeads_[chunkIndex];
                if(info.isOccupied){
                    if(option == FailOption::CANCEL){
                        return false;
                    }
                    else{

                    }
                }

                return true;
            }

            template <typename ComponentT>
            bool GetChunk(FailOption option, const ChunkArrayRef<ComponentT>& ref, size_t index, ChunkExecuteHandle& handle){

            }

            template <typename ComponentT>
            bool GetChunk(FailOption option, const ChunkRef<ComponentT>& ref, ChunkExecuteHandle<ComponentT>* handle = nullptr){

            }

        private:
            struct WaitSystemRequestData{
                ECS::SystemID id;
                ChunkHeadState requestType;
            };

            struct ChunkSchedulingData{
                uint32_t sharedReadNum = 0;
                std::deque<WaitSystemRequestData> waitQueue;
            };

            std::unordered_map<ChunkHead*,ChunkSchedulingData> chunk2WaitRequest;
            // 多线程可调用
            template <typename ComponentT>
            bool ProcessRequest(
                ChunkHeadState requestType,
                ChunkHead* head, 
                ChunkExecuteHandle<ComponentT>* handle, 
                FailOption failOption,
                ECS::SystemID id
            ){
                std::lock_guard<std::mutex> lock(head->requestMutex);
                if(!head->isOccupied){  // 通过短路由
                    head->isOccupied = true;
                    head->state = requestType;
                    return true;
                }
                else{                   // 不通过长编排路由
                    if(head->state == ChunkHeadState::READ){
                        chunk2WaitRequest[head].sharedReadNum++;
                        GenHandle(head, handle);
                        return true;
                    }
                    else{               
                        if(failOption == FailOption::CANCEL){
                            return false;
                        }
                        else{ // 进入等待
                            chunk2WaitRequest[head].waitQueue.push_back(
                                {
                                    id,
                                    requestType
                                }
                            )
                            WaitToGet();
                        }
                    }
                }
            }

            void WaitToGet(){

            }

            bool CheckChunk(ChunkHead* head){
                return head->isOccupied;
            }

            template <typename ComponentT>
            void GenHandle(ChunkHead* head, ChunkExecuteHandle<ComponentT>* handle){
                handle->ref.infoAddr = head;
            }

            void ResponseChunkFinishOccupy(ChunkHead* head){    // 句柄析构响应回调函数,含义为调用者释放chunk占用
                std::lock_guard<std::mutex> lock(head->requestMutex);
                

                if(head->state == ChunkHeadState::READ)
                {
                    chunk2WaitRequest[head].sharedReadNum--;
                    if(chunk2WaitRequest[head].sharedReadNum != 0)
                        return;
                }

                if(head->isWaited){
                    // 出队

                }
                else{
                    head->isOccupied = false;
                    head->state = ChunkHeadState::IDLE;
                }

                
                
            }
    };

    template <typename ComponentT>
    class ChunkArrayRef{
        FixedChunkArray<ComponentT>* arrayAddr;
    };

    template <typename ComponentT>
    class ChunkRef{
        ChunkHead* infoAddr = nullptr;
    };

    template <typename ComponentT>
    class ChunkExecuteHandle{
        private:
           
        public:
            ChunkRef<ComponentT> ref;
            ~ChunkExecuteHandle(){
                ECS::Core::globalECSCoreContext.chunkSchedule->ResponseChunkFinishOccupy(ref.infoAddr);
            }
    };
}