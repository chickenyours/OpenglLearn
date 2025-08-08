#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <thread>
#include <string>
#include <queue>
#include <mutex>
#include "code/shader.h"

//窗口信息
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;
std::string fsname = "awe";  // 片段着色器名字
std::queue<std::string> inputQueue; // 用于存储输入事件
std::mutex fsnameMutex; // 用于保护fsname的访问
std::mutex queueMutex; // 用于保护队列的访问

//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void inputThread();  // 处理输入的线程

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

    //指针版本
    //ShaderProgram* screenShader = new ShaderProgram ("shaders/Final/screen.vs", "shaders/shaderToy/" + fsname + ".fs");
    //栈版本
    ShaderProgram screenShader("shaders/Final/screen.vs", "shaders/shaderToy/" + fsname + ".fs");

    // 启动处理输入的线程
    std::thread inputHandler(inputThread);

    //循环
    while (!glfwWindowShouldClose(window))
    {
        screenShader.Use();
        //screenShader->Use();

        //计算iTime
        float timeValue = glfwGetTime();
        ShaderU1f(screenShader, "iTime", timeValue);
        //ShaderU1f(*screenShader, "iTime", timeValue);

        //设置分辨率
        ShaderUvec2(screenShader, "iResolution", glm::vec2(SCR_WIDTH, SCR_HEIGHT));
        //ShaderUvec2(*screenShader, "iResolution", glm::vec2(SCR_WIDTH, SCR_HEIGHT));
        //渲染
        glClear(GL_COLOR_BUFFER_BIT);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);                                //交换缓冲
        glfwPollEvents();                                       //事件响应

        // 检查输入事件队列
        std::lock_guard<std::mutex> lock(queueMutex);  // 锁住队列
        if (!inputQueue.empty()) {
            std::string newFsname = inputQueue.front(); // 获取输入事件
            inputQueue.pop();  // 从队列中移除事件

            std::lock_guard<std::mutex> fsnameLock(fsnameMutex);  // 锁住fsname
            if (newFsname != fsname) {
                fsname = newFsname;  // 更新片段着色器名称

                // 注意,创建临时对象时需要把资源完全转移,因为临时对象构造完毕会立即销毁
                screenShader = ShaderProgram("shaders/Final/screen.vs", "shaders/shaderToy/" + fsname + ".fs");
                //delete screenShader;
                //screenShader = new ShaderProgram("shaders/Final/screen.vs", "shaders/shaderToy/" + fsname + ".fs");
            }
        }
    }

    inputHandler.join();  // 等待输入线程结束
    glfwTerminate();
    return 0;
}

void inputThread() {
    while (true) {
        std::string newShader;
        std::cout << "Enter fragment shader name (without extension): ";
        std::cin >> newShader;
        std::cout<<std::endl;

        if (newShader == "exit") {
            break;  // 输入 "exit" 来退出线程
        }

        // 将输入的文件名存入事件队列
        std::lock_guard<std::mutex> lock(queueMutex);  // 锁住队列
        inputQueue.push(newShader);  // 将输入添加到队列
    }
}

void processInput(GLFWwindow *window)
{
    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
    // 鼠标回调函数，可以添加自定义代码
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
