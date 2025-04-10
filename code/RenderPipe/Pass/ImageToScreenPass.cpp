#include "ImageToScreenPass.h"

#include "code/shader.h"

using namespace Render;



void ImageToBuffer::Init(const PassConfig& ctx){
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    QuadVAO_ = VAO;

    screenShader_ = std::make_unique<ShaderProgram>("shaders/Final/screen.vs","shaders/Final/screen.fs");
}
void ImageToBuffer::SetConfig(const PassConfig& cfg){

}
void ImageToBuffer::Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList){
    if(texture_ > 0){   
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
        glViewport(0,0,screenWidth_,screenHeight_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture()
    }
}
void ImageToBuffer::Release(){

}