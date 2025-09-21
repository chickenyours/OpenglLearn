#pragma once

#include <glad/glad.h>
#include "code/ToolAndAlgorithm/DateType/DAT/node.h"
#include "code/ToolAndAlgorithm/DateType/DAT/traget.h"
#include "code/Resource/RenderPipeDAT/render_environment.h"

namespace Render{
    class Pass : public Node{

        // 初始化流
        public:
            int Init(RenderEnvironment* environment){
                environment_ = environment;
                if(environment_){
                    return InitSelf();
                }
                return 1;
            }
        protected:
            RenderEnvironment* environment_ = nullptr;
            virtual int InitSelf() = 0;

        // 工作流 (根据target状态出发修改流)
        public: 
            int Update(){
                return UpdateSelf();
            }
        protected:
            virtual int UpdateSelf() = 0;
        
        // 更新流
    };
}
