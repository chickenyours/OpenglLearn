#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define STB_IMAGE_IMPLEMENTATION    //是使预编译器include相关功能的代码
#include "stb_image.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_M) {
            // 按 M 键最大化窗口
            glfwMaximizeWindow(window);
            std::cout << "Window maximized" << std::endl;
        }
        else if (key == GLFW_KEY_R) {
            // 按 R 键恢复窗口
            glfwRestoreWindow(window);
            std::cout << "Window restored" << std::endl;
        }
    }
}
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;

float vertices[] = {
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f
};
float normalvertices[] = {
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
     0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f, 

    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,

    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
     0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,

    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
};

float materialvertices[] = {
    // 顶点坐标        纹理坐标    法线坐标
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,  0.0f,  0.0f, -1.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f,  0.0f,  0.0f, -1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,  0.0f,  0.0f, -1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,  0.0f,  0.0f, -1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,  0.0f,  0.0f, -1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,  0.0f,  0.0f, -1.0f,

    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,  0.0f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,  0.0f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,  0.0f,  0.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,  0.0f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,  0.0f,  0.0f, 1.0f,

    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, -1.0f,  0.0f,  0.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, -1.0f,  0.0f,  0.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, -1.0f,  0.0f,  0.0f,

     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,  1.0f,  0.0f,  0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 0.0f,  1.0f,  0.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,  1.0f,  0.0f,  0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,  0.0f, -1.0f,  0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,  0.0f, -1.0f,  0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,  0.0f, -1.0f,  0.0f,

    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,  0.0f,  1.0f,  0.0f
};

float texWidth = 5.0;
float texHeight = 5.0;
float panelWidth = 200.0;
float panelHeight = 200.0;
float panel[] = {
    -panelWidth/2.0f,0.0f,-panelHeight/2.0f,   0.0f,0.0f,                           0.0f,1.0f,0.0f,
    panelWidth/2.0f,0.0f,-panelHeight/2.0f,    panelWidth/texWidth,0.0f,            0.0f,1.0f,0.0f,
    -panelWidth/2.0f,0.0f,panelHeight/2.0f,     0.0f,panelHeight/texHeight,          0.0f,1.0f,0.0f,

    panelWidth/2.0f,0.0f,-panelHeight/2.0f,    panelWidth/texWidth,0.0f,            0.0f,1.0f,0.0f,
    -panelWidth/2.0f,0.0f,panelHeight/2.0f,     0.0f,panelHeight/texHeight,          0.0f,1.0f,0.0f,
    panelWidth/2.0f,0.0f,panelHeight/2.0f,      panelHeight/texHeight,panelHeight/texHeight,    0.0f,1.0f,0.0f,
};


glm::vec3 cubePositions[] = {
    glm::vec3( 0.0f,  0.0f,  0.0f), 
    glm::vec3( 2.0f,  5.0f, -15.0f), 
    glm::vec3(-1.5f, -2.2f, -2.5f),  
    glm::vec3(-3.8f, -2.0f, -12.3f),  
    glm::vec3( 2.4f, -0.4f, -3.5f),  
    glm::vec3(-1.7f,  3.0f, -7.5f),  
    glm::vec3( 1.3f, -2.0f, -2.5f),  
    glm::vec3( 1.5f,  2.0f, -2.5f), 
    glm::vec3( 1.5f,  0.2f, -1.5f), 
    glm::vec3(-1.3f,  1.0f, -1.5f)  
};

//camera
glm::vec3 camPos(0.0,100.0,0.0);
glm::vec3 camFront(0.0f,0.0f,-1.0f);
glm::vec3 camUp(0.0f,1.0f,0.0f);
float pitch = 0.0f;
float yaw = -90.0f;
float roll = 0.0f;
float lastX = SCR_WIDTH/2;
float lastY = SCR_HEIGHT/2;
float sensitivity = 0.1f;
float camfixSpeed = 2.5;
float camSpeed = 0;


float deltaTime = 0.0f;
float latestTime = 0.0f;

float rotation = 0.0;

void mouse_callback(GLFWwindow *window,double xpos,double ypos){
    
    float xOffset =  xpos - lastX;
    float yOffset =  lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    yaw += sensitivity * xOffset;
    pitch += sensitivity * yOffset;
    if(pitch>89.0f)pitch = 89.0f;
    if(pitch<-89.0f)pitch = -89.0f;
}

int main()
{
    
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }    
    glEnable(GL_DEPTH_TEST);  //深度测试
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT); // 初始化视口
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); 
    glfwSetCursorPos(window,SCR_WIDTH/2,SCR_HEIGHT/2);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback); // 注册缓冲区size变化事件
    glfwSetCursorPosCallback(window,mouse_callback);
    glfwSetKeyCallback(window, keyCallback);
    //初始化
    Opengl::Shader objectvs(GL_VERTEX_SHADER,"20240723/toy.vs");
    Opengl::Shader objectfs(GL_FRAGMENT_SHADER,"20240723/ambient.fs");
    Opengl::Shader lightCube(GL_FRAGMENT_SHADER,"20240723/lightsource.fs");
    Opengl::Shader diffusionvs(GL_VERTEX_SHADER,"20240723/diffusion.vs");
    Opengl::Shader diffusionfs(GL_FRAGMENT_SHADER,"20240723/diffusion.fs");
    Opengl::Shader specularvs(GL_VERTEX_SHADER,"20240723/speculargouraud.vs");
    Opengl::Shader specularfs(GL_FRAGMENT_SHADER,"20240723/speculargouraud.fs");
    Opengl::Shader materialvs(GL_VERTEX_SHADER,"20240723/material.vs");
    Opengl::Shader materialfs(GL_FRAGMENT_SHADER,"20240723/material.fs");
    Opengl::Shader materiallightmapvs(GL_VERTEX_SHADER,"20240723/material.vs");
    Opengl::Shader materiallightmapfs(GL_FRAGMENT_SHADER,"20240723/pointlight.fs");
    Opengl::Shader flashLight(GL_FRAGMENT_SHADER,"20240723/flashlight.fs");
    Opengl::Shader multlightmodelvs(GL_VERTEX_SHADER,"20240723/multlightmodel.vs");
    Opengl::Shader multlightmodelfs(GL_FRAGMENT_SHADER,"20240723/multlightmodelcontainer.fs");
    Opengl::ShaderProgram objdectShaderProgram(&objectvs,&objectfs);
    Opengl::ShaderProgram lightCubeShaderProgram(&objectvs,&lightCube);
    Opengl::ShaderProgram diffusionShaderProgram(&diffusionvs,&diffusionfs);
    Opengl::ShaderProgram specularShaderProgram(&specularvs,&specularfs);
    Opengl::ShaderProgram materialShaderProgram(&materialvs,&materialfs);
    Opengl::ShaderProgram materiallightmapShaderProgram(&materiallightmapvs,&materiallightmapfs);
    Opengl::ShaderProgram flashLightShaderProgram(&materiallightmapvs,&flashLight);
    Opengl::ShaderProgram multlightmodelShaderProgram(&multlightmodelvs,&multlightmodelfs);
    glm::vec3 objectPos(6.0f,0.0f,0.0f);
    glm::vec3 lightPos(0.0f,0.0f,10.0f);
    glm::vec3 diffusionPos(-6.0,0.0,0.0);
    glm::vec3 specularPos(-0.0f,0.0,0.0);
    glm::vec3 materialPos(-0.0,-0.0,-0.0);
    glm::vec3 materiallightmapPos(-0.0,-0.0,-0.0);
    glm::vec3 panelPos(0.0f,-3.0f,0.0f);
    glm::mat4 model(1.0f);
    glm::mat4 objectModel;
    glm::mat4 lightModel;
    glm::mat4 diffusionModel;
    glm::mat4 specularModel;
    glm::mat4 materialModel;
    glm::mat4 materiallightmapModel;
    glm::mat4 panelModel = glm::translate(model,panelPos);
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 small = glm::scale(model,glm::vec3(0.2f,0.2f,0.2f));
    objectModel     =   glm::translate(model,objectPos) * glm::scale(model,glm::vec3(5.0f,5.0f,5.0f));
    lightModel      =   glm::translate(model,lightPos) * glm::scale(model,glm::vec3(0.05f,0.05f,0.05f));
    diffusionModel  =   glm::translate(model,diffusionPos) * glm::scale(model,glm::vec3(5.0f,5.0f,5.0f));
    specularModel   =   glm::translate(model,specularPos) * glm::scale(model,glm::vec3(5.0f,5.0f,5.0f));
    materialModel   =   glm::translate(model,materialPos) * glm::scale(model,glm::vec3(5.0f,5.0f,5.0f));
    materiallightmapModel = glm::translate(model,materiallightmapPos) * glm::scale(model,glm::vec3(5.0f,5.0f,5.0f));
    view            =   glm::lookAt(camPos,camPos+camFront,camUp);
    projection      =   glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/(float)SCR_HEIGHT,0.1f,500.0f);

    glm::vec3 lightColor(1.0,1.0,1.0);

    objdectShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(objdectShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(objectModel));
    glUniformMatrix4fv(glGetUniformLocation(objdectShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(objdectShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(objdectShaderProgram.getShaderProgramID(),"lightColor"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(objdectShaderProgram.getShaderProgramID(),"objectColor"),1.0f, 0.5f, 0.31f);
   
    flashLightShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(lightModel));
    glUniformMatrix4fv(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"color"),lightColor.x,lightColor.y,lightColor.z);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.position"),lightPos.x,lightPos.y,lightPos.z);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.lightColor"),lightColor.x,lightColor.y,lightColor.z);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.ambientStrength"),0.1f, 0.1f, 0.1f);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.diffusionStrength"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.specularStrength"),1.0f, 1.0f, 1.0f);
    glUniform1f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.constant"),0.1);
    glUniform1f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.linear"),0.05);
    glUniform1f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.quadratic"),0.01);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.lightDirection"),camFront.x,camFront.y,camFront.z);
    glUniform1f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.cutoff"),glm::cos(glm::radians(7.5)));
    glUniform1f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.outCutoff"),glm::cos(glm::radians(12.5)));
    glUniform1i(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.cookie"),2);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"material.objectColor"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"material.ambientStrength"),0.1f, 0.1f, 0.1f);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"material.diffusionStrength"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"material.specularStrength"),0.7f, 0.7f, 0.7f);
    glUniform1i(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"material.diffuse"),0);
    glUniform1i(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"material.specular"),1);

    glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);

    lightCubeShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(lightModel));
    glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"color"),lightColor.x,lightColor.y,lightColor.z);

    diffusionShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
    glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"lightPos"),lightPos.x,lightPos.y,lightPos.z);
    glUniform3f(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"lightColor"),1.0f, 0.5f, 0.31f);
    glUniform3f(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"objectColor"),1.0f, 0.5f, 0.31f);

    specularShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(specularModel));
    glUniformMatrix4fv(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"lightColor"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);
    glUniform3f(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"lightPos"),lightPos.x,lightPos.y,lightPos.z);

    materialShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(materialModel));
    glUniformMatrix4fv(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"material.objectColor"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"material.ambientStrength"),0.1f, 0.1f, 0.1f);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"material.diffusionStrength"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"material.specularStrength"),0.7f, 0.7f, 0.7f);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"light.position"),lightPos.x,lightPos.y,lightPos.z);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"light.lightColor"),lightColor.x,lightColor.y,lightColor.z);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"light.ambientStrength"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"light.diffusionStrength"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"light.specularStrength"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);

    materiallightmapShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(materiallightmapModel));
    glUniformMatrix4fv(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.position"),lightPos.x,lightPos.y,lightPos.z);
    glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.lightColor"),lightColor.x,lightColor.y,lightColor.z);
    glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.ambientStrength"),0.1f, 0.1f, 0.1f);
    glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.diffusionStrength"),1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.specularStrength"),1.0f, 1.0f, 1.0f);
    glUniform1f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.constant"),1.0);
    glUniform1f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.linear"),0.3);
    glUniform1f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.quadratic"),2.0);
    glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);
    glUniform1i(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"material.diffuse"),0);
    glUniform1i(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"material.specular"),1);
    glUniform1i(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"material.emission"),2);

    float pointLightRadian = 2.0;
    glm::vec3 pointLightPositions[] = {
        glm::vec3(pointLightRadian*glm::cos(glm::radians(0.0)),1.0,pointLightRadian*glm::sin(glm::radians(0.0))),
        glm::vec3(pointLightRadian*glm::cos(glm::radians(120.0)),1.0,pointLightRadian*glm::sin(glm::radians(120.0))),
        glm::vec3(pointLightRadian*glm::cos(glm::radians(240.0)),1.0,pointLightRadian*glm::sin(glm::radians(240.0))),
    };
    float constant = 0.1;
    float linear = 0.05;
    float quadratic = 0.01;
    multlightmodelShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(materiallightmapModel));
    glUniformMatrix4fv(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform1i(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"material.diffuse"),0);
    glUniform1i(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"material.specular"),1);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"material.shininess"),32.0);
    //glUniform1i(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"material.emission"),2);
    //directionlight
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"dlight.color"),0.25, 0.35, 0.55);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"dlight.direction"),0.23, -0.45, 0.86);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"dlight.ambient"),0.1,0.1,0.1);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"dlight.diffuse"),0.3,0.3,0.3);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"dlight.specular"),0.5,0.5,0.5);
    //PointLight[0]
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].color"),0.0,0.0,1.0);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].pos"),pointLightPositions[0].x,pointLightPositions[0].y,pointLightPositions[0].z);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].constant"),constant);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].linear"),linear);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].quadratic"),quadratic);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].ambient"),0.1,0.1,0.1);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].diffuse"),0.5,0.5,0.5);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].specular"),1.0,1.0,1.0);
    //PointLight[1]
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].color"),0.0,1.0,0.0);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].pos"),pointLightPositions[1].x,pointLightPositions[1].y,pointLightPositions[1].z);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].constant"),constant);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].linear"),linear);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].quadratic"),quadratic);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].ambient"),0.1,0.1,0.1);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].diffuse"),0.5,0.5,0.5);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].specular"),1.0,1.0,1.0);
    //PointLight[2]
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].color"),1.0,0.0,0.0);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].pos"),pointLightPositions[2].x,pointLightPositions[2].y,pointLightPositions[2].z);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].constant"),constant);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].linear"),linear);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].quadratic"),quadratic);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].ambient"),0.1,0.1,0.1);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].diffuse"),0.5,0.5,0.5);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].specular"),1.0,1.0,1.0);
    //SpotLight
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.color"),1.0,1.0,1.0);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.pos"),10.0,5.0,0.0);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.direction"),0.0,-1.0,0.0);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.cutOff"),glm::cos(glm::radians(60.0)));
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.outerCutOff"),glm::cos(glm::radians(75.0)));
    glUniform1i(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.cookie"),2);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.constant"),constant);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.linear"),linear);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.quadratic"),quadratic);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.ambient"),0.1,0.1,0.1);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.diffuse"),0.5,0.5,0.5);
    glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.specular"),2.0,2.0,2.0);
    glUniform1i(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.cookie"),2);
    glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.degree"),0.0f);
   
    
    unsigned int VAO,VBO;
    glGenVertexArrays(1,&VAO);
    glBindVertexArray(VAO);
    glGenBuffers(1,&VBO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    unsigned int LVAO,LVBO;
    glGenVertexArrays(1,&LVAO);
    glBindVertexArray(LVAO);
    glGenBuffers(1,&LVBO);
    glBindBuffer(GL_ARRAY_BUFFER,LVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(normalvertices),normalvertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    unsigned int MVAO,MVBO;
    glGenVertexArrays(1,&MVAO);
    glBindVertexArray(MVAO);
    glGenBuffers(1,&MVBO);
    glBindBuffer(GL_ARRAY_BUFFER,MVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(materialvertices),materialvertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(5*sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    unsigned int PanalVAO,PanalVBO;
    glGenVertexArrays(1,&PanalVAO);
    glBindVertexArray(PanalVAO);
    glGenBuffers(1,&PanalVBO);
    glBindBuffer(GL_ARRAY_BUFFER,PanalVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(panel),panel,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(5*sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    unsigned int texture1; 
    glGenTextures(1,&texture1);
    glBindTexture(GL_TEXTURE_2D,texture1);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    int width,height,nrChannels;
    unsigned char* data = stbi_load("./images/container2.png",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
        glfwTerminate();
        return 0;
    }
    
    unsigned int texture2; 
    glGenTextures(1,&texture2);
    glBindTexture(GL_TEXTURE_2D,texture2);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    data = stbi_load("./images/container2_specular.png",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
        glfwTerminate();
        return 0;
    }

    unsigned int texture3; 
    glGenTextures(1,&texture3);
    glBindTexture(GL_TEXTURE_2D,texture3);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    data = stbi_load("./images/matrix.jpg",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
        glfwTerminate();
        return 0;
    }

    unsigned int texture4; 
    glGenTextures(1,&texture4);
    glBindTexture(GL_TEXTURE_2D,texture4);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    data = stbi_load("./images/awesomeface.png",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
        glfwTerminate();
        return 0;
    }

    //black and white
    unsigned int texture5; 
    glGenTextures(1,&texture5);
    glBindTexture(GL_TEXTURE_2D,texture5);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    data = stbi_load("./images/blackandwhite.png",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
        glfwTerminate();
        return 0;
    }
    unsigned int texture6; 
    glGenTextures(1,&texture6);
    glBindTexture(GL_TEXTURE_2D,texture6);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    data = stbi_load("./images/hh.png",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
        glfwTerminate();
        return 0;
    }

    while (!glfwWindowShouldClose(window))
    {
        float currentTime = glfwGetTime();
        deltaTime = currentTime - latestTime;
        latestTime = deltaTime + latestTime;
        processInput(window);
        float camMove;
        if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)){
            camSpeed += deltaTime*3;
            camMove  = deltaTime*(camSpeed+camfixSpeed*4);
        }
        else{
            camSpeed = 0.0;
            camMove = deltaTime * camfixSpeed;
        }
        if(glfwGetKey(window,GLFW_KEY_W)){
        camPos += camFront*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_A)){
            camPos -= glm::normalize(glm::cross(camFront,camUp))*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_S)){
            camPos -= camFront*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_D)){
            camPos += glm::normalize(glm::cross(camFront,camUp))*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_SPACE)){
            camPos.y += camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_RIGHT)){
            diffusionPos += glm::vec3(1.0*deltaTime,0.0,0.0);
            diffusionModel = glm::translate(model,glm::vec3(1.0*deltaTime,0.0,0.0)) * diffusionModel;
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_LEFT)){
            diffusionPos += glm::vec3(-1.0*deltaTime,0.0,0.0);
            diffusionModel = glm::translate(model,glm::vec3(-1.0*deltaTime,0.0,0.0)) * diffusionModel;
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_UP)){
            diffusionPos += glm::vec3(0.0,0.0,1.0*deltaTime);
            diffusionModel = glm::translate(model,glm::vec3(0.0,0.0,1.0*deltaTime)) * diffusionModel;
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_DOWN)){
            diffusionPos += glm::vec3(0.0,0.0,-1.0*deltaTime);
            diffusionModel = glm::translate(model,glm::vec3(0.0,0.0,-1.0*deltaTime)) * diffusionModel;
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)){
            diffusionPos += glm::vec3(0.0,1.0*deltaTime,0.0);
            diffusionModel = glm::translate(model,glm::vec3(0.0,1.0*deltaTime,0.0)) * diffusionModel;
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_RIGHT_CONTROL)){
            diffusionPos += glm::vec3(0.0,-1.0*deltaTime,0.0);
            diffusionModel = glm::translate(model,glm::vec3(0.0,-1.0*deltaTime,0.0)) * diffusionModel;
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_RIGHT_ALT)){
            
            diffusionModel = glm::rotate(model,glm::radians(60*deltaTime),glm::vec3(0.0,0.0,1.0)) * diffusionModel;
            
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_U)){
            diffusionModel = glm::scale(diffusionModel,glm::vec3(1.0,1+deltaTime,1.0));
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_I)){
            diffusionModel = glm::scale(diffusionModel,glm::vec3(1.0,1-deltaTime,1.0));
            diffusionShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(diffusionModel));
        }
        if(glfwGetKey(window,GLFW_KEY_Q)){
            rotation -= deltaTime * 60.0f;
        }
        if(glfwGetKey(window,GLFW_KEY_E)){
            rotation += deltaTime * 60.0f;
        }
        if(glfwGetKey(window,GLFW_KEY_Y)){
            glm::mat4 rotates = glm::rotate(model,glm::radians(deltaTime*60),glm::vec3(1,1,1));
            materialModel = rotates * materialModel;
        }
        //float radian = 8.0f;
        // glm::vec3 newPos = glm::vec3(0.0f,glm::sin(glm::radians(rotation))*radian,glm::cos(glm::radians(rotation))*radian);
        // lightModel =   glm::translate(model,newPos)  * glm::translate(model,-lightPos) * lightModel;
        // lightCubeShaderProgram.Use();
        // lightPos = newPos;
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 
        view = glm::lookAt(camPos,camFront+camPos,camUp);
        //有些输入处理需要依赖一些变量
        if(glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS){
            lightPos = camPos; //- camFront * 0.2f;
            lightModel = glm::translate(model,lightPos) * glm::scale(model,glm::vec3(0.05f,0.05f,0.05f));
            lightCubeShaderProgram.Use();
            glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(lightModel));
            flashLightShaderProgram.Use();
            glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.lightDirection"),camFront.x,camFront.y,camFront.z);
        }
       
        //渲染
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // glBindVertexArray(VAO);
        // objdectShaderProgram.Use();
        // glUniformMatrix4fv(glGetUniformLocation(objdectShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        // glDrawArrays(GL_TRIANGLES,0,36);

        //lightcube
        // lightCubeShaderProgram.Use();
        // glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        // glDrawArrays(GL_TRIANGLES,0,36);

        // glBindVertexArray(LVAO);
        // diffusionShaderProgram.Use();
        // glUniformMatrix4fv(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        // glUniform3f(glGetUniformLocation(diffusionShaderProgram.getShaderProgramID(),"lightPos"),lightPos.x,lightPos.y,lightPos.z);
        // glDrawArrays(GL_TRIANGLES,0,36);
        // specularShaderProgram.Use();
        // glUniformMatrix4fv(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        // glUniform3f(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);
        // glUniform3f(glGetUniformLocation(specularShaderProgram.getShaderProgramID(),"lightPos"),lightPos.x,lightPos.y,lightPos.z);
        // glDrawArrays(GL_TRIANGLES,0,36);

        // glBindVertexArray(MVAO);
        // materialShaderProgram.Use();
        // glActiveTexture(GL_TEXTURE0);
        // glBindTexture(GL_TEXTURE_2D,texture1);
        // glUniformMatrix4fv(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        // glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);
        // glUniform3f(glGetUniformLocation(materialShaderProgram.getShaderProgramID(),"light.position"),lightPos.x,lightPos.y,lightPos.z);
        

        glBindVertexArray(MVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,texture2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D,texture4);
        //glActiveTexture(GL_TEXTURE2);
        //glBindTexture(GL_TEXTURE_2D,texture3);
        // materiallightmapShaderProgram.Use();
        // glUniformMatrix4fv(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(materialModel));
        // glUniformMatrix4fv(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        // glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);
        // glUniform3f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"light.position"),lightPos.x,lightPos.y,lightPos.z);
        //glUniform1f(glGetUniformLocation(materiallightmapShaderProgram.getShaderProgramID(),"time"),glfwGetTime());

        // flashLightShaderProgram.Use();
        // glUniformMatrix4fv(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(materialModel));
        // glUniformMatrix4fv(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        // glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);
        // glUniform3f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"light.position"),lightPos.x,lightPos.y,lightPos.z);
        // glUniform1f(glGetUniformLocation(flashLightShaderProgram.getShaderProgramID(),"time"),currentTime);
        pointLightPositions[0] = glm::vec3(pointLightRadian*glm::cos(glm::radians(currentTime*30)),1.0,pointLightRadian*glm::sin(glm::radians(currentTime*30)));
        pointLightPositions[1] = glm::vec3(pointLightRadian*glm::cos(glm::radians(currentTime*30+120)),1.0,pointLightRadian*glm::sin(glm::radians(currentTime*30+120)));
        pointLightPositions[2] = glm::vec3(pointLightRadian*glm::cos(glm::radians(currentTime*30+240)),1.0,pointLightRadian*glm::sin(glm::radians(currentTime*30+240)));
        lightCubeShaderProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(glm::translate(model,pointLightPositions[0])*small));
        glUniform3f(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"color"),0.0,0.0,1.0);
        glDrawArrays(GL_TRIANGLES,0,36);
        glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(glm::translate(model,pointLightPositions[1])*small));
        glUniform3f(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"color"),0.0,1.0,0.0);
        glDrawArrays(GL_TRIANGLES,0,36);
        glUniformMatrix4fv(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(glm::translate(model,pointLightPositions[2])*small));
        glUniform3f(glGetUniformLocation(lightCubeShaderProgram.getShaderProgramID(),"color"),1.0,0.0,0.0);
        glDrawArrays(GL_TRIANGLES,0,36);

        multlightmodelShaderProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[0].pos"),pointLightPositions[0].x,pointLightPositions[0].y,pointLightPositions[0].z);
        glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[1].pos"),pointLightPositions[1].x,pointLightPositions[1].y,pointLightPositions[1].z);
        glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"plight[2].pos"),pointLightPositions[2].x,pointLightPositions[2].y,pointLightPositions[2].z);

        glUniform3f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"viewPos"),camPos.x,camPos.y,camPos.z);
        glUniform1f(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"slight.degree"),currentTime*60.0f);
        
        for(int i = 0;i<10;i++){
            glm::mat4 model(1.0f);
            model = glm::translate(model,cubePositions[i]);
            model = glm::rotate(model,glm::radians(i*20.0f+rotation),glm::vec3(0.3f,0.5f,1.0f));
            glUniformMatrix4fv(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES,0,36);
        }
        glBindVertexArray(PanalVAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture5);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,texture5);
        glUniformMatrix4fv(glGetUniformLocation(multlightmodelShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(panelModel));
        glDrawArrays(GL_TRIANGLES,0,6);
        //结束
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

   
    glfwTerminate();
    return 0;
}


void processInput(GLFWwindow *window)
{
    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS and glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }
}


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}