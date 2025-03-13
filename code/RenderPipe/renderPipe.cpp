#include "renderPipe.h"
#include <iostream>
#include <glad/glad.h>
#include "code/shader.h"

using namespace Render;

static void CheckFramebufferStatus(GLenum target = GL_FRAMEBUFFER){
    GLenum status = glCheckFramebufferStatus(target);
    if(status != GL_FRAMEBUFFER_COMPLETE){
        std::cout << "Framebuffer not complete!" << std::endl;
        exit(0);
    }
}

SimpleRenderPipe::SimpleRenderPipe(){
    Init();
}

void SimpleRenderPipe::Init(){
    // 创建深度贴图
    glGenFramebuffers(1, &_depthMapFBO);
    glGenTextures(1, &_depthMap);
    glBindTexture(GL_TEXTURE_2D, _depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, _depthMapWidth, _depthMapHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // 采样模式: 边缘截取,使用白色填充
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindFramebuffer(GL_FRAMEBUFFER, _depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _depthMap, 0);
    CheckFramebufferStatus();
    // 深度贴图不需要颜色缓冲区
    glDrawBuffer(GL_NONE);
    // 读取深度贴图不需要颜色缓冲区
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
}

void SimpleRenderPipe::Resize(unsigned int width, unsigned int height){
    _outBufferWidth = width;
    _outBufferHeight = height;
    //响应的缓冲区处理
}