#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#include <glad/glad.h>

#include "ApplicationWindow/module.h"
#include "Render/module.h"

#include "Render/Systems/callback_system.h"


// std::vector<TestVertex> vertices = {
//     {
//         {  0.0f,  0.5f, 0.0f },
//         {  0.0f,  0.0f, 1.0f },
//         {  0.5f,  1.0f },
//         1
//     },
//     {
//         { -0.5f, -0.5f, 0.0f },
//         {  0.0f,  0.0f, 1.0f },
//         {  0.0f,  0.0f },
//         2
//     },
//     {
//         {  0.5f, -0.5f, 0.0f },
//         {  0.0f,  0.0f, 1.0f },
//         {  1.0f,  0.0f },
//         3
//     }
// };

int main() {
    ApplicationWindow::ApplicationWindowModule appWindowMod;
    Render::RenderModule renderMod; 
    Render::System::CallBackSystem callbackSystem;

    appWindowMod.Startup();
    renderMod.Startup();
    callbackSystem.OnStart();

    ObjectWeakPtr<Render::RHIDevice> device = renderMod.GetRHIDevice();

    Render::VertexLayout layout;
    layout.typeSlots.push_back(Render::VertexFieldType::Vec3);
    layout.typeSlots.push_back(Render::VertexFieldType::Vec2);

    std::vector<float> vertexBufferData = {
        0.0f, 0.0f, 0.0f,   0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,   1.0f, 0.0f,
        0.0f, 1.0f, 1.0f,   1.0f, 1.0f,
        0.0f, 0.0f, 1.0f,   0.0f, 1.0f,
    };

    Render::RenderResourceHandle<Render::VertexBufferSpec> bufferHandle;

    if (device) {
        device->async_CreateVertexBuffer(
            layout,
            4,
            vertexBufferData.data(),
            [&](Render::RenderResourceHandle<Render::VertexBufferSpec> handle) {
                bufferHandle = handle;
            }
        );
    }

    Render::RHIFrameCommandBufferPool* framebufferpool = device->GetRHIFrameCommandBufferPool();

    
    std::atomic_bool flag = true;

    float time = 0.f;

    // FPS 统计
    using Clock = std::chrono::steady_clock;
    auto fpsLastTime = Clock::now();

    std::atomic<int> completedFrames = 0;
    int lastCompletedFrames = 0;

    while (true) {
        glfwPollEvents();

        if (flag.exchange(false)) {
            ObjectWeakPtr<Render::RHIFrameCommandBuffer> buffer =
                framebufferpool->threadAny_GetBuffer();

            Render::RHICommand::SetBackgroundColor setbackground = {
                glm::vec4(glm::sin(time) * 0.5f + 0.5f, 0.f, 0.f, 1.0f)
            };

            buffer->PushCommand<Render::RHICommand::SetBackgroundColor>(setbackground);
            buffer->PushCommand<Render::RHICommand::Flip>({});

            device->async_SubmitFrameCommands(buffer, [&]() {
                completedFrames.fetch_add(1, std::memory_order_relaxed);
                flag.store(true, std::memory_order_release);
            });
        }

        callbackSystem.OnTick();

        // 每 1 秒统计一次真实完成 FPS
        auto now = Clock::now();
        float elapsed = std::chrono::duration<float>(now - fpsLastTime).count();

        if (elapsed >= 1.0f) {
            int currentCompletedFrames = completedFrames.load(std::memory_order_relaxed);
            int framesThisSecond = currentCompletedFrames - lastCompletedFrames;

            float fps = framesThisSecond / elapsed;

            std::cout << "Real FPS: " << fps << std::endl;

            lastCompletedFrames = currentCompletedFrames;
            fpsLastTime = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        time += 0.011f;
    }

    

    return 0;
}
