#include "renderPipe.h"
#include <iostream>
#include <glad/glad.h>
#include "code/shader.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Render;

SimpleRenderPipe::SimpleRenderPipe(){

}

void SimpleRenderPipe::Addmesh(Mesh* mesh){
    meshQueue.push(mesh);
}

void SimpleRenderPipe::Render(){
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glClearColor(0.25f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    //静态渲染管线算法
    if(!meshQueue.empty()){
        Mesh* mesh = meshQueue.front();
        meshQueue.pop();
        glBindVertexArray(mesh->VAO);
        mesh->_material->BindAllTexture();
        mesh->_material->SetShaderParams();
        glDrawElements(GL_TRIANGLES, mesh->indicesSize, GL_UNSIGNED_INT, 0);
    }
}