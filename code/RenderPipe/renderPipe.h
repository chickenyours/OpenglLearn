// 这是一个渲染管线的头文件，定义了渲染管线的基本结构和接口
#pragma once
#include <queue>
#include "code/Model/mesh.h"
#include "code/Material/material.h"
#include "code/RenderPipe/Pass/CSMpass.h"
#include "code/Camera/camera.h"

namespace Render{
    class Material;
    class Mesh;
    struct RenderItem{
        glm::mat4 model;
        Mesh* mesh;
        Material* material;
    }; 
    class SimpleRenderPipe{
        public:
            SimpleRenderPipe();
            void Addmesh(Mesh* mesh);
            void Push(const RenderItem & renderItem);
            void Render();
            void setCamera(const Camera* cam);
            ~SimpleRenderPipe();
        private:
            void Init();
            CSMPass CSMPass;
            // 渲染队列
            std::queue<Mesh*> meshQueue;
            std::queue<RenderItem> renderItemQueue_;
    };
}