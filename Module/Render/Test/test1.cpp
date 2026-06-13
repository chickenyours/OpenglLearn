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

    while (true) {
        callbackSystem.OnTick();


        device->async_ExecuteCode([](){
            glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        });

        if (bufferHandle.IsValid()) {
            auto spec = device->GetResourcePool().vertexBufferTable.Get(bufferHandle);

            if (spec) {
                LOG_INFO("bufferHandle", "id : " + std::to_string(bufferHandle.id));
                LOG_INFO("spec", "num : " + std::to_string(spec->num));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(32));
    }

    return 0;
}
