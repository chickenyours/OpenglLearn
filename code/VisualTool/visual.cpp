#include "visual.h"
#include <iostream>
#include <string>
#include <glad/glad.h>


using namespace Render;

BufferChunk::BufferChunk(BufferChunk&& other) noexcept
    : vao(other.vao), vbo(other.vbo), maxCount(other.maxCount), count(other.count) {
    other.vao = 0;
    other.vbo = 0;
    std::cout << "BufferChunk 发送移动" << std::endl;
}

BufferChunk::BufferChunk(size_t maxCount):maxCount(maxCount) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, maxCount * elementSize , nullptr, GL_DYNAMIC_DRAW);

    // 假设 layout(0) = pos, layout(1) = color, 全是 vec3
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) * 2, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) * 2, (void*)(sizeof(glm::vec3)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0); // 解除绑定，避免其他代码误操作
}

void BufferChunk::Update(int index, int num, const VisualElement* data) {
    if (index + num >= count || index + num >= maxCount) {
        std::cout << "Marker: 越界绘制无效 index: " << index << " count: " << count << std::endl;
        return;
    }
    if(num > 0){
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, index * elementSize, elementSize * num, data);
    }
}

int BufferChunk::Add(int num, const VisualElement* data){
    if (count + num > maxCount) {
        std::cout << "Add 超出最大缓冲限制: " << maxCount << std::endl;
        return -1;
    }
    Update(count, num, data);
    count += num;
    return count-num;
}

BufferChunk::~BufferChunk(){
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    std::cout << "BufferChunk被销毁" << std::endl;
}

Marker::Marker(size_t maxPoints, size_t maxLines):lineBuffer(maxLines),pointBuffer(maxPoints), _shaderProgram("./shaders/Final/Visual/base_visual.vs","./shaders/Final/Visual/single_color.fs"){
    glEnable(GL_PROGRAM_POINT_SIZE);
    pointBufferVAO = pointBuffer.GetVAO();
    lineBufferVAO = lineBuffer.GetVAO();
}

// 批量添加点
int Marker::AddPoint(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& colors) {
    if (positions.size() != colors.size()) {
        std::cerr << "AddPoint: positions 和 colors 长度不匹配" << std::endl;
        return -1;
    }
    
    std::vector<VisualElement> elements;
    for (size_t i = 0; i < positions.size(); ++i) {
        elements.push_back({positions[i], colors[i]});
    }

    return pointBuffer.Add(elements.size(), elements.data());
}

// 批量更新点
void Marker::SetPoint(int index, const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& colors) {
    if (positions.size() != colors.size()) {
        std::cerr << "SetPoint: positions 和 colors 长度不匹配" << std::endl;
        return;
    }

    std::vector<VisualElement> elements;
    for (size_t i = 0; i < positions.size(); ++i) {
        elements.push_back({positions[i], colors[i]});
    }

    pointBuffer.Update(index, elements.size(), elements.data());
}

// 批量添加线
int Marker::AddLine(const std::vector<glm::vec3>& startPositions, const std::vector<glm::vec3>& startColors, 
    const std::vector<glm::vec3>& endPositions, const std::vector<glm::vec3>& endColors) {

    if (startPositions.size() != startColors.size() || startPositions.size() != endPositions.size() || endPositions.size() != endColors.size()) {
        std::cerr << "AddLine: 所有输入数组长度必须匹配" << std::endl;
        return -1;
    }

    std::vector<VisualElement> elements;
    elements.reserve(startPositions.size() * 2);
    for (size_t i = 0; i < startPositions.size(); ++i) {
        elements.push_back({startPositions[i], startColors[i]});
        elements.push_back({endPositions[i], endColors[i]});
    }

    return lineBuffer.Add(elements.size(), elements.data());
}
// 批量更新线
void Marker::SetLine(int index, const std::vector<glm::vec3>& startPositions, const std::vector<glm::vec3>& startColors, 
    const std::vector<glm::vec3>& endPositions, const std::vector<glm::vec3>& endColors) {
    if (startPositions.size() != startColors.size() || startPositions.size() != endPositions.size() || endPositions.size() != endColors.size()) {
        std::cerr << "SetLine: 所有输入数组长度必须匹配" << std::endl;
        return;
    }

    std::vector<VisualElement> elements;
    elements.reserve(startPositions.size() * 2);
    for (size_t i = 0; i < startPositions.size(); ++i) {
        elements.push_back({startPositions[i], startColors[i]});
        elements.push_back({endPositions[i], endColors[i]});
    }

    lineBuffer.Update(index, elements.size(), elements.data());
}

void Marker::Draw() {
    _shaderProgram.Use();
    ShaderUmatf4(_shaderProgram,"model",_model);
    ShaderUmatf4(_shaderProgram,"view",_view);
    ShaderUmatf4(_shaderProgram,"projection",_perspective);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(pointBufferVAO);
    glDrawArrays(GL_POINTS,0,pointBuffer.GetElementCount());
    glBindVertexArray(lineBufferVAO);
    glDrawArrays(GL_LINES,0,lineBuffer.GetElementCount());
    glEnable(GL_DEPTH_TEST);
}

void Marker::SetPointSize(float size) {
    glPointSize(size);
}

void Marker::SetLineWidth(float width) {
    glLineWidth(width);
}

void Marker::SetMatrix(glm::mat4 perspective, glm::mat4 view, glm::mat4 model){
    _model = model;
    _perspective = perspective;
    _view = view;
}

Marker::~Marker() {
    std::cout<< "Maker被销毁" << std::endl;
}

