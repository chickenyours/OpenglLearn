#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "code/Config/config.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/Resource/Texture/texture.h"


ECS::Core::ResourceSystem::ResourceManager rm;


void test1(){
    auto tex = rm.Get<Resource::Texture>("./images/texconfig/.json");
    auto tex2 = rm.Get<Resource::Texture>("./images/texconfig/.json");
    std::cout << tex2->GetID() << std::endl;
}


int main(){
#ifdef _WIN32
    Log::EnableAnsiColor();  // 👈 只需要调用一次
#endif
    // 初始化 GLFW
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    // 配置 OpenGL 版本
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 创建窗口并获取上下文
    GLFWwindow* window = glfwCreateWindow(800, 600, "OpenGL Context", nullptr, nullptr);
    if (!window) {
        std::cerr << "GLFW window creation failed\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window); // 💡关键：设置当前上下文

    // 初始化 GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return -1;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;


    test1();

    

    // 清理
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}