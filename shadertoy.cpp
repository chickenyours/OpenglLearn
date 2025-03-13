// 这是片段着色器的神圣几何领域

// 1. 你可以使用以下变量：
//    uniform float iTime;           //当前时间
//    uniform vec3  iResolution;     //屏幕分辨率
//    uniform vec4  iMouse;          //鼠标位置
//    uniform vec4  iDate;           //年月日时
//    uniform float iChannelTime[4]; //通道时间
//    uniform vec3  iChannelResolution[4]; //通道分辨率
//    uniform sampler2D iChannel0;   //通道0
//    uniform sampler2D iChannel1;   //通道1
//    uniform sampler2D iChannel2;   //通道2
//    uniform sampler2D iChannel3;   //通道3
//    uniform sampler2D iChannel4;   //通道4
//    uniform sampler2D iChannel5;   //通道5
//    uniform sampler2D iChannel6;   //通道6
//    uniform sampler2D iChannel7;   //通道7

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"

//窗口信息
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;
//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);


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
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);                                    //初始化视口
    //注册基本事件
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);          
    glfwSetCursorPosCallback(window,mouse_callback);
    glfwSetKeyCallback(window, keyCallback);

    //quad
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    unsigned int quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    //shader
    ShaderProgram screenShader("shaders/Final/screen.vs","shaders/shaderToy/qiu.fs");

    //循环
    while (!glfwWindowShouldClose(window))
    {
        screenShader.Use();
        //计算iTime
        float timeValue = glfwGetTime();
        ShaderU1f(screenShader,"iTime",timeValue);
        //设置分辨率
        ShaderUvec2(screenShader,"iResolution",glm::vec2(SCR_WIDTH,SCR_HEIGHT));
        //渲染
        glClear(GL_COLOR_BUFFER_BIT);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        
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