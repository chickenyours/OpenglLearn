#pragma once

#include <glad/glad.h>
#include <cstddef>
#include <cstdint>

#include "Render/Private/Backend/backend.h"
#include "Render/Public/render_backend_context.h"

namespace Render {

    struct GLVertexBufferSpec{
        GLuint vbo;
    };

class OpenglBackend : public IBackend {
private:
    OpenglBackendContext context_;

    // rhi_id = vao
    std::unordered_map<uint32_t,GLVertexBufferSpec> VAO2VertexBufferSpec;
    
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
    virtual RenderResourceHandle<VertexBufferSpec> CreateVertexBuffer(
        const CreateVertexBufferCommand& command
    ) override {
        if (command.numVertex == 0 || command.Data == nullptr) {
            LOG_ERROR("CreateVertexBuffer", "invalid vertex buffer data");
            return {};
        }

        const auto& layout = command.vertexLayout;

        if (layout.typeSlots.empty()) {
            LOG_ERROR("CreateVertexBuffer", "empty vertex layout");
            return {};
        }

        const size_t vertexStride = CalculateVertexStride(layout);
        const size_t bufferSize = command.numVertex * vertexStride;

        GLuint vao = 0;
        GLuint vbo = 0;

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(bufferSize),
            command.Data,
            GL_STATIC_DRAW
        );

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
        VAO2VertexBufferSpec[vao] = glSpec;
        VertexBufferSpec rhiSpec;
        rhiSpec.layout = command.vertexLayout;
        rhiSpec.num = command.numVertex;
        rhiSpec.rhi_id = vao;
        auto handle = rhiContext_.resourcePool->vertexBufferTable.Add(rhiSpec);
        return handle;
    }

    virtual void DeleteVertexBuffer(
        const DeleteVertexBufferCommand& command
    ) override {
        // TODO:
        // 从你的资源系统里找到 vao/vbo，然后删除。
        //
        // auto it = vertexBuffers_.find(command.handle);
        // if (it == vertexBuffers_.end()) return;
        //
        // GLuint vao = it->second.vao;
        // GLuint vbo = it->second.vbo;
        //
        // glDeleteBuffers(1, &vbo);
        // glDeleteVertexArrays(1, &vao);
        //
        // vertexBuffers_.erase(it);
    }
};

} // namespace Render