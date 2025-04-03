// 这是一个渲染管线的头文件，定义了渲染管线的基本结构和接口
#pragma once
#include <queue>
#include "code/Model/mesh.h"
#include "code/RenderPipe/Pass/CSMpass.h"
#include "code/Camera/camera.h"

namespace Render{
    class Mesh;
    
    class SimpleRenderPipe{
        public:
            SimpleRenderPipe();
            void Addmesh(Mesh* mesh);
            void Render();
            void setCamera(const Camera* cam) {}
        private:
            CSMPass CSMPass;
            // 渲染队列
            std::queue<Mesh*> meshQueue;
    };
}