#pragma once

#include <glad/glad.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "Render/Private/Backend/backend.h"
#include "Render/Public/render_backend_context.h"

namespace Render {

    struct GLVertexBufferSpec{
        GLuint vbo = 0;
        GLuint ebo = 0;
    };

class OpenglBackend : public IBackend {
private:
    // 后端属性
    OpenglBackendContext context_;
    // rhi_id = vao
    std::unordered_map<uint32_t,GLVertexBufferSpec> VAO2VertexBufferSpec;
private:
    // 管线状态
    RenderResourceHandle<PipelineSpec> currentPipeline;
    RenderResourceHandle<ShaderProgramSpec> currentShaderProgram;
    bool pipelineValid = false;
    // 顶点状态
    RenderResourceHandle<VertexBufferSpec> currentVertexBuffer;
    uint32_t vertexNum = 0; // 0 表示顶点缓冲区不合法
    uint32_t indexNum = 0;
    bool isUseElementIndex = false;
    IndexType currentIndexType = IndexType::UInt32;
    // 绘制状态
    GLenum topology = GL_TRIANGLES;
private:
    static size_t GetVertexDataTypeSize(VertexFieldType type) {
        switch (type) {
        case VertexFieldType::Byte:  return sizeof(std::uint8_t);
        case VertexFieldType::Float: return sizeof(float);
        case VertexFieldType::Int:   return sizeof(std::int32_t);

        case VertexFieldType::Vec2:  return sizeof(float) * 2;
        case VertexFieldType::Vec3:  return sizeof(float) * 3;
        case VertexFieldType::Vec4:  return sizeof(float) * 4;

        case VertexFieldType::Mat2:  return sizeof(float) * 2 * 2;
        case VertexFieldType::Mat3:  return sizeof(float) * 3 * 3;
        case VertexFieldType::Mat4:  return sizeof(float) * 4 * 4;

        default:
            LOG_ERROR("GetVertexDataTypeSize", "unknown vertex field type");
            return 0;
        }
    }

    static bool IsIntegerVertexType(VertexFieldType type) {
        switch (type) {
        case VertexFieldType::Byte:
        case VertexFieldType::Int:
            return true;

        default:
            return false;
        }
    }

    static bool IsMatrixVertexType(VertexFieldType type) {
        switch (type) {
        case VertexFieldType::Mat2:
        case VertexFieldType::Mat3:
        case VertexFieldType::Mat4:
            return true;

        default:
            return false;
        }
    }

    static GLenum GetOpenGLScalarType(VertexFieldType type) {
        switch (type) {
        case VertexFieldType::Byte:
            return GL_UNSIGNED_BYTE;

        case VertexFieldType::Int:
            return GL_INT;

        case VertexFieldType::Float:
        case VertexFieldType::Vec2:
        case VertexFieldType::Vec3:
        case VertexFieldType::Vec4:
        case VertexFieldType::Mat2:
        case VertexFieldType::Mat3:
        case VertexFieldType::Mat4:
            return GL_FLOAT;

        default:
            LOG_ERROR("GetOpenGLScalarType", "unknown vertex field type");
            return GL_FLOAT;
        }
    }

    static GLint GetVertexComponentCount(VertexFieldType type) {
        switch (type) {
        case VertexFieldType::Byte:
        case VertexFieldType::Float:
        case VertexFieldType::Int:
            return 1;

        case VertexFieldType::Vec2:
            return 2;

        case VertexFieldType::Vec3:
            return 3;

        case VertexFieldType::Vec4:
            return 4;

        default:
            LOG_ERROR("GetVertexComponentCount", "invalid non-matrix vertex type");
            return 0;
        }
    }

    static GLint GetMatrixDimension(VertexFieldType type) {
        switch (type) {
        case VertexFieldType::Mat2:
            return 2;

        case VertexFieldType::Mat3:
            return 3;

        case VertexFieldType::Mat4:
            return 4;

        default:
            LOG_ERROR("GetMatrixDimension", "invalid matrix vertex type");
            return 0;
        }
    }

    static size_t CalculateVertexStride(const VertexLayout& layout) {
        size_t stride = 0;

        for (VertexFieldType type : layout.typeSlots) {
            stride += GetVertexDataTypeSize(type);
        }

        return stride;
    }

    static GLenum ToOpenGLBufferUsage(BufferUsage usage) {
        switch (usage) {
        case BufferUsage::Static: return GL_STATIC_DRAW;
        case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
        case BufferUsage::Stream: return GL_STREAM_DRAW;
        }
        return GL_STATIC_DRAW;
    }

    static GLenum ToOpenGLIndexType(IndexType type) {
        return type == IndexType::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    }

    static GLenum ToOpenGLTopology(PrimitiveTopology value) {
        switch (value) {
        case PrimitiveTopology::TriangleList: return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::LineList: return GL_LINES;
        case PrimitiveTopology::PointList: return GL_POINTS;
        }
        return GL_TRIANGLES;
    }

    static GLenum ToOpenGLCompare(CompareOp value) {
        switch (value) {
        case CompareOp::Never: return GL_NEVER;
        case CompareOp::Less: return GL_LESS;
        case CompareOp::LessEqual: return GL_LEQUAL;
        case CompareOp::Equal: return GL_EQUAL;
        case CompareOp::GreaterEqual: return GL_GEQUAL;
        case CompareOp::Greater: return GL_GREATER;
        case CompareOp::Always: return GL_ALWAYS;
        }
        return GL_LEQUAL;
    }

    static void SetupVertexAttribute(
        GLuint& location,
        VertexFieldType type,
        size_t stride,
        size_t offset
    ) {
        const GLenum glType = GetOpenGLScalarType(type);

        if (IsMatrixVertexType(type)) {
            const GLint dim = GetMatrixDimension(type);

            // GLSL 里的 mat4 会占用 4 个 attribute location。
            // mat3 占用 3 个，mat2 占用 2 个。
            for (GLint column = 0; column < dim; ++column) {
                const size_t columnOffset =
                    offset + sizeof(float) * dim * column;

                glVertexAttribPointer(
                    location,
                    dim,
                    GL_FLOAT,
                    GL_FALSE,
                    static_cast<GLsizei>(stride),
                    reinterpret_cast<const void*>(columnOffset)
                );

                glEnableVertexAttribArray(location);

                ++location;
            }

            return;
        }

        const GLint componentCount = GetVertexComponentCount(type);

        if (IsIntegerVertexType(type)) {
            glVertexAttribIPointer(
                location,
                componentCount,
                glType,
                static_cast<GLsizei>(stride),
                reinterpret_cast<const void*>(offset)
            );
        } else {
            glVertexAttribPointer(
                location,
                componentCount,
                glType,
                GL_FALSE,
                static_cast<GLsizei>(stride),
                reinterpret_cast<const void*>(offset)
            );
        }

        glEnableVertexAttribArray(location);

        ++location;
    }

public:
    OpenglBackend(
        const RHIBackContext& rhiContext) : IBackend(rhiContext){}
    virtual void Init(void* specData) override {
        context_ = *reinterpret_cast<OpenglBackendContext*>(specData);
        context_.thread_TakeRenderContext();

    }
    virtual void Shutdown() override {
        glBindVertexArray(0);
        glUseProgram(0);
        context_.thread_DetachRenderContext();
    }
    virtual RenderResourceHandle<VertexBufferSpec> CreateVertexBuffer(
        const CreateVertexBufferCommand& command
    ) override {
        if (command.numVertex == 0 || command.vertexData.empty()) {
            LOG_ERROR("CreateVertexBuffer", "invalid vertex buffer data");
            return {};
        }

        const auto& layout = command.vertexLayout;

        if (layout.typeSlots.empty()) {
            LOG_ERROR("CreateVertexBuffer", "empty vertex layout");
            return {};
        }

        const size_t vertexStride = CalculateVertexStride(layout);

        if (vertexStride == 0) {
            LOG_ERROR("CreateVertexBuffer", "invalid vertex stride");
            return {};
        }

        const size_t bufferSize = command.vertexData.size();

        if (bufferSize == 0 || bufferSize != vertexStride * command.numVertex) {
            LOG_ERROR("CreateVertexBuffer", "vertex byte size does not match layout and count");
            return {};
        }

        const size_t indexStride = command.indexType == IndexType::UInt16
            ? sizeof(uint16_t) : sizeof(uint32_t);
        if ((command.numIndex == 0) != command.indexData.empty() ||
            (!command.indexData.empty() &&
             command.indexData.size() != indexStride * command.numIndex)) {
            LOG_ERROR("CreateVertexBuffer", "index byte size does not match type and count");
            return {};
        }

        GLuint vao = 0;
        GLuint vbo = 0;

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(bufferSize),
            command.vertexData.data(),
            ToOpenGLBufferUsage(command.usage)
        );

        GLuint ebo = 0;
        if (command.numIndex > 0 && !command.indexData.empty()) {
            glGenBuffers(1, &ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(command.indexData.size()),
                command.indexData.data(),
                ToOpenGLBufferUsage(command.usage)
            );
        }

        GLuint location = 0;
        size_t offset = 0;

        for (VertexFieldType type : layout.typeSlots) {
            SetupVertexAttribute(
                location,
                type,
                vertexStride,
                offset
            );

            offset += GetVertexDataTypeSize(type);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        GLVertexBufferSpec glSpec;
        glSpec.vbo = vbo;
        glSpec.ebo = ebo;
        VAO2VertexBufferSpec[vao] = glSpec;
        VertexBufferSpec rhiSpec;
        rhiSpec.layout = command.vertexLayout;
        rhiSpec.num = command.numVertex;
        rhiSpec.vertexCount = command.numVertex;
        rhiSpec.indexCount = command.numIndex;
        rhiSpec.vertexByteSize = static_cast<uint32_t>(command.vertexData.size());
        rhiSpec.indexByteSize = static_cast<uint32_t>(command.indexData.size());
        rhiSpec.rhi_id = vao;
        rhiSpec.isUseElementBuffer = ebo != 0;
        rhiSpec.indexType = command.indexType;
        rhiSpec.usage = command.usage;
        auto handle = rhiContext_.resourcePool->vertexBufferTable.Add(rhiSpec);
        return handle;
    }

    virtual bool UpdateVertexBuffer(
        const UpdateVertexBufferCommand& command
    ) override {
        VertexBufferSpec* spec =
            rhiContext_.resourcePool->vertexBufferTable.Get(command.handle);
        if (!spec || command.numVertex == 0) return false;
        const auto native = VAO2VertexBufferSpec.find(spec->rhi_id);
        if (native == VAO2VertexBufferSpec.end()) return false;

        const size_t stride = CalculateVertexStride(spec->layout);
        const size_t indexStride = command.indexType == IndexType::UInt16
            ? sizeof(uint16_t) : sizeof(uint32_t);
        if (stride == 0 || command.vertexData.size() != stride * command.numVertex ||
            (command.numIndex == 0) != command.indexData.empty() ||
            (!command.indexData.empty() &&
             command.indexData.size() != indexStride * command.numIndex)) {
            LOG_ERROR("UpdateVertexBuffer", "buffer byte size does not match metadata");
            return false;
        }

        glBindVertexArray(spec->rhi_id);
        glBindBuffer(GL_ARRAY_BUFFER, native->second.vbo);
        glBufferData(GL_ARRAY_BUFFER, command.vertexData.size(),
            command.vertexData.data(), ToOpenGLBufferUsage(spec->usage));

        if (command.numIndex > 0) {
            if (native->second.ebo == 0) {
                glGenBuffers(1, &native->second.ebo);
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, native->second.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, command.indexData.size(),
                command.indexData.data(), ToOpenGLBufferUsage(spec->usage));
        } else if (native->second.ebo != 0) {
            glDeleteBuffers(1, &native->second.ebo);
            native->second.ebo = 0;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        if (glGetError() != GL_NO_ERROR) {
            LOG_ERROR("UpdateVertexBuffer", "OpenGL upload failed");
            return false;
        }

        spec->num = command.numVertex;
        spec->vertexCount = command.numVertex;
        spec->indexCount = command.numIndex;
        spec->vertexByteSize = static_cast<uint32_t>(command.vertexData.size());
        spec->indexByteSize = static_cast<uint32_t>(command.indexData.size());
        spec->isUseElementBuffer = command.numIndex > 0;
        spec->indexType = command.indexType;
        if (currentVertexBuffer == command.handle) currentVertexBuffer = {};
        return true;
    }

    virtual void DeleteVertexBuffer(
        const DeleteVertexBufferCommand& command
    ) override {
        VertexBufferSpec* spec =
            rhiContext_.resourcePool->vertexBufferTable.Get(command.handle);
        if (!spec) return;
        const GLuint vao = spec->rhi_id;
        const auto native = VAO2VertexBufferSpec.find(vao);
        if (native != VAO2VertexBufferSpec.end()) {
            if (native->second.ebo != 0) glDeleteBuffers(1, &native->second.ebo);
            if (native->second.vbo != 0) glDeleteBuffers(1, &native->second.vbo);
            VAO2VertexBufferSpec.erase(native);
        }
        if (vao != 0) glDeleteVertexArrays(1, &vao);
        if (currentVertexBuffer == command.handle) {
            currentVertexBuffer = {};
            vertexNum = 0;
            indexNum = 0;
        }
        rhiContext_.resourcePool->vertexBufferTable.Remove(command.handle);
    }

    virtual RenderResourceHandle<UniformBufferSpec> CreateUniformBuffer(
        const CreateUniformBufferCommand& command
    ) override {
        if (command.byteSize == 0 || command.initialData.size() > command.byteSize) {
            LOG_ERROR("CreateUniformBuffer", "invalid buffer size");
            return {};
        }
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, buffer);
        glBufferData(GL_UNIFORM_BUFFER, command.byteSize,
            command.initialData.empty() ? nullptr : command.initialData.data(),
            ToOpenGLBufferUsage(command.usage));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        if (buffer == 0 || glGetError() != GL_NO_ERROR) {
            if (buffer != 0) glDeleteBuffers(1, &buffer);
            LOG_ERROR("CreateUniformBuffer", "OpenGL allocation failed");
            return {};
        }
        UniformBufferSpec spec{};
        spec.byteSize = command.byteSize;
        spec.rhi_id = buffer;
        spec.usage = command.usage;
        return rhiContext_.resourcePool->uniformBufferTable.Add(spec);
    }

    virtual void DeleteUniformBuffer(
        const DeleteUniformBufferCommand& command
    ) override {
        UniformBufferSpec* spec =
            rhiContext_.resourcePool->uniformBufferTable.Get(command.handle);
        if (!spec) return;
        if (spec->rhi_id != 0) glDeleteBuffers(1, &spec->rhi_id);
        rhiContext_.resourcePool->uniformBufferTable.Remove(command.handle);
    }

    virtual void SetBackgroundColor(const RHICommand::SetBackgroundColor& command) override {
        glClearColor(command.color.x,command.color.y,command.color.z,command.color.w);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    virtual void Flip(const RHICommand::Flip&) override {
        context_.thread_Swap();
    }

    virtual void SetVertexBuffer(const RHICommand::SetVertexBuffer& command) override {
        if (command.buffer == currentVertexBuffer) {
            return;
        }

        VertexBufferSpec* spec =
            rhiContext_.resourcePool->vertexBufferTable.Get(command.buffer);

        GLuint vao = 0;

        if (!spec) {
            LOG_ERROR("SetVertexBuffer", "error VertexBuffer");
            vertexNum = 0;
            indexNum = 0;
            isUseElementIndex = false;
        } else {
            vao = spec->rhi_id;
            vertexNum = spec->vertexCount != 0 ? spec->vertexCount : spec->num;
            indexNum = spec->indexCount;
            isUseElementIndex = spec->isUseElementBuffer;
            currentIndexType = spec->indexType;
        }

        glBindVertexArray(vao);
        currentVertexBuffer = command.buffer;
    }
    virtual void SetPipeline(const RHICommand::SetPipeline& command)  override {
        if (pipelineValid && currentPipeline == command.pipeline) {
            return;
        }

        PipelineSpec* newPipeline =
            rhiContext_.resourcePool->PipelineTable.Get(command.pipeline);

        if (!newPipeline) {
            LOG_ERROR("SetPipeline", "cannot get PipelineSpec");
            glUseProgram(0);
            currentPipeline = {};
            currentShaderProgram = {};
            pipelineValid = false;
            return;
        }

        ShaderProgramSpec* shaderProgram =
            rhiContext_.resourcePool->shaderProgramTable.Get(newPipeline->shaderProgram);

        if (!shaderProgram) {
            LOG_ERROR("SetPipeline", "cannot get ShaderProgramSpec");
            glUseProgram(0);
            currentPipeline = {};
            currentShaderProgram = {};
            pipelineValid = false;
            return;
        }

        if (shaderProgram->rhi_id == 0) {
            LOG_ERROR("SetPipeline", "invalid OpenGL shader program id");
            glUseProgram(0);
            currentPipeline = {};
            currentShaderProgram = {};
            pipelineValid = false;
            return;
        }

        // 绑定 shader program
        if (currentShaderProgram != newPipeline->shaderProgram) {
            glUseProgram(shaderProgram->rhi_id);
            currentShaderProgram = newPipeline->shaderProgram;
        }

        topology = ToOpenGLTopology(newPipeline->topology);
        glPolygonMode(GL_FRONT_AND_BACK,
            newPipeline->polygonMode == PolygonMode::Line ? GL_LINE : GL_FILL);
        glFrontFace(newPipeline->frontFaceCounterClockwise ? GL_CCW : GL_CW);

        if (newPipeline->cullMode == CullMode::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(newPipeline->cullMode == CullMode::Front ? GL_FRONT : GL_BACK);
        }

        if (newPipeline->depthTest) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(newPipeline->depthWrite ? GL_TRUE : GL_FALSE);
        glDepthFunc(ToOpenGLCompare(newPipeline->depthCompare));

        if (newPipeline->blendEnable && newPipeline->blendMode != BlendMode::Opaque) {
            glEnable(GL_BLEND);
            if (newPipeline->blendMode == BlendMode::Additive) {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            } else {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
        } else {
            glDisable(GL_BLEND);
        }

        currentPipeline = command.pipeline;
        pipelineValid = true;
    }
    virtual void Draw(const RHICommand::Draw& command) override {
        if (!pipelineValid || !vertexNum || command.instanceCount == 0 ||
            command.firstVertex >= vertexNum) return;
        const uint32_t available = vertexNum - command.firstVertex;
        const uint32_t requested = command.vertexCount == 0
            ? available : command.vertexCount;
        if (requested > available) {
            LOG_ERROR("Draw", "vertex range exceeds bound mesh buffer");
            return;
        }
        const GLsizei count = static_cast<GLsizei>(
            requested);
        const GLsizei instances = static_cast<GLsizei>(command.instanceCount);
        if (instances > 1 || command.firstInstance > 0) {
            glDrawArraysInstancedBaseInstance(
                topology,
                static_cast<GLint>(command.firstVertex),
                count,
                instances,
                command.firstInstance
            );
        } else {
            glDrawArrays(topology, static_cast<GLint>(command.firstVertex), count);
        }
    }
    virtual void DrawInstance(const RHICommand::DrawInstance& command) override {
        RHICommand::Draw draw{};
        draw.instanceCount = command.instanceCount;
        Draw(draw);
    }
    virtual void DrawRect(const RHICommand::DrawRect&) override {
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    virtual void BeginFrame(const RHICommand::BeginFrame& command) override {
        // A new frame is also a state-cache boundary. Resources may be streamed
        // independently, so stale handles must never leak across it.
        currentPipeline = {};
        currentShaderProgram = {};
        pipelineValid = false;
        currentVertexBuffer = {};
        vertexNum = 0;
        indexNum = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0,
            static_cast<GLsizei>(command.framebufferWidth),
            static_cast<GLsizei>(command.framebufferHeight));
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        GLbitfield mask = 0;
        if ((command.clearFlags & RHICommand::ClearColor) != 0) {
            glClearColor(command.clearColor.r, command.clearColor.g,
                command.clearColor.b, command.clearColor.a);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if ((command.clearFlags & RHICommand::ClearDepth) != 0) {
            glClearDepth(command.clearDepth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if ((command.clearFlags & RHICommand::ClearStencil) != 0) {
            glClearStencil(command.clearStencil);
            mask |= GL_STENCIL_BUFFER_BIT;
        }
        if (mask != 0) glClear(mask);
    }

    virtual void EndFrame(const RHICommand::EndFrame& command) override {
        if (command.present) context_.thread_Swap();
    }

    virtual void SetViewport(const RHICommand::SetViewport& command) override {
        glViewport(command.x, command.y,
            static_cast<GLsizei>(command.width),
            static_cast<GLsizei>(command.height));
        glDepthRange(command.minDepth, command.maxDepth);
    }

    virtual void SetScissor(const RHICommand::SetScissor& command) override {
        if (command.enabled) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(command.x, command.y,
                static_cast<GLsizei>(command.width),
                static_cast<GLsizei>(command.height));
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    virtual void BindTexture(const RHICommand::BindTexture& command) override {
        const RHITextureSpec* spec =
            rhiContext_.resourcePool->TextureTable.Get(command.texture);
        glActiveTexture(GL_TEXTURE0 + command.slot);
        glBindTexture(GL_TEXTURE_2D, spec ? spec->rhi_id : 0);
    }

    virtual void BindUniformBuffer(const RHICommand::BindUniformBuffer& command) override {
        const UniformBufferSpec* spec =
            rhiContext_.resourcePool->uniformBufferTable.Get(command.buffer);
        if (!spec) {
            glBindBufferBase(GL_UNIFORM_BUFFER, command.binding, 0);
            return;
        }
        const uint32_t remaining = command.offset < spec->byteSize
            ? spec->byteSize - command.offset : 0;
        const uint32_t size = command.size == 0 ? remaining : command.size;
        if (command.offset == 0 && size == spec->byteSize) {
            glBindBufferBase(GL_UNIFORM_BUFFER, command.binding, spec->rhi_id);
        } else if (size > 0 && command.offset + size <= spec->byteSize) {
            glBindBufferRange(GL_UNIFORM_BUFFER, command.binding, spec->rhi_id,
                command.offset, size);
        }
    }

    virtual void UpdateUniformBuffer(const RHICommand::UpdateUniformBuffer& command) override {
        const UniformBufferSpec* spec =
            rhiContext_.resourcePool->uniformBufferTable.Get(command.buffer);
        if (!spec || command.size == 0 ||
            command.size > RHICommand::MaxInlineUniformBytes ||
            command.offset + command.size > spec->byteSize) {
            LOG_ERROR("UpdateUniformBuffer", "invalid buffer range");
            return;
        }
        glBindBuffer(GL_UNIFORM_BUFFER, spec->rhi_id);
        glBufferSubData(GL_UNIFORM_BUFFER, command.offset, command.size,
            command.data.data());
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    virtual void DrawIndexed(const RHICommand::DrawIndexed& command) override {
        if (!pipelineValid || !isUseElementIndex || indexNum == 0 ||
            command.instanceCount == 0 || command.firstIndex >= indexNum) return;
        const uint32_t available = indexNum - command.firstIndex;
        const uint32_t count = command.indexCount == 0
            ? available : command.indexCount;
        if (count > available) {
            LOG_ERROR("DrawIndexed", "index range exceeds bound mesh buffer");
            return;
        }
        const GLenum type = ToOpenGLIndexType(currentIndexType);
        const uintptr_t byteOffset = static_cast<uintptr_t>(command.firstIndex) *
            (currentIndexType == IndexType::UInt16 ? sizeof(uint16_t) : sizeof(uint32_t));
        const GLsizei instances = static_cast<GLsizei>(command.instanceCount);
        if (instances > 1 || command.firstInstance > 0) {
            glDrawElementsInstancedBaseVertexBaseInstance(
                topology, static_cast<GLsizei>(count), type,
                reinterpret_cast<const void*>(byteOffset), instances,
                command.baseVertex, command.firstInstance);
        } else {
            glDrawElementsBaseVertex(
                topology, static_cast<GLsizei>(count), type,
                reinterpret_cast<const void*>(byteOffset), command.baseVertex);
        }
    }

    // 之后把帧命令缓冲搬到后端后,需要写重置帧渲染状态

    virtual RenderResourceHandle<ShaderProgramSpec> CreateGraphicShaderProgram(const CreateGraphicShaderProgramCommand& command) override {
        ShaderProgramSpec spec{};
        spec.type = ShaderProgramType::Graphics;
        spec.rhi_id = 0;

        const CreateGraphicShaderDesc& desc = command.createDesc;

        const bool isUseGeometry = desc.geometryShaderSource.IsValid();

        ShaderSourceSpec* vertexShaderSourceSpec =
            rhiContext_.resourcePool->shaderSourceTable.Get(desc.vertexShaderSource);

        ShaderSourceSpec* fragmentShaderSourceSpec =
            rhiContext_.resourcePool->shaderSourceTable.Get(desc.fragmentShaderSource);

        ShaderSourceSpec* geometryShaderSourceSpec = nullptr;

        if (isUseGeometry)
        {
            geometryShaderSourceSpec =
                rhiContext_.resourcePool->shaderSourceTable.Get(desc.geometryShaderSource);
        }

        // 1. 检查 ShaderSource 是否存在
        if (!vertexShaderSourceSpec || !fragmentShaderSourceSpec ||
            (isUseGeometry && !geometryShaderSourceSpec))
        {
            LOG_ERROR("OpenGL","CreateGraphicShaderProgram failed: invalid shader source handle.");
            return {};
        }

        // 2. 检查 Shader 类型是否匹配
        if (vertexShaderSourceSpec->type != ShaderSourceType::Vertex)
        {
            LOG_ERROR("OpenGL","CreateGraphicShaderProgram failed: vertex shader type mismatch.");
            return {};
        }

        if (fragmentShaderSourceSpec->type != ShaderSourceType::Fragment)
        {
            LOG_ERROR("OpenGL","CreateGraphicShaderProgram failed: fragment shader type mismatch.");
            return {};
        }

        if (isUseGeometry &&
            geometryShaderSourceSpec->type != ShaderSourceType::Geometry)
        {
            LOG_ERROR("OpenGL","CreateGraphicShaderProgram failed: geometry shader type mismatch.");
            return {};
        }

        // 3. 创建 OpenGL Program
        GLuint program = glCreateProgram();
        if (program == 0)
        {
            LOG_ERROR("OpenGL","CreateGraphicShaderProgram failed: glCreateProgram returned 0.");
            return {};
        }

        // 4. Attach shader
        glAttachShader(program, vertexShaderSourceSpec->rhi_id);
        glAttachShader(program, fragmentShaderSourceSpec->rhi_id);

        if (isUseGeometry)
        {
            glAttachShader(program, geometryShaderSourceSpec->rhi_id);
        }

        // 5. Link program
        glLinkProgram(program);

        // 6. 检查 link 状态
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);

        if (linkStatus != GL_TRUE)
        {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

            std::string errorLog;
            if (logLength > 0)
            {
                errorLog.resize(static_cast<size_t>(logLength));
                glGetProgramInfoLog(program, logLength, nullptr, errorLog.data());
            }

            std::cerr << "[OpenGL] Shader program link failed:\n"
                    << errorLog << "\n";

            glDetachShader(program, vertexShaderSourceSpec->rhi_id);
            glDetachShader(program, fragmentShaderSourceSpec->rhi_id);

            if (isUseGeometry)
            {
                glDetachShader(program, geometryShaderSourceSpec->rhi_id);
            }

            glDeleteProgram(program);

            return {};
        }

        // 7. Link 成功后可以 detach shader
        // Program link 成功后，OpenGL Program 已经拥有链接结果，
        // shader object 可以 detach，之后 shader source 资源是否删除由你的资源系统决定。
        glDetachShader(program, vertexShaderSourceSpec->rhi_id);
        glDetachShader(program, fragmentShaderSourceSpec->rhi_id);

        if (isUseGeometry)
        {
            glDetachShader(program, geometryShaderSourceSpec->rhi_id);
        }

        // 8. 保存 OpenGL program id
        spec.rhi_id = program;

        return rhiContext_.resourcePool->shaderProgramTable.Add(spec);
    }

    GLenum ToOpenGLShaderType(ShaderSourceType type){
        if(type == ShaderSourceType::Vertex){
            return GL_VERTEX_SHADER;
        }
        if(type == ShaderSourceType::Geometry){
            return GL_GEOMETRY_SHADER;
        }
        if(type == ShaderSourceType::Fragment){
            return GL_FRAGMENT_SHADER;
        }
        if(type == ShaderSourceType::Compute){
            return GL_COMPUTE_SHADER;
        }
        return 0;
    }

    virtual RenderResourceHandle<ShaderSourceSpec> CreateShaderSource(const CreateShaderSourceCommand& command) override {
        ShaderSourceSpec spec;
        spec.type = command.type;
        spec.rhi_id = 0;

       // 1. 参数检查
        if (command.source.empty())
        {
            std::cerr << "[OpenGL] CreateShaderSource failed: shader source is empty.\n";
            return {};
        }

        // 2. Shader 类型转换
        GLenum glShaderType = ToOpenGLShaderType(command.type);
        if (glShaderType == 0)
        {
            std::cerr << "[OpenGL] CreateShaderSource failed: invalid shader type.\n";
            return {};
        }

        // 3. 创建 OpenGL Shader
        GLuint shader = glCreateShader(glShaderType);
        if (shader == 0)
        {
            std::cerr << "[OpenGL] CreateShaderSource failed: glCreateShader returned 0.\n";
            return {};
        }

        // 4. 上传源码
        const GLchar* source = command.source.data();
        GLint sourceLength = static_cast<GLint>(command.source.size());

        glShaderSource(shader, 1, &source, &sourceLength);

        // 5. 编译 Shader
        glCompileShader(shader);

        // 6. 检查编译状态
        GLint compileStatus = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);

        if (compileStatus != GL_TRUE)
        {
            GLint logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

            std::string errorLog;
            if (logLength > 0)
            {
                errorLog.resize(static_cast<size_t>(logLength));
                glGetShaderInfoLog(shader, logLength, nullptr, errorLog.data());
            }

            std::cerr << "[OpenGL] Shader compile failed:\n"
                    << errorLog << "\n";

            glDeleteShader(shader);

            return {};
        }

        // 7. 保存 OpenGL shader id
        spec.rhi_id = shader;
        // 8. 加入资源池
        return rhiContext_.resourcePool->shaderSourceTable.Add(spec);
    }

    bool CheckPipelineContent(const PipelineSpec& spec){
        return spec.shaderProgram.IsValid() && rhiContext_.resourcePool->shaderProgramTable.Contains(spec.shaderProgram);
    }
    virtual RenderResourceHandle<PipelineSpec> CreatePipeline(const CreatePipelineCommand& command) override {
        const PipelineSpec& spec = command.desc.spec;
        if(!CheckPipelineContent(spec)){
            return {};
        }
        RenderResourceHandle<PipelineSpec> handle = rhiContext_.resourcePool->PipelineTable.Add(spec);
        return handle;
    }
    virtual void DeletePipeline(const DeletePipelineCommand& command) override {
        if(!command.handle.IsValid()) return;
        rhiContext_.resourcePool->PipelineTable.Remove(command.handle);
    }

    static GLint ToOpenGLInternalFormat(RHITextureFormat format) {
    switch (format) {
    case RHITextureFormat::RGB8:
        return GL_RGB8;
    case RHITextureFormat::RGBA8:
        return GL_RGBA8;
    case RHITextureFormat::RGB32F:
        return GL_RGB32F;
    case RHITextureFormat::RGBA32F:
        return GL_RGBA32F;
    default:
        LOG_ERROR("ToOpenGLInternalFormat", "unknown texture format");
        return 0;
    }
}

    static GLenum ToOpenGLPixelFormat(RHITextureFormat format) {
        switch (format) {
        case RHITextureFormat::RGB8:
        case RHITextureFormat::RGB32F:
            return GL_RGB;

        case RHITextureFormat::RGBA8:
        case RHITextureFormat::RGBA32F:
            return GL_RGBA;

        default:
            LOG_ERROR("ToOpenGLPixelFormat", "unknown texture format");
            return 0;
        }
    }

    static GLenum ToOpenGLPixelType(RHITextureFormat format) {
        switch (format) {
        case RHITextureFormat::RGB8:
        case RHITextureFormat::RGBA8:
            return GL_UNSIGNED_BYTE;

        case RHITextureFormat::RGB32F:
        case RHITextureFormat::RGBA32F:
            return GL_FLOAT;

        default:
            LOG_ERROR("ToOpenGLPixelType", "unknown texture format");
            return 0;
        }
    }

    static GLint ToOpenGLMagFilter(RHIFilterMode filterMode) {
        switch (filterMode) {
        case RHIFilterMode::Nearest:
            return GL_NEAREST;
        case RHIFilterMode::Linear:
            return GL_LINEAR;
        default:
            LOG_ERROR("ToOpenGLMagFilter", "unknown filter mode");
            return GL_LINEAR;
        }
    }

    static GLint ToOpenGLMinFilter(
        RHIFilterMode filterMode,
        RHIMipmapMode mipmapMode
    ) {
        if (filterMode == RHIFilterMode::Nearest &&
            mipmapMode == RHIMipmapMode::Nearest) {
            return GL_NEAREST_MIPMAP_NEAREST;
        }

        if (filterMode == RHIFilterMode::Nearest &&
            mipmapMode == RHIMipmapMode::Linear) {
            return GL_NEAREST_MIPMAP_LINEAR;
        }

        if (filterMode == RHIFilterMode::Linear &&
            mipmapMode == RHIMipmapMode::Nearest) {
            return GL_LINEAR_MIPMAP_NEAREST;
        }

        if (filterMode == RHIFilterMode::Linear &&
            mipmapMode == RHIMipmapMode::Linear) {
            return GL_LINEAR_MIPMAP_LINEAR;
        }

        LOG_ERROR("ToOpenGLMinFilter", "unknown filter or mipmap mode");
        return GL_LINEAR_MIPMAP_LINEAR;
    }

    static GLint ToOpenGLAddressMode(RHIAddressMode addressMode) {
        switch (addressMode) {
        case RHIAddressMode::Repeat:
            return GL_REPEAT;
        case RHIAddressMode::MirroredRepeat:
            return GL_MIRRORED_REPEAT;
        case RHIAddressMode::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
        case RHIAddressMode::ClampToBorder:
            return GL_CLAMP_TO_BORDER;
        default:
            LOG_ERROR("ToOpenGLAddressMode", "unknown address mode");
            return GL_REPEAT;
        }
    }

    // texture
    virtual RenderResourceHandle<RHITextureSpec> CreateTexture(
        const CreateTextureCommand& command
    ) override {
        const CreateRHITextureSpec& createSpec = command.spec;

        if (createSpec.width == 0 || createSpec.height == 0) {
            LOG_ERROR("CreateTexture", "invalid texture size");
            return {};
        }

        const GLint internalFormat =
            ToOpenGLInternalFormat(createSpec.textureDataStoreType);

        const GLenum pixelFormat =
            ToOpenGLPixelFormat(createSpec.textureUseType);

        const GLenum pixelType =
            ToOpenGLPixelType(createSpec.textureUseType);

        if (internalFormat == 0 || pixelFormat == 0 || pixelType == 0) {
            LOG_ERROR("CreateTexture", "invalid texture format");
            return {};
        }

        GLuint texture = 0;
        glGenTextures(1, &texture);

        if (texture == 0) {
            LOG_ERROR("CreateTexture", "glGenTextures failed");
            return {};
        }

        glBindTexture(GL_TEXTURE_2D, texture);

        // 避免 RGB8 在 width 不是 4 字节对齐时上传错位。
        GLint oldUnpackAlignment = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldUnpackAlignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            internalFormat,
            static_cast<GLsizei>(createSpec.width),
            static_cast<GLsizei>(createSpec.height),
            0,
            pixelFormat,
            pixelType,
            command.data.empty() ? nullptr : command.data.data()
        );

        glPixelStorei(GL_UNPACK_ALIGNMENT, oldUnpackAlignment);

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            ToOpenGLMagFilter(createSpec.filterMode)
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            ToOpenGLMinFilter(
                createSpec.filterMode,
                createSpec.mipmapMode
            )
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            ToOpenGLAddressMode(createSpec.addressMode)
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            ToOpenGLAddressMode(createSpec.addressMode)
        );

        // 当前 RHITextureSpec 有 mipmapMode，但没有 enableMipMap 字段。
        // 所以这里默认为纹理生成 mipmap，保证 GL_TEXTURE_MIN_FILTER 使用 mipmap 时纹理完整。
        glGenerateMipmap(GL_TEXTURE_2D);

        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            LOG_ERROR("CreateTexture", "OpenGL texture creation failed");

            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &texture);

            return {};
        }

        glBindTexture(GL_TEXTURE_2D, 0);

        RHITextureSpec rhiSpec{};
        rhiSpec.width = createSpec.width;
        rhiSpec.height = createSpec.height;
        rhiSpec.textureDataStoreType = createSpec.textureDataStoreType;
        rhiSpec.textureUseType = createSpec.textureUseType;
        rhiSpec.filterMode = createSpec.filterMode;
        rhiSpec.mipmapMode = createSpec.mipmapMode;
        rhiSpec.addressMode = createSpec.addressMode;
        rhiSpec.rhi_id = texture;

        RenderResourceHandle<RHITextureSpec> handle =
            rhiContext_.resourcePool->TextureTable.Add(rhiSpec);

        return handle;
    }

    virtual void DeleteTexture(
        const DeleteTextureCommand& command
    ) override {
        if (!command.handle.IsValid()) {
            LOG_ERROR("DeleteTexture", "invalid texture handle");
            return;
        }

        RHITextureSpec* spec =
            rhiContext_.resourcePool->TextureTable.Get(command.handle);

        if (!spec) {
            LOG_ERROR("DeleteTexture", "texture handle not found");
            return;
        }

        GLuint texture = static_cast<GLuint>(spec->rhi_id);

        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }

        rhiContext_.resourcePool->TextureTable.Remove(command.handle);

    }
};



} // namespace Render
