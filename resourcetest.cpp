#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "code/Config/config.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/ECS/Core/Resource/resource_manager.h"

#include "code/Resource/Texture/texture.h"
#include "code/Resource/Shader/shader.h"
#include "code/Resource/Shader/shader_manager.h"
#include "code/Resource/Shader/shader_factory.h"

void test1(){
    Resource::ResourceHandle<Resource::Shader> shader5;
    {

        // auto tex = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::Texture>("./images/texconfig/.json");
        // auto tex2 = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::Texture>("./images/texconfig/.json");
      

        auto factory3 = Resource::ShaderManager::GetInstance().GetShaderFactoryFromConfigFile("./shaders/shaderConfig/2.json");
        auto factory = Resource::ShaderManager::GetInstance().GetShaderFactoryFromConfigFile("./shaders/shaderConfig/.json");
        auto factory2 = Resource::ShaderManager::GetInstance().GetShaderFactoryFromConfigFile("./shaders/shaderConfig/.json");

        // Resource::Shader shader;
        // std::string err;
        // if(factory){
        //     if(!factory->GenerateShader(err,shader)){
        //         LOG_ERROR("MAIN", err);
        //         return;
        //     }
        //     factory->Print();
        //     std::string code;
        //     factory->GenerateFinalShaderCode(code);
        //     std::cout << code << std::endl;
        // } 


        // std::cout<< shader.GetShaderID() << std::endl;
        // std::cout<< &(*factory) << std::endl;
        // std::cout<< &(*factory2) << std::endl;
    }

    

}

void test2(){
    auto factory = Resource::ShaderManager::GetInstance().GetShaderFactoryFromConfigFile("./shaders/shaderConfig/3.json");
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
    test2();
    // std::cout<< "hhhhh" << std::endl;
    

    // 清理
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}