// 测试着色器

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "ApplicationWindow/module.h"
#include "Render/module.h"
#include "Render/Systems/callback_system.h"

int main()
{
    ApplicationWindow::ApplicationWindowModule appWindowMod;
    Render::RenderModule renderMod;
    Render::System::CallBackSystem callbackSystem;

    appWindowMod.Startup();
    renderMod.Startup();
    callbackSystem.OnStart();

    ObjectWeakPtr<Render::RHIDevice> device = renderMod.GetRHIDevice();
    if (!device)
    {
        std::cerr << "GetRHIDevice failed." << std::endl;
        return -1;
    }

    const char vertexShaderCode[] = R"(
#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUV;

out vec3 vColor;
out vec2 vUV;

void main()
{
    gl_Position = vec4(aPosition, 1.0);
    vColor = aColor;
    vUV = aUV;
}
)";

    const char fragmentShaderCode[] = R"(
#version 460 core

in vec3 vColor;
in vec2 vUV;

out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

    std::atomic_bool finished = false;
    std::atomic_bool shaderProgramCreated = false;

    Render::CreateShaderSourceDesc vertexShader{};
    vertexShader.codeSource = vertexShaderCode;
    vertexShader.size = sizeof(vertexShaderCode) - 1;
    vertexShader.type = Render::ShaderSourceType::Vertex;

    Render::CreateShaderSourceDesc fragmentShader{};
    fragmentShader.codeSource = fragmentShaderCode;
    fragmentShader.size = sizeof(fragmentShaderCode) - 1;
    fragmentShader.type = Render::ShaderSourceType::Fragment;

    Render::RenderResourceHandle<Render::ShaderSourceSpec> vertexShaderHandle{};
    Render::RenderResourceHandle<Render::ShaderSourceSpec> fragmentShaderHandle{};
    Render::RenderResourceHandle<Render::ShaderProgramSpec> shaderProgramHandle{};


    // 创建顶点
    // layout: position(vec3) + color(vec3) + uv(vec2)
    std::vector<float> vertexData = {
        // Screen Pos              // Color              // UV
        0.0f,  0.5f, 0.0f,        1.0f, 0.0f, 0.0f,     0.5f, 1.0f, // top, red
        -0.5f, -0.5f, 0.0f,        0.0f, 1.0f, 0.0f,     0.0f, 0.0f, // left, green
        0.5f, -0.5f, 0.0f,        0.0f, 0.0f, 1.0f,     1.0f, 0.0f  // right, blue
    };
    Render::VertexLayout layout;
    layout.typeSlots.push_back(Render::VertexFieldType::Vec3);
    layout.typeSlots.push_back(Render::VertexFieldType::Vec3);
    layout.typeSlots.push_back(Render::VertexFieldType::Vec2);
    Render::RenderResourceHandle<Render::VertexBufferSpec> bufferhandle;
    device->async_CreateVertexBuffer(layout,3,vertexData.data(),[&](Render::RenderResourceHandle<Render::VertexBufferSpec> handle){
        bufferhandle = handle;
    });

    Render::RenderResourceHandle<Render::PipelineSpec> pipelinehandle;

    auto OnPipelineFinish = 
    [&](Render::RenderResourceHandle<Render::PipelineSpec> handle){
        std::cout << handle.IsValid(); 
        if(!handle.IsValid()){
            finished = true;
        }
        pipelinehandle = handle;
    };

    auto OnProgramFinish =
        [&](Render::RenderResourceHandle<Render::ShaderProgramSpec> handle)
        {
            shaderProgramHandle = handle;
            shaderProgramCreated = handle.IsValid();

            std::cout << "shaderProgram valid: "
                      << std::boolalpha
                      << handle.IsValid()
                      << std::endl;
            Render::CreatePipelineDesc pipelineDesc;
            pipelineDesc.spec.shaderProgram = handle;
            device->async_CreatePipeline(pipelineDesc, OnPipelineFinish);
        };

    auto TryCreateShaderProgram =
        [&]()
        {
            if (shaderProgramCreated)
            {
                return;
            }

            if (!vertexShaderHandle.IsValid())
            {
                return;
            }

            if (!fragmentShaderHandle.IsValid())
            {
                return;
            }

            Render::CreateGraphicShaderDesc desc{};
            desc.vertexShaderSource = vertexShaderHandle;
            desc.fragmentShaderSource = fragmentShaderHandle;

            device->async_CreateGraphicShader(desc, OnProgramFinish);
        };

    Render::OnCreateShaderSourceFinish OnVertexShaderFinish =
        [&](Render::RenderResourceHandle<Render::ShaderSourceSpec> handle)
        {
            std::cout << "vertex shader valid: "
                      << std::boolalpha
                      << handle.IsValid()
                      << std::endl;

            if (!handle.IsValid())
            {
                finished = true;
                return;
            }

            vertexShaderHandle = handle;
            TryCreateShaderProgram();
        };

    Render::OnCreateShaderSourceFinish OnFragmentShaderFinish =
        [&](Render::RenderResourceHandle<Render::ShaderSourceSpec> handle)
        {
            std::cout << "fragment shader valid: "
                      << std::boolalpha
                      << handle.IsValid()
                      << std::endl;

            if (!handle.IsValid())
            {
                finished = true;
                return;
            }

            fragmentShaderHandle = handle;
            TryCreateShaderProgram();
        };

    device->async_CreateShaderSource(vertexShader, OnVertexShaderFinish);
    device->async_CreateShaderSource(fragmentShader, OnFragmentShaderFinish);

    

    auto CheckReady = [&](){
        return bufferhandle.IsValid() && pipelinehandle.IsValid();
    };

    while (!finished)
    {
        glfwPollEvents();
        callbackSystem.OnTick();
        if(CheckReady()){
            ObjectWeakPtr<Render::RHIFrameCommandBuffer> buffer = device->GetRHIFrameCommandBufferPool()->threadAny_GetBuffer();
            buffer->PushCommand(Render::RHICommand::SetBackgroundColor{.color = glm::vec4(1,0,1,1)});
            buffer->PushCommand(Render::RHICommand::SetVertexBuffer{.buffer=bufferhandle});
            buffer->PushCommand(Render::RHICommand::SetPipeline{.pipeline=pipelinehandle});
            buffer->PushCommand(Render::RHICommand::Draw{});
            buffer->PushCommand(Render::RHICommand::Flip{});
            device->async_SubmitFrameCommands(buffer);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (shaderProgramHandle.IsValid())
    {
        std::cout << "Shader program create test success." << std::endl;
    }
    else
    {
        std::cout << "Shader program create test failed." << std::endl;
    }

    callbackSystem.OnEnd();
    renderMod.Shutdown();
    appWindowMod.Shutdown();

    return shaderProgramHandle.IsValid() ? 0 : -1;
}