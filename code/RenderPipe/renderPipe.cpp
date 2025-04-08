#include "renderPipe.h"
#include <iostream>
#include <glad/glad.h>
#include "code/shader.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Render;

SimpleRenderPipe::SimpleRenderPipe(){
    std::cout<<"创建渲染管线对象: SimpleRenderPipe"<<std::endl;
}

void SimpleRenderPipe::Addmesh(Mesh* mesh){
    meshQueue.push(mesh);
}

void SimpleRenderPipe::Push(const RenderItem& renderItem){
    renderItemQueue_.push(renderItem);
}

void SimpleRenderPipe::Render(){
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glClearColor(0.25f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    //静态渲染管线算法
    // while(!meshQueue.empty()){
    //     Mesh* mesh = meshQueue.front();
    //     meshQueue.pop();
    //     glBindVertexArray(mesh->VAO);
    //     mesh->_material->BindAllTexture();
    //     mesh->_material->SetMaterialPropertiesToShader();
    //     glDrawElements(GL_TRIANGLES, mesh->indicesSize, GL_UNSIGNED_INT, 0);
    // }

    while(!renderItemQueue_.empty()){
        RenderItem& renderItem = renderItemQueue_.front();
        glBindVertexArray(renderItem.mesh->VAO);
        renderItem.material->BindAllTexture();
        renderItem.material->SetMaterialPropertiesToShader();
        // 设定shaderProgram的一些值
        ShaderProgram* materialShader = renderItem.material->shaderProgram.get(); 
        ShaderUmatf4(*materialShader,"model",renderItem.model);
        glDrawElements(GL_TRIANGLES, renderItem.mesh->indicesSize, GL_UNSIGNED_INT, 0);
        renderItemQueue_.pop();
    }

}

SimpleRenderPipe::~SimpleRenderPipe(){
    std::cout<<"销毁渲染管线对象: SimpleRenderPipe"<<std::endl;
}