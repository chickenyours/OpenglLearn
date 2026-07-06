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
    // 后端属性
    OpenglBackendContext context_;
    // rhi_id = vao
    std::unordered_map<uint32_t,GLVertexBufferSpec> VAO2VertexBufferSpec;
private:
    // 管线状态
    RenderResourceHandle<PipelineSpec> currentPipeline;
    RenderResourceHandle<ShaderProgramSpec> currentShaderProgram;
    // 顶点状态
    RenderResourceHandle<VertexBufferSpec> currentVertexBuffer;
    uint32_t vertexNum = 0; // 0 表示顶点缓冲区不合法
    bool isUseElementIndex;
    // 绘制状态
    uint32_t topology;
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

        if (vertexStride == 0) {
            LOG_ERROR("CreateVertexBuffer", "invalid vertex stride");
            return {};
        }

        const size_t bufferSize = command.numVertex * vertexStride;

        if (bufferSize == 0) {
            LOG_ERROR("CreateVertexBuffer", "invalid buffer size");
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
        rhiSpec.isUseElementBuffer = false;
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

    virtual void SetBackgroundColor(const RHICommand::SetBackgroundColor& command) override {
        glClearColor(command.color.x,command.color.y,command.color.z,command.color.w);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    virtual void Flip(const RHICommand::Flip& command) override {
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
            isUseElementIndex = false;
        } else {
            vao = spec->rhi_id;
            vertexNum = spec->num;
            isUseElementIndex = spec->isUseElementBuffer;
        }

        glBindVertexArray(vao);
        currentVertexBuffer = command.buffer;
    }
    virtual void SetPipeline(const RHICommand::SetPipeline& command)  override {
        if (currentPipeline == command.pipeline) {
            return;
        }

        PipelineSpec* newPipeline =
            rhiContext_.resourcePool->PipelineTable.Get(command.pipeline);

        if (!newPipeline) {
            LOG_ERROR("SetPipeline", "cannot get PipelineSpec");
            // hasError = true;
            return;
        }

        ShaderProgramSpec* shaderProgram =
            rhiContext_.resourcePool->shaderProgramTable.Get(newPipeline->shaderProgram);

        if (!shaderProgram) {
            LOG_ERROR("SetPipeline", "cannot get ShaderProgramSpec");
            // hasError = true;
            return;
        }

        if (shaderProgram->rhi_id == 0) {
            LOG_ERROR("SetPipeline", "invalid OpenGL shader program id");
            // hasError = true;
            return;
        }

        // 绑定 shader program
        if (currentShaderProgram != newPipeline->shaderProgram) {
            glUseProgram(shaderProgram->rhi_id);
            currentShaderProgram = newPipeline->shaderProgram;
        }

        // 绑定其他 pipeline 状态
        topology = GL_TRIANGLES;
        // ApplyRasterizerState(newPipeline->rasterizerState);
        // ApplyDepthStencilState(newPipeline->depthStencilState);
        // ApplyBlendState(newPipeline->blendState);
        // ApplyPrimitiveTopology(newPipeline->primitiveTopology);

        currentPipeline = command.pipeline;
    }
    virtual void Draw(const RHICommand::Draw& command) override {
        if(vertexNum){
            if(isUseElementIndex){
                glDrawElements(topology, vertexNum, GL_UNSIGNED_INT, 0);
            }
            else{
                glDrawArrays(topology, 0, vertexNum);
            }
        }
    }
    virtual void DrawInstance(const RHICommand::DrawInstance& command) override {
        
    }
    virtual void DrawRect(const RHICommand::DrawRect& command) override {
        
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
        const CreateShaderSourceDesc& desc = command.createDesc;
        spec.type = desc.type;
        spec.rhi_id = 0;

       // 1. 参数检查
        if (desc.codeSource == nullptr || desc.size == 0)
        {
            std::cerr << "[OpenGL] CreateShaderSource failed: shader source is empty.\n";
            return {};
        }

        // 2. Shader 类型转换
        GLenum glShaderType = ToOpenGLShaderType(desc.type);
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
        const GLchar* source = reinterpret_cast<const GLchar*>(desc.codeSource);
        GLint sourceLength = static_cast<GLint>(desc.size);

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

};

} // namespace Render