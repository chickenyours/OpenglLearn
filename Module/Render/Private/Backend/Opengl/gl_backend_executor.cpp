#include "Render/Private/Backend/Opengl/gl_backend_executor.h"
#include "Render/Private/Backend/Opengl/gl_buffer.h"
#include "Render/Private/Backend/Opengl/gl_texture.h"
#include "Render/Private/Backend/Opengl/gl_input_layout.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

namespace Render::RHI {

    GLBackendExecutor::GLBackendExecutor() = default;

    GLBackendExecutor::~GLBackendExecutor() {
        Shutdown();
    }

    bool GLBackendExecutor::Initialize() {
        if (initialized_) {
            return true;
        }

        // 获取当前 OpenGL 上下文
        
        
        if (!nativeContext_) {
            std::cerr << "[GLBackendExecutor] No OpenGL context available!" << std::endl;
            return false;
        }

        initialized_ = true;
        std::cout << "[GLBackendExecutor] Initialized successfully." << std::endl;
        return true;
    }

    void GLBackendExecutor::Shutdown() {
        if (!initialized_) {
            return;
        }

        // 清理所有资源
        resources_.~RHIBackendResourceTable();
        new (&resources_) RHIBackendResourceTable();
        nativeContext_ = nullptr;
        initialized_ = false;

        std::cout << "[GLBackendExecutor] Shutdown complete." << std::endl;
    }

    void GLBackendExecutor::Execute(const RHICommand& cmd) {
        if (!initialized_) {
            std::cerr << "[GLBackendExecutor] Not initialized, cannot execute command!" << std::endl;
            return;
        }

        switch (cmd.type) {
            // Buffer 命令
            case RHICommandType::CreateBuffer:
                ExecuteCreateBuffer(std::get<CreateBufferCmd>(cmd.payload));
                break;
            case RHICommandType::UpdateBuffer:
                ExecuteUpdateBuffer(std::get<UpdateBufferCmd>(cmd.payload));
                break;
            case RHICommandType::DestroyBuffer:
                ExecuteDestroyBuffer(std::get<DestroyBufferCmd>(cmd.payload));
                break;
            case RHICommandType::CopyBuffer:
                ExecuteCopyBuffer(std::get<CopyBufferCmd>(cmd.payload));
                break;

            // Texture 命令
            case RHICommandType::CreateEmptyTexture:
                ExecuteCreateEmptyTexture(std::get<CreateEmptyTextureCmd>(cmd.payload));
                break;
            case RHICommandType::CreateTexture:
                ExecuteCreateTexture(std::get<CreateTextureCmd>(cmd.payload));
                break;
            case RHICommandType::UploadTexture:
                ExecuteUploadTexture(std::get<UploadTextureCmd>(cmd.payload));
                break;
            case RHICommandType::UpdateTexture:
                ExecuteUpdateTexture(std::get<UpdateTextureCmd>(cmd.payload));
                break;
            case RHICommandType::DestroyTexture:
                ExecuteDestroyTexture(std::get<DestroyTextureCmd>(cmd.payload));
                break;

            // InputLayout 命令
            case RHICommandType::CreateInputLayout:
                ExecuteCreateInputLayout(std::get<CreateInputLayoutCmd>(cmd.payload));
                break;
            case RHICommandType::DestroyInputLayout:
                ExecuteDestroyInputLayout(std::get<DestroyInputLayoutCmd>(cmd.payload));
                break;

            // 帧命令
            case RHICommandType::BeginFrame:
                ExecuteBeginFrame(std::get<BeginFrameCmd>(cmd.payload));
                break;
            case RHICommandType::EndFrame:
                ExecuteEndFrame(std::get<EndFrameCmd>(cmd.payload));
                break;

            // 委托命令
            case RHICommandType::ExecuteDelegate:
                ExecuteDelegate(std::get<ExecuteDelegateCmd>(cmd.payload));
                break;

            default:
                std::cerr << "[GLBackendExecutor] Unknown command type: " 
                          << static_cast<int>(cmd.type) << std::endl;
                break;
        }
    }

    BackendDelegateContext GLBackendExecutor::GetDelegateContext() {
        BackendDelegateContext ctx;
        ctx.backendExecutor = this;
        ctx.nativeDevice = nullptr;
        ctx.nativeContext = nativeContext_;
        return ctx;
    }

    // Buffer 命令执行
    void GLBackendExecutor::ExecuteCreateBuffer(const CreateBufferCmd& cmd) {
        auto glBuffer = std::make_unique<Backend::OpenGL::GLBuffer>(cmd.desc);
        if (!glBuffer->Create()) {
            std::cerr << "[GLBackendExecutor] Failed to create GLBuffer, handle id=" 
                      << cmd.handle.id << std::endl;
            return;
        }

        if (!cmd.initialData.empty()) {
            glBuffer->Update(cmd.initialData.data(), cmd.initialData.size(), 0);
        }

        resources_.RegisterBuffer(cmd.handle, std::move(glBuffer));
        std::cout << "[GLBackendExecutor] Created Buffer, handle id=" 
                  << cmd.handle.id << std::endl;
    }

    void GLBackendExecutor::ExecuteUpdateBuffer(const UpdateBufferCmd& cmd) {
        auto* glBuffer = resources_.FindBuffer(cmd.handle);
        if (!glBuffer) {
            std::cerr << "[GLBackendExecutor] Buffer not found, handle id=" 
                      << cmd.handle.id << std::endl;
            return;
        }

        glBuffer->Update(cmd.data.data(), cmd.data.size(), cmd.offset);
    }

    void GLBackendExecutor::ExecuteDestroyBuffer(const DestroyBufferCmd& cmd) {
        resources_.RemoveBuffer(cmd.handle);
    }

    void GLBackendExecutor::ExecuteCopyBuffer(const CopyBufferCmd& cmd) {
        auto* srcBuffer = resources_.FindBuffer(cmd.src);
        auto* dstBuffer = resources_.FindBuffer(cmd.dst);

        if (!srcBuffer || !dstBuffer) {
            std::cerr << "[GLBackendExecutor] CopyBuffer: source or destination buffer not found" 
                      << std::endl;
            return;
        }

        // 使用 OpenGL 的 glCopyBufferSubData 进行拷贝
        glBindBuffer(GL_COPY_READ_BUFFER, srcBuffer->GetHandle());
        glBindBuffer(GL_COPY_WRITE_BUFFER, dstBuffer->GetHandle());
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                            static_cast<GLintptr>(cmd.srcOffset),
                            static_cast<GLintptr>(cmd.dstOffset),
                            static_cast<GLsizeiptr>(cmd.size));
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }

    // Texture 命令执行
    void GLBackendExecutor::ExecuteCreateEmptyTexture(const CreateEmptyTextureCmd& cmd) {
        auto glTexture = std::make_unique<Backend::OpenGL::GLTexture>(cmd.desc);
        if (!glTexture->Create()) {
            std::cerr << "[GLBackendExecutor] Failed to create empty GLTexture, handle id="
                      << cmd.handle.id << std::endl;
            return;
        }

        resources_.RegisterTexture(cmd.handle, std::move(glTexture));
        std::cout << "[GLBackendExecutor] Created empty Texture, handle id="
                  << cmd.handle.id << std::endl;
    }

    void GLBackendExecutor::ExecuteCreateTexture(const CreateTextureCmd& cmd) {
        auto glTexture = std::make_unique<Backend::OpenGL::GLTexture>(cmd.desc);
        if (!glTexture->Create()) {
            std::cerr << "[GLBackendExecutor] Failed to create GLTexture, handle id="
                      << cmd.handle.id << std::endl;
            return;
        }

        resources_.RegisterTexture(cmd.handle, std::move(glTexture));
        std::cout << "[GLBackendExecutor] Created Texture, handle id="
                  << cmd.handle.id << std::endl;
    }

    void GLBackendExecutor::ExecuteUploadTexture(const UploadTextureCmd& cmd) {
        auto* glTexture = resources_.FindTexture(cmd.handle);
        if (!glTexture) {
            std::cerr << "[GLBackendExecutor] Texture not found, handle id="
                      << cmd.handle.id << std::endl;
            return;
        }

        glTexture->SetData(
            cmd.data.data(),
            cmd.mipLevel,
            cmd.xOffset, cmd.yOffset, cmd.zOffset,
            cmd.width, cmd.height, cmd.depth
        );
    }

    void GLBackendExecutor::ExecuteUpdateTexture(const UpdateTextureCmd& cmd) {
        auto* glTexture = resources_.FindTexture(cmd.handle);
        if (!glTexture) {
            std::cerr << "[GLBackendExecutor] Texture not found, handle id="
                      << cmd.handle.id << std::endl;
            return;
        }

        glTexture->SetData(
            cmd.data.data(),
            cmd.mipLevel,
            cmd.xOffset, cmd.yOffset, cmd.zOffset,
            cmd.width, cmd.height, cmd.depth
        );
    }

    void GLBackendExecutor::ExecuteDestroyTexture(const DestroyTextureCmd& cmd) {
        resources_.RemoveTexture(cmd.handle);
    }

    // InputLayout 命令执行
    void GLBackendExecutor::ExecuteCreateInputLayout(const CreateInputLayoutCmd& cmd) {
        auto glLayout = std::make_unique<Backend::OpenGL::GLInputLayout>(cmd.desc);
        if (!glLayout->Create()) {
            std::cerr << "[GLBackendExecutor] Failed to create GLInputLayout, handle id=" 
                      << cmd.handle.id << std::endl;
            return;
        }

        resources_.RegisterInputLayout(cmd.handle, std::move(glLayout));
        std::cout << "[GLBackendExecutor] Created InputLayout, handle id=" 
                  << cmd.handle.id << std::endl;
    }

    void GLBackendExecutor::ExecuteDestroyInputLayout(const DestroyInputLayoutCmd& cmd) {
        resources_.RemoveInputLayout(cmd.handle);
    }

    // 帧命令执行
    void GLBackendExecutor::ExecuteBeginFrame(const BeginFrameCmd& cmd) {
        // 第一阶段暂不做复杂逻辑
    }

    void GLBackendExecutor::ExecuteEndFrame(const EndFrameCmd& cmd) {
        // 第一阶段暂不做复杂逻辑
    }

    // 委托命令执行
    void GLBackendExecutor::ExecuteDelegate(const ExecuteDelegateCmd& cmd) {
        BackendDelegateContext ctx = GetDelegateContext();
        cmd.delegate(ctx);
    }

} // namespace Render::RHI
