#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


#include "code/shader.h"
#include "code/Model/model_animation.h"
#include "code/Material/material.h"
#include "code/Model/animator.h"
#include "code/VisualTool/visual.h"
#include "code/RenderPipe/simpleRenderPipe.h"
#include "code/Camera/camera.h"
#include "code/Config/config.h"
#include "code/RenderPipe/RenderContext/RenderPipeConfig.h" 

#define print(msg) std::cout<<(msg)<<std::endl 



//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods); 
//camera
glm::vec3 camPos(0.0,0.0,3.0);
glm::vec3 camFront(0.0f,0.0f,-1.0f);
glm::vec3 camUp(0.0f,1.0f,0.0f);
float pitch = 0.0f;
float yaw = -90.0f;
float roll = 0.0f;
float lastX = SCR_WIDTH/2;
float lastY = SCR_HEIGHT/2;
float sensitivity = 5.0f;
float camfixSpeed = 2.5;
float camSpeed = 0;

Render::Camera cam;

Render::SimpleRenderPipe renderPipe;

//time
float deltaTime = 0.0f;
float latestTime = 0.0f;

//全局变量代码写在这里:

int main()
{
    //初始化
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    //创建对话窗口
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    //获取Opengl上下文
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }    
    //设置Opengl属性
    glEnable(GL_DEPTH_TEST);                                                    //深度测试
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);                                    //初始化视口
    //设置对话窗口属性
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);                //隐藏鼠标
    glfwSetCursorPos(window,SCR_WIDTH/2,SCR_HEIGHT/2);                          //设定鼠标初始位置
    //注册基本事件
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);          
    glfwSetCursorPosCallback(window,mouse_callback);
    glfwSetKeyCallback(window, keyCallback);
    //初始化的代码写在这里：
    const glm::mat4 model(1.0f);
    glm::mat4 view          = glm::lookAt(camPos,camPos+camFront,camUp);
    glm::mat4 projection    = glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/(float)SCR_HEIGHT,0.1f,500.0f);

    //渲染管线 

    Render::RenderPipeConfig cfg;
    cfg.targetBufferWidth = SCR_WIDTH;
    cfg.targetBufferHeight = SCR_HEIGHT;
    renderPipe.Init(cfg);
    renderPipe.SetCamera(&cam);

   
    Render::Model m("./myModelsConfigs/Oil_barrel.json");

    Render::Model cube("./myModelsConfigs/base_models/cube.json");
    
    Render::Model plane("./myModelsConfigs/base_models/plane.json");

    plane.Print();

    //循环
    while (!glfwWindowShouldClose(window))
    {
        //get currentTime & deltaTime
        float currentTime = glfwGetTime();
        deltaTime = currentTime - latestTime;
        latestTime = deltaTime + latestTime;
        //处理输入
        processInput(window);
        cam.Update();
        //逻辑处理代码写在这里：
        Render::Material::GlobalMat4ParameterMap["projection"] = cam.GetProjectionMatrix();
        Render::Material::GlobalMat4ParameterMap["view"] = cam.GetViewMatrix();
        Render::Material::GlobalVec3ParameterMap["viewPos"] = cam.GetPosition();
        Render::Material::GlobalFloatParameterMap["iTime"] = currentTime;
        cube.model = glm::scale(model,glm::vec3(0.5));
        cube.CommitMeshToRenderPipe(&renderPipe);
        cube.model = glm::scale(glm::translate(model,glm::vec3(3.0)),glm::vec3(0.5));
        cube.CommitMeshToRenderPipe(&renderPipe);
        plane.model = glm::scale(glm::translate(model,glm::vec3(0.0,-5.0,0.0)),glm::vec3(50.0));
        plane.CommitMeshToRenderPipe(&renderPipe);
        renderPipe.RenderCall();
        glfwSwapBuffers(window);                                //交换缓冲
        glfwPollEvents();                                       //事件响应
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
    //摄像机变速
    if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)){
        camSpeed += camSpeed * 0.05f * deltaTime + deltaTime * 3.0f;
    }
    else{
        camSpeed = 1.0;
    }
    float camMove = deltaTime * camSpeed;
    const glm::vec3& caf = cam.GetCameraFrontRef(); 
    const glm::vec3& caup = cam.GetCameraUpRef();
    //摄像机移动输入处理
    if(glfwGetKey(window,GLFW_KEY_W)){
        camPos += camFront*camMove;
        cam.Move(caf*camMove);
    }
    if(glfwGetKey(window,GLFW_KEY_A)){
        camPos -= glm::normalize(glm::cross(camFront,camUp))*camMove;
        cam.Move(-glm::normalize(glm::cross(caf,camUp))*camMove);
    }
    if(glfwGetKey(window,GLFW_KEY_S)){
        camPos -= camFront*camMove;
        cam.Move(-caf*camMove);
    }
    if(glfwGetKey(window,GLFW_KEY_D)){
        camPos += glm::normalize(glm::cross(camFront,camUp))*camMove;
        cam.Move(glm::normalize(glm::cross(caf,camUp))*camMove);
    }
    if(glfwGetKey(window,GLFW_KEY_SPACE)){
        camPos.y += camMove;
        cam.Move(caup * camMove);
    }
    if(glfwGetKey(window,GLFW_KEY_LEFT_ALT)){
        cam.Move(-caup * camMove);
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // glViewport(0, 0, width, height);
    Render::RenderPipeConfig cfg;
    cfg.targetBufferWidth = width;
    cfg.targetBufferHeight = height;
    renderPipe.SetConfig(cfg);
}

void mouse_callback(GLFWwindow *window,double xpos,double ypos){
    float xOffset =  xpos - lastX;
    float yOffset =  lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    yaw += sensitivity * xOffset * deltaTime;
    pitch += sensitivity * yOffset * deltaTime;
    if(pitch>89.0f)pitch = 89.0f;
    if(pitch<-89.0f)pitch = -89.0f;
    cam.Rotate(sensitivity * xOffset * deltaTime,sensitivity * yOffset * deltaTime);
}

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