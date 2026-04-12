// RHI Buffer 测试程序
// 使用 RHI 接口创建缓冲区，然后用 OpenGL API 验证是否创建成功

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "Render/Public/RHI/RHI_device.h"
#include "Render/Public/RHI/RHI_buffer.h"
#include "Render/Public/RHI/RHI_input_layout.h"
#include "Render/Private/Backend/Opengl/gl_buffer.h"
#include "Render/Private/Backend/Opengl/gl_input_layout.h"
#include <iostream>

using namespace Render::RHI;

struct Vertex {
    float position[2];
    float color[3];
};

// 使用 OpenGL API 验证顶点缓冲
bool VerifyVertexBuffer(GLuint bufferHandle) {
    if (bufferHandle == 0) {
        std::cerr << "  [OpenGL] Buffer handle is 0" << std::endl;
        return false;
    }

    // 检查缓冲区是否存在
    if (!glIsBuffer(bufferHandle)) {
        std::cerr << "  [OpenGL] glIsBuffer returned false" << std::endl;
        return false;
    }

    // 获取缓冲区大小
    GLint bufferSize = 0;
    glBindBuffer(GL_ARRAY_BUFFER, bufferHandle);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (bufferSize == 0) {
        std::cerr << "  [OpenGL] Buffer size is 0" << std::endl;
        return false;
    }

    std::cout << "  [OpenGL] Buffer handle: " << bufferHandle << std::endl;
    std::cout << "  [OpenGL] Buffer size: " << bufferSize << std::endl;
    return true;
}

// 使用 OpenGL API 验证索引缓冲
bool VerifyIndexBuffer(GLuint bufferHandle) {
    if (bufferHandle == 0) {
        std::cerr << "  [OpenGL] Buffer handle is 0" << std::endl;
        return false;
    }

    if (!glIsBuffer(bufferHandle)) {
        std::cerr << "  [OpenGL] glIsBuffer returned false" << std::endl;
        return false;
    }

    GLint bufferSize = 0;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferHandle);
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    if (bufferSize == 0) {
        std::cerr << "  [OpenGL] Buffer size is 0" << std::endl;
        return false;
    }

    std::cout << "  [OpenGL] Buffer handle: " << bufferHandle << std::endl;
    std::cout << "  [OpenGL] Buffer size: " << bufferSize << std::endl;
    return true;
}

// 使用 OpenGL API 验证常量缓冲 (UBO)
bool VerifyConstantBuffer(GLuint bufferHandle) {
    if (bufferHandle == 0) {
        std::cerr << "  [OpenGL] Buffer handle is 0" << std::endl;
        return false;
    }

    if (!glIsBuffer(bufferHandle)) {
        std::cerr << "  [OpenGL] glIsBuffer returned false" << std::endl;
        return false;
    }

    GLint bufferSize = 0;
    glBindBuffer(GL_UNIFORM_BUFFER, bufferHandle);
    glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    if (bufferSize == 0) {
        std::cerr << "  [OpenGL] Buffer size is 0" << std::endl;
        return false;
    }

    std::cout << "  [OpenGL] Buffer handle: " << bufferHandle << std::endl;
    std::cout << "  [OpenGL] Buffer size: " << bufferSize << std::endl;
    return true;
}

// 使用 OpenGL API 验证存储缓冲 (SSBO)
bool VerifyStorageBuffer(GLuint bufferHandle) {
    if (bufferHandle == 0) {
        std::cerr << "  [OpenGL] Buffer handle is 0" << std::endl;
        return false;
    }

    if (!glIsBuffer(bufferHandle)) {
        std::cerr << "  [OpenGL] glIsBuffer returned false" << std::endl;
        return false;
    }

    GLint bufferSize = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufferHandle);
    glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    if (bufferSize == 0) {
        std::cerr << "  [OpenGL] Buffer size is 0" << std::endl;
        return false;
    }

    std::cout << "  [OpenGL] Buffer handle: " << bufferHandle << std::endl;
    std::cout << "  [OpenGL] Buffer size: " << bufferSize << std::endl;
    return true;
}

// 使用 OpenGL API 验证 VAO
bool VerifyVAO(GLuint vaoHandle) {
    if (vaoHandle == 0) {
        std::cerr << "  [OpenGL] VAO handle is 0" << std::endl;
        return false;
    }

    if (!glIsVertexArray(vaoHandle)) {
        std::cerr << "  [OpenGL] glIsVertexArray returned false" << std::endl;
        return false;
    }

    std::cout << "  [OpenGL] VAO handle: " << vaoHandle << std::endl;
    return true;
}

// 使用 OpenGL API 验证缓冲数据内容
bool VerifyBufferData(GLuint bufferHandle, GLenum target, const void* expectedData, GLsizeiptr size) {
    if (!glIsBuffer(bufferHandle)) {
        return false;
    }

    // 映射缓冲读取数据
    glBindBuffer(target, bufferHandle);
    void* ptr = glMapBuffer(target, GL_READ_ONLY);
    if (!ptr) {
        glBindBuffer(target, 0);
        std::cerr << "  [OpenGL] Failed to map buffer" << std::endl;
        return false;
    }

    // 比较数据
    bool match = (memcmp(ptr, expectedData, size) == 0);
    glUnmapBuffer(target);
    glBindBuffer(target, 0);

    if (!match) {
        std::cerr << "  [OpenGL] Buffer data mismatch" << std::endl;
        return false;
    }

    std::cout << "  [OpenGL] Buffer data verified successfully" << std::endl;
    return true;
}

bool TestVertexBuffer(Device* device) {
    std::cout << "\n--- Test: Vertex Buffer ---" << std::endl;

    Vertex vertices[] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f,  0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    // 使用 RHI 创建顶点缓冲
    BufferDesc desc;
    desc.size = sizeof(vertices);
    desc.stride = sizeof(Vertex);
    desc.usage = static_cast<uint32_t>(BufferUsage::Vertex);
    desc.memoryUsage = MemoryUsage::CpuToGpu;
    desc.cpuAccess = static_cast<uint32_t>(CpuAccessMode::Write);

    std::cout << "[RHI] Creating vertex buffer with RHI..." << std::endl;
    Buffer* buffer = device->CreateBuffer(desc, vertices);
    if (!buffer) {
        std::cerr << "[RHI] Failed to create buffer" << std::endl;
        return false;
    }
    std::cout << "[RHI] Buffer created successfully" << std::endl;

    if (!buffer->Check()) {
        std::cerr << "[RHI] Buffer check failed" << std::endl;
        device->DestroyBuffer(buffer);
        return false;
    }
    std::cout << "[RHI] Buffer check passed" << std::endl;

    // 获取 OpenGL 句柄进行验证
    auto* glBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(buffer);
    if (!glBuffer) {
        std::cerr << "[OpenGL] Failed to cast to GLBuffer" << std::endl;
        device->DestroyBuffer(buffer);
        return false;
    }

    GLuint handle = glBuffer->GetHandle();
    std::cout << "[OpenGL] Verifying buffer with OpenGL API..." << std::endl;
    if (!VerifyVertexBuffer(handle)) {
        device->DestroyBuffer(buffer);
        return false;
    }

    // 验证数据内容
    if (!VerifyBufferData(handle, GL_ARRAY_BUFFER, vertices, sizeof(vertices))) {
        device->DestroyBuffer(buffer);
        return false;
    }

    device->DestroyBuffer(buffer);
    std::cout << "[Test] Vertex Buffer Test PASSED" << std::endl;
    return true;
}

bool TestIndexBuffer(Device* device) {
    std::cout << "\n--- Test: Index Buffer ---" << std::endl;

    uint16_t indices[] = {0, 1, 2, 2, 3, 0};

    BufferDesc desc;
    desc.size = sizeof(indices);
    desc.stride = sizeof(uint16_t);
    desc.usage = static_cast<uint32_t>(BufferUsage::Index);
    desc.memoryUsage = MemoryUsage::CpuToGpu;
    desc.cpuAccess = static_cast<uint32_t>(CpuAccessMode::Write);

    std::cout << "[RHI] Creating index buffer with RHI..." << std::endl;
    Buffer* buffer = device->CreateBuffer(desc, indices);
    if (!buffer) {
        std::cerr << "[RHI] Failed to create buffer" << std::endl;
        return false;
    }
    std::cout << "[RHI] Buffer created successfully" << std::endl;

    if (!buffer->Check()) {
        std::cerr << "[RHI] Buffer check failed" << std::endl;
        device->DestroyBuffer(buffer);
        return false;
    }

    auto* glBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(buffer);
    GLuint handle = glBuffer->GetHandle();

    std::cout << "[OpenGL] Verifying buffer with OpenGL API..." << std::endl;
    if (!VerifyIndexBuffer(handle)) {
        device->DestroyBuffer(buffer);
        return false;
    }

    if (!VerifyBufferData(handle, GL_ELEMENT_ARRAY_BUFFER, indices, sizeof(indices))) {
        device->DestroyBuffer(buffer);
        return false;
    }

    device->DestroyBuffer(buffer);
    std::cout << "[Test] Index Buffer Test PASSED" << std::endl;
    return true;
}

bool TestConstantBuffer(Device* device) {
    std::cout << "\n--- Test: Constant Buffer ---" << std::endl;

    struct UniformData {
        float matrix[16];
    };

    UniformData uniforms = {};
    uniforms.matrix[0] = 1.0f;
    uniforms.matrix[5] = 1.0f;
    uniforms.matrix[10] = 1.0f;
    uniforms.matrix[15] = 1.0f;

    BufferDesc desc;
    desc.size = sizeof(uniforms);
    desc.usage = static_cast<uint32_t>(BufferUsage::Constant);
    desc.memoryUsage = MemoryUsage::CpuToGpu;
    desc.cpuAccess = static_cast<uint32_t>(CpuAccessMode::Write);

    std::cout << "[RHI] Creating constant buffer with RHI..." << std::endl;
    Buffer* buffer = device->CreateBuffer(desc, &uniforms);
    if (!buffer) {
        std::cerr << "[RHI] Failed to create buffer" << std::endl;
        return false;
    }
    std::cout << "[RHI] Buffer created successfully" << std::endl;

    if (!buffer->Check()) {
        std::cerr << "[RHI] Buffer check failed" << std::endl;
        device->DestroyBuffer(buffer);
        return false;
    }

    auto* glBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(buffer);
    GLuint handle = glBuffer->GetHandle();

    std::cout << "[OpenGL] Verifying buffer with OpenGL API..." << std::endl;
    if (!VerifyConstantBuffer(handle)) {
        device->DestroyBuffer(buffer);
        return false;
    }

    if (!VerifyBufferData(handle, GL_UNIFORM_BUFFER, &uniforms, sizeof(uniforms))) {
        device->DestroyBuffer(buffer);
        return false;
    }

    device->DestroyBuffer(buffer);
    std::cout << "[Test] Constant Buffer Test PASSED" << std::endl;
    return true;
}

bool TestStorageBuffer(Device* device) {
    std::cout << "\n--- Test: Storage Buffer ---" << std::endl;

    struct SSBOData {
        float data[64];
    };

    SSBOData ssboData = {};
    for (int i = 0; i < 64; ++i) {
        ssboData.data[i] = static_cast<float>(i);
    }

    BufferDesc desc;
    desc.size = sizeof(ssboData);
    desc.usage = static_cast<uint32_t>(BufferUsage::Storage);
    desc.memoryUsage = MemoryUsage::CpuToGpu;
    desc.cpuAccess = static_cast<uint32_t>(CpuAccessMode::Write);

    std::cout << "[RHI] Creating storage buffer with RHI..." << std::endl;
    Buffer* buffer = device->CreateBuffer(desc, &ssboData);
    if (!buffer) {
        std::cerr << "[RHI] Failed to create buffer" << std::endl;
        return false;
    }
    std::cout << "[RHI] Buffer created successfully" << std::endl;

    if (!buffer->Check()) {
        std::cerr << "[RHI] Buffer check failed" << std::endl;
        device->DestroyBuffer(buffer);
        return false;
    }

    auto* glBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(buffer);
    GLuint handle = glBuffer->GetHandle();

    std::cout << "[OpenGL] Verifying buffer with OpenGL API..." << std::endl;
    if (!VerifyStorageBuffer(handle)) {
        device->DestroyBuffer(buffer);
        return false;
    }

    if (!VerifyBufferData(handle, GL_SHADER_STORAGE_BUFFER, &ssboData, sizeof(ssboData))) {
        device->DestroyBuffer(buffer);
        return false;
    }

    device->DestroyBuffer(buffer);
    std::cout << "[Test] Storage Buffer Test PASSED" << std::endl;
    return true;
}

bool TestInputLayout(Device* device) {
    std::cout << "\n--- Test: Input Layout (VAO) ---" << std::endl;

    InputLayoutDesc desc;
    desc.attributes = {
        {0, 0, VertexElementFormat::Float2, 0},   // position
        {1, 0, VertexElementFormat::Float3, 8}    // color
    };
    desc.bindings = {
        {0, sizeof(Vertex), VertexInputRate::PerVertex}
    };

    std::cout << "[RHI] Creating input layout with RHI..." << std::endl;
    InputLayout* layout = device->CreateInputLayout(desc);
    if (!layout) {
        std::cerr << "[RHI] Failed to create input layout" << std::endl;
        return false;
    }
    std::cout << "[RHI] Input layout created successfully" << std::endl;

    if (!layout->Check()) {
        std::cerr << "[RHI] Input layout check failed" << std::endl;
        device->DestroyInputLayout(layout);
        return false;
    }
    std::cout << "[RHI] Input layout check passed" << std::endl;

    // 获取 OpenGL VAO 句柄进行验证
    auto* glLayout = dynamic_cast<Render::Backend::OpenGL::GLInputLayout*>(layout);
    if (!glLayout) {
        std::cerr << "[OpenGL] Failed to cast to GLInputLayout" << std::endl;
        device->DestroyInputLayout(layout);
        return false;
    }

    GLuint vaoHandle = glLayout->GetHandle();

    std::cout << "[OpenGL] Verifying VAO with OpenGL API..." << std::endl;
    if (!VerifyVAO(vaoHandle)) {
        device->DestroyInputLayout(layout);
        return false;
    }

    device->DestroyInputLayout(layout);
    std::cout << "[Test] Input Layout Test PASSED" << std::endl;
    return true;
}

bool TestBufferUpdate(Device* device) {
    std::cout << "\n--- Test: Buffer Update ---" << std::endl;

    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};

    BufferDesc desc;
    desc.size = sizeof(initialData);
    desc.usage = static_cast<uint32_t>(BufferUsage::Vertex);
    desc.memoryUsage = MemoryUsage::CpuToGpu;
    desc.cpuAccess = static_cast<uint32_t>(CpuAccessMode::Write);

    std::cout << "[RHI] Creating buffer with initial data..." << std::endl;
    Buffer* buffer = device->CreateBuffer(desc, initialData);
    if (!buffer) {
        std::cerr << "[RHI] Failed to create buffer" << std::endl;
        return false;
    }

    auto* glBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(buffer);
    GLuint handle = glBuffer->GetHandle();

    // 验证初始数据
    std::cout << "[OpenGL] Verifying initial data..." << std::endl;
    if (!VerifyBufferData(handle, GL_ARRAY_BUFFER, initialData, sizeof(initialData))) {
        device->DestroyBuffer(buffer);
        return false;
    }

    // 使用 RHI Update 更新数据
    float newData[] = {10.0f, 20.0f, 30.0f, 40.0f};
    std::cout << "[RHI] Updating buffer with RHI Update()..." << std::endl;
    if (!buffer->Update(newData, sizeof(newData), 0)) {
        std::cerr << "[RHI] Buffer update failed" << std::endl;
        device->DestroyBuffer(buffer);
        return false;
    }
    std::cout << "[RHI] Buffer update successful" << std::endl;

    // 验证更新后的数据
    std::cout << "[OpenGL] Verifying updated data..." << std::endl;
    if (!VerifyBufferData(handle, GL_ARRAY_BUFFER, newData, sizeof(newData))) {
        device->DestroyBuffer(buffer);
        return false;
    }

    device->DestroyBuffer(buffer);
    std::cout << "[Test] Buffer Update Test PASSED" << std::endl;
    return true;
}

bool TestBufferCopy(Device* device) {
    std::cout << "\n--- Test: Buffer Copy ---" << std::endl;

    float srcData[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float expectedDst[4] = {};

    BufferDesc srcDesc;
    srcDesc.size = sizeof(srcData);
    srcDesc.usage = static_cast<uint32_t>(BufferUsage::TransferSrc);
    srcDesc.memoryUsage = MemoryUsage::CpuToGpu;

    std::cout << "[RHI] Creating source buffer..." << std::endl;
    Buffer* srcBuffer = device->CreateBuffer(srcDesc, srcData);
    if (!srcBuffer) {
        std::cerr << "[RHI] Failed to create source buffer" << std::endl;
        return false;
    }

    BufferDesc dstDesc;
    dstDesc.size = sizeof(srcData);
    dstDesc.usage = static_cast<uint32_t>(BufferUsage::TransferDst);
    dstDesc.memoryUsage = MemoryUsage::CpuToGpu;

    std::cout << "[RHI] Creating destination buffer..." << std::endl;
    Buffer* dstBuffer = device->CreateBuffer(dstDesc);
    if (!dstBuffer) {
        std::cerr << "[RHI] Failed to create destination buffer" << std::endl;
        device->DestroyBuffer(srcBuffer);
        return false;
    }

    // 使用 RHI CopyBuffer 进行拷贝
    std::cout << "[RHI] Copying buffer with RHI CopyBuffer()..." << std::endl;
    if (!device->CopyBuffer(srcBuffer, dstBuffer, sizeof(srcData), 0, 0)) {
        std::cerr << "[RHI] Buffer copy failed" << std::endl;
        device->DestroyBuffer(srcBuffer);
        device->DestroyBuffer(dstBuffer);
        return false;
    }
    std::cout << "[RHI] Buffer copy successful" << std::endl;

    // 验证目标缓冲数据
    auto* glDstBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(dstBuffer);
    GLuint dstHandle = glDstBuffer->GetHandle();

    std::cout << "[OpenGL] Verifying copied data..." << std::endl;
    if (!VerifyBufferData(dstHandle, GL_COPY_WRITE_BUFFER, srcData, sizeof(srcData))) {
        device->DestroyBuffer(srcBuffer);
        device->DestroyBuffer(dstBuffer);
        return false;
    }

    device->DestroyBuffer(srcBuffer);
    device->DestroyBuffer(dstBuffer);
    std::cout << "[Test] Buffer Copy Test PASSED" << std::endl;
    return true;
}

bool TestBufferMapUnmap(Device* device) {
    std::cout << "\n--- Test: Buffer Map/Unmap ---" << std::endl;

    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};

    BufferDesc desc;
    desc.size = sizeof(initialData);
    desc.usage = static_cast<uint32_t>(BufferUsage::Vertex);
    desc.memoryUsage = MemoryUsage::CpuToGpu;
    desc.cpuAccess = static_cast<uint32_t>(CpuAccessMode::Write);

    std::cout << "[RHI] Creating buffer..." << std::endl;
    Buffer* buffer = device->CreateBuffer(desc, initialData);
    if (!buffer) {
        std::cerr << "[RHI] Failed to create buffer" << std::endl;
        return false;
    }

    auto* glBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(buffer);
    GLuint handle = glBuffer->GetHandle();

    // 使用 RHI Map 进行映射
    std::cout << "[RHI] Mapping buffer with RHI Map()..." << std::endl;
    void* mapped = buffer->Map(MapMode::Write);
    if (!mapped) {
        std::cerr << "[RHI] Buffer map failed" << std::endl;
        device->DestroyBuffer(buffer);
        return false;
    }
    std::cout << "[RHI] Buffer mapped successfully" << std::endl;

    // 修改数据
    float* floatData = static_cast<float*>(mapped);
    floatData[0] = 100.0f;
    floatData[1] = 200.0f;
    floatData[2] = 300.0f;
    floatData[3] = 400.0f;

    // 使用 RHI Unmap 进行解映射
    std::cout << "[RHI] Unmapping buffer with RHI Unmap()..." << std::endl;
    buffer->Unmap();
    std::cout << "[RHI] Buffer unmapped successfully" << std::endl;

    // 验证修改后的数据
    float expectedData[] = {100.0f, 200.0f, 300.0f, 400.0f};
    std::cout << "[OpenGL] Verifying mapped data..." << std::endl;
    if (!VerifyBufferData(handle, GL_ARRAY_BUFFER, expectedData, sizeof(expectedData))) {
        device->DestroyBuffer(buffer);
        return false;
    }

    device->DestroyBuffer(buffer);
    std::cout << "[Test] Buffer Map/Unmap Test PASSED" << std::endl;
    return true;
}

bool TestBufferBinding(Device* device) {
    std::cout << "\n--- Test: Buffer Binding ---" << std::endl;

    Vertex vertices[] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f,  0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    BufferDesc vertexDesc;
    vertexDesc.size = sizeof(vertices);
    vertexDesc.stride = sizeof(Vertex);
    vertexDesc.usage = static_cast<uint32_t>(BufferUsage::Vertex);
    vertexDesc.memoryUsage = MemoryUsage::CpuToGpu;

    std::cout << "[RHI] Creating vertex buffer..." << std::endl;
    Buffer* vertexBuffer = device->CreateBuffer(vertexDesc, vertices);
    if (!vertexBuffer) {
        std::cerr << "[RHI] Failed to create vertex buffer" << std::endl;
        return false;
    }

    uint16_t indices[] = {0, 1, 2};
    BufferDesc indexDesc;
    indexDesc.size = sizeof(indices);
    indexDesc.usage = static_cast<uint32_t>(BufferUsage::Index);

    std::cout << "[RHI] Creating index buffer..." << std::endl;
    Buffer* indexBuffer = device->CreateBuffer(indexDesc, indices);
    if (!indexBuffer) {
        std::cerr << "[RHI] Failed to create index buffer" << std::endl;
        device->DestroyBuffer(vertexBuffer);
        return false;
    }

    // 测试 RHI 绑定接口
    std::cout << "[RHI] Binding vertex buffer with RHI BindVertexBuffer()..." << std::endl;
    if (!device->BindVertexBuffer(vertexBuffer, 0)) {
        std::cerr << "[RHI] BindVertexBuffer failed" << std::endl;
        device->DestroyBuffer(vertexBuffer);
        device->DestroyBuffer(indexBuffer);
        return false;
    }

    // 验证 OpenGL 绑定状态
    GLint boundBuffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &boundBuffer);
    auto* glVertexBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(vertexBuffer);
    if (static_cast<GLuint>(boundBuffer) != glVertexBuffer->GetHandle()) {
        std::cerr << "[OpenGL] GL_ARRAY_BUFFER_BINDING mismatch" << std::endl;
        device->DestroyBuffer(vertexBuffer);
        device->DestroyBuffer(indexBuffer);
        return false;
    }
    std::cout << "[OpenGL] GL_ARRAY_BUFFER_BINDING verified: " << boundBuffer << std::endl;

    std::cout << "[RHI] Binding index buffer with RHI BindIndexBuffer()..." << std::endl;
    if (!device->BindIndexBuffer(indexBuffer, IndexFormat::UInt16)) {
        std::cerr << "[RHI] BindIndexBuffer failed" << std::endl;
        device->DestroyBuffer(vertexBuffer);
        device->DestroyBuffer(indexBuffer);
        return false;
    }

    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundBuffer);
    auto* glIndexBuffer = dynamic_cast<Render::Backend::OpenGL::GLBuffer*>(indexBuffer);
    if (static_cast<GLuint>(boundBuffer) != glIndexBuffer->GetHandle()) {
        std::cerr << "[OpenGL] GL_ELEMENT_ARRAY_BUFFER_BINDING mismatch" << std::endl;
        device->DestroyBuffer(vertexBuffer);
        device->DestroyBuffer(indexBuffer);
        return false;
    }
    std::cout << "[OpenGL] GL_ELEMENT_ARRAY_BUFFER_BINDING verified: " << boundBuffer << std::endl;

    device->DestroyBuffer(vertexBuffer);
    device->DestroyBuffer(indexBuffer);
    std::cout << "[Test] Buffer Binding Test PASSED" << std::endl;
    return true;
}

int main() {
    std::cout << "=== RHI Buffer Test Suite ===" << std::endl;
    std::cout << "Using RHI to create buffers, verifying with OpenGL API" << std::endl;

    // 初始化 GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "RHI Buffer Test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

    // 创建 RHI 设备
    auto device = CreateOpenGLDevice();
    if (!device) {
        std::cerr << "Failed to create OpenGL device" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    std::cout << "OpenGL device created successfully" << std::endl;

    int passedTests = 0;
    int totalTests = 0;

    // 测试顶点缓冲
    totalTests++;
    if (TestVertexBuffer(device.get())) {
        passedTests++;
    }

    // 测试索引缓冲
    totalTests++;
    if (TestIndexBuffer(device.get())) {
        passedTests++;
    }

    // 测试常量缓冲
    totalTests++;
    if (TestConstantBuffer(device.get())) {
        passedTests++;
    }

    // 测试存储缓冲
    totalTests++;
    if (TestStorageBuffer(device.get())) {
        passedTests++;
    }

    // 测试输入布局
    totalTests++;
    if (TestInputLayout(device.get())) {
        passedTests++;
    }

    // 测试缓冲更新
    totalTests++;
    if (TestBufferUpdate(device.get())) {
        passedTests++;
    }

    // 测试缓冲拷贝
    totalTests++;
    if (TestBufferCopy(device.get())) {
        passedTests++;
    }

    // 测试 Map/Unmap
    totalTests++;
    if (TestBufferMapUnmap(device.get())) {
        passedTests++;
    }

    // 测试缓冲绑定
    totalTests++;
    if (TestBufferBinding(device.get())) {
        passedTests++;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passedTests << "/" << totalTests << std::endl;
    std::cout << "========================================" << std::endl;

    // 清理
    glfwDestroyWindow(window);
    glfwTerminate();

    return (passedTests == totalTests) ? 0 : 1;
}
