// 这是一个渲染管线的头文件，定义了渲染管线的基本结构和接口
#pragma once

namespace Render{
    enum SimpleRenderPipeFlag{
        SIMPLE_RENDER_PIPE_FLAG_NONE = 0,
        SIMPLE_RENDER_PIPE_FLAG_SHADOW = 1,
        SIMPLE_RENDER_PIPE_FLAG_REFLECTION = 2,
        SIMPLE_RENDER_PIPE_FLAG_REFRACTION = 4,
        SIMPLE_RENDER_PIPE_FLAG_POST_PROCESS = 8,
        SIMPLE_RENDER_PIPE_FLAG_UI = 16,
        SIMPLE_RENDER_PIPE_FLAG_ALL = 0xFFFFFFFF
    };

    class SimpleRenderPipe{
    public:
        SimpleRenderPipe();
        ~SimpleRenderPipe();
        void Render();
        //设置输出缓冲区大小
        void Resize(unsigned int width, unsigned int height);
        inline unsigned int GetOutBuffer(){return _outBuffer;}
        inline unsigned int GetOutBufferWidth(){return _outBufferWidth;}
        inline unsigned int GetOutBufferHeight(){return _outBufferHeight;}
    private:
        //输出缓冲区
        unsigned int _outBuffer,_outBufferWidth, _outBufferHeight;
        //属性
        unsigned int _depthMapFBO, _depthMap, _depthMapWidth, _depthMapHeight;
        void Init();
        void Update();
        void RenderScene();
        void RenderUI();
    };
}