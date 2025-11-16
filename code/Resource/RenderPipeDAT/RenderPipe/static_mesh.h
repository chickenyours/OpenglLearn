#pragma once

#include "code/Resource/RenderPipeDAT/renderpipe.h"
#include "code/Resource/RenderPipeDAT/Passes/camera_node.h"
#include "code/Resource/RenderPipeDAT/Passes/scene_node.h"

namespace Render{

    struct SimpleRenderPipeInput{
    
    };
    
    struct SimpleRenderPipeOutput{
    
    };
    
    struct SimpleRenderPipeDepend{
        VersionTargetGroupViewer<Render::ModelRenderItem> meshRenderItems;

    };
    
    class SimpleRenderPipe : protected RenderPipe<SimpleRenderPipeInput,SimpleRenderPipeOutput,SimpleRenderPipeDepend>{
        private:
            CameraPass camNode_;
            ScenePass sceneNode_;

        virtual int Init(const NodeContext& cxt) override {
            camNode_.CallInit();
            sceneNode_.CallInit();
        }
        virtual int Set(const NodeContext& cxt) override {
    
        }
        virtual int Update() override {
            
        }

        public:
            void SetMeshRenderItem(const VersionTargetGroupViewer<Render::ModelRenderItem>& other){
                depend.meshRenderItems = other;
                sceneNode_.depend.renderItems = depend.meshRenderItems;
            }
            void SetMainCamera()
    };
}

