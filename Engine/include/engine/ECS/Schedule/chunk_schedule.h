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

    enum class ChunkRequestOption{
        READ,
        WRITE,
        CHANGE
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
            struct WaitSystemRequest{
                ECS::SystemID id;
                ChunkRequestOption requestType;
            };

            std::unordered_map<ChunkHead*,std::deque<WaitSystemRequest>> chunk2WaitRequest;

            // 多线程访问
            template <typename ComponentT>
            bool ProcessRequest(ChunkHead* head, ChunkExecuteHandle<ComponentT>* handle){
                if(!head->isOccupied){
                    head->isOccupied = true;

                }
                else{

                }
            }

            bool CheckChunk(ChunkHead* head){
                return head->isOccupied;
            }

            void ResponseChunk(ChunkHead* head){
#ifdef DEVELOP
                if(!head){
                    LOG_ERROR("ChunkSchedule","error");
                    return;
                }
#endif
                

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
                ECS::Core::globalECSCoreContext.chunkSchedule->ResponseChunk(ref.infoAddr);
            }
    };
}