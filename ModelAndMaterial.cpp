#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/Model/model_animation.h"
#include "code/Material/material.h"
#include "code/Model/animator.h"
#include "code/VisualTool/visual.h"

#define print(msg) std::cout<<(msg)<<std::endl 

//窗口信息
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;
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
float sensitivity = 0.1f;
float camfixSpeed = 2.5;
float camSpeed = 0;

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

    //Render::Model m("./myModelsConfigs/Oil_barrel.json");
    Render::Model mouse("./myModelsConfigs/hhhh.json");
    Render::Animator* mouse_animatior = mouse.GetAnimator(); 
    mouse.Print(0);

    // //场景可视化组件
    Render::Marker marker(1000,1000);
    marker.SetPointSize(10.0);
    marker.SetLineWidth(5.0);
    // marker.AddPoint(glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // Red
    // marker.AddPoint(glm::vec3(1.0f, -1.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));  // Green
    // marker.AddPoint(glm::vec3(1.0f, 1.0f, -1.0f), glm::vec3(0.0f, 0.0f, 1.0f));   // Blue
    // marker.AddPoint(glm::vec3(-1.0f, 1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 0.0f));  // Yellow
    // marker.AddPoint(glm::vec3(-1.0f, -1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f));  // Magenta
    // marker.AddPoint(glm::vec3(1.0f, -1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f));   // Cyan
    // marker.AddPoint(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f));    // White
    // marker.AddPoint(glm::vec3(-1.0f, 1.0f, 1.0f), glm::vec3(0.5f, 0.5f, 0.5f));   // Gray

    // // 添加树形结构的线，注意线的两个点颜色不同
    // marker.AddLine(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)); // trunk
    // marker.AddLine(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-0.5f, 1.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)); // left branch
    // marker.AddLine(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.5f, 1.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));  // right branch
    // marker.AddLine(glm::vec3(-0.5f, 1.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(-0.75f, 2.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)); // left-left branch
    // marker.AddLine(glm::vec3(-0.5f, 1.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(-0.25f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 1.0f)); // left-right branch
    // marker.AddLine(glm::vec3(0.5f, 1.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.25f, 2.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.0f));  // right-left branch
    // marker.AddLine(glm::vec3(0.5f, 1.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.75f, 2.0f, 0.0f), glm::vec3(0.5f, 0.0f, 1.0f));  // right-right branch
    // marker.AddLine(glm::vec3(-0.75f, 2.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(-0.85f, 2.5f, 0.0f), glm::vec3(1.0f, 0.0f, 1.0f)); // left-left-left branch
    // marker.AddLine(glm::vec3(-0.75f, 2.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(-0.65f, 2.5f, 0.0f), glm::vec3(0.5f, 1.0f, 0.5f)); // left-left-right branch
    // marker.AddLine(glm::vec3(-0.25f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(-0.35f, 2.5f, 0.0f), glm::vec3(1.0f, 0.5f, 0.5f)); // left-right-left branch
    // marker.AddLine(glm::vec3(-0.25f, 2.0f, 0.0f), glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(-0.15f, 2.5f, 0.0f), glm::vec3(0.5f, 0.5f, 1.0f)); // left-right-right branch
    // marker.AddLine(glm::vec3(0.25f, 2.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.0f), glm::vec3(0.15f, 2.5f, 0.0f), glm::vec3(1.0f, 0.75f, 0.25f)); // right-left-left branch
    // marker.AddLine(glm::vec3(0.25f, 2.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.0f), glm::vec3(0.35f, 2.5f, 0.0f), glm::vec3(0.25f, 0.75f, 1.0f)); // right-left-right branch
    // marker.AddLine(glm::vec3(0.75f, 2.0f, 0.0f), glm::vec3(0.5f, 0.0f, 1.0f), glm::vec3(0.65f, 2.5f, 0.0f), glm::vec3(0.75f, 1.0f, 0.25f)); // right-right-left branch
    // marker.AddLine(glm::vec3(0.75f, 2.0f, 0.0f), glm::vec3(0.5f, 0.0f, 1.0f), glm::vec3(0.85f, 2.5f, 0.0f), glm::vec3(0.25f, 1.0f, 0.75f)); // right-right-right branch
    // marker.AddLine(glm::vec3(-0.85f, 2.5f, 0.0f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(-0.9f, 3.0f, 0.0f), glm::vec3(1.0f, 0.25f, 0.25f)); // left-left-left-left branch
    // marker.AddLine(glm::vec3(-0.85f, 2.5f, 0.0f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(-0.8f, 3.0f, 0.0f), glm::vec3(0.25f, 1.0f, 0.25f)); // left-left-left-right branch
    // marker.AddLine(glm::vec3(0.85f, 2.5f, 0.0f), glm::vec3(0.25f, 1.0f, 0.75f), glm::vec3(0.8f, 3.0f, 0.0f), glm::vec3(0.25f, 0.25f, 1.0f));  // right-right-right-left branch
    // marker.AddLine(glm::vec3(0.85f, 2.5f, 0.0f), glm::vec3(0.25f, 1.0f, 0.75f), glm::vec3(0.9f, 3.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.5f));    // right-right-right-right branch

    // 将模型节点添加到marker
    
    mouse.VisualAddNodeAttribution(&marker);
    //m.VisualAddNodeAttribution(&marker);

    //循环
    while (!glfwWindowShouldClose(window))
    {
        //get currentTime & deltaTime
        float currentTime = glfwGetTime();
        deltaTime = currentTime - latestTime;
        latestTime = deltaTime + latestTime;
        //处理输入
        processInput(window);
        //摄像机变速
        float camMove;
        if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)){
            camSpeed += deltaTime*3;
            camMove  = deltaTime*(camSpeed+camfixSpeed*4);
        }
        else{
            camSpeed = 0.0;
            camMove = deltaTime * camfixSpeed;
        }
        //摄像机移动输入处理
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
        //摄像机矩阵处理
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 
        view = glm::lookAt(camPos,camFront+camPos,camUp);

        //逻辑处理代码写在这里：
        Render::Material::GlobalMat4ParameterMap["projection"] = projection;
        Render::Material::GlobalMat4ParameterMap["view"] = view; 
        Render::Material::GlobalMat4ParameterMap["model"] = glm::scale(model,glm::vec3(0.05));
        Render::Material::GlobalVec3ParameterMap["viewPos"] = camPos;
        Render::Material::GlobalFloatParameterMap["iTime"] = currentTime;

        if(mouse_animatior){
            mouse_animatior->UpdateAnimation(deltaTime);
            auto final_matrix = mouse_animatior->GetFinalBoneMatrices();
        }
        
        

        //渲染
        glClearColor(0.25f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        //m.Draw();

        glm::mat4 mouseModel = glm::scale(glm::rotate(glm::translate(model, glm::vec3(1,0,0)), glm::radians(90.0f), glm::vec3(0.0, 1.0, 0.0)), glm::vec3(0.5));
        Render::Material::GlobalMat4ParameterMap["model"] = mouseModel;

        mouse.Draw();

        //可视化渲染组件渲染
        marker.SetMatrix(projection,view, mouseModel);
        marker.Draw();
        

        //渲染主要逻辑写在这里
        
        
        
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
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

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