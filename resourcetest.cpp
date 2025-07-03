#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "code/Config/config.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/ECS/Core/Resource/resource_load_option.h"

#include "code/Resource/Texture/texture.h"
#include "code/Resource/Shader/shader.h"
#include "code/Resource/Shader/shader_manager.h"
#include "code/Resource/Shader/shader_factory.h"
#include "code/Resource/Shader/shader_program.h"

#include "code/Resource/Material/material.h"
#include "code/Resource/Material/Interfaces/common.h"

#include "code/ECS/Component/component_register.h"
#include "code/ECS/Entity/entity.h"

#include "code/ECS/Component/Render/mesh_renderer.h"

void test1(){
    std::cout<< "jjj" << std::endl;
    auto shader = ShaderManager::GetInstance().GetShaderFromShaderFile("./shaders/Final/base_render_world_animation.vs");
    std::cout<< "hhh" << std::endl;
}

void TestShaderProgram1(){
    // 检验模块是否成功加载以及处理异常情况
    
    auto shaderProgram = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgram>(
        ECS::Core::ResourceModule::FromConfig<Resource::ShaderProgram>("./ShaderProgramConfig/1.json")
    );

    auto shaderProgram2 = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgram>(
        ECS::Core::ResourceModule::FromConfig<Resource::ShaderProgram>("./ShaderProgramConfig/1.json")
    );

    

    if(shaderProgram2){
        std::cout<< (unsigned int)shaderProgram->GetID() << std::endl;
    }
}

void TestMaterial1(){
    Resource::Material material;

    material.AddFeature<Resource::IBase>(
        [](){
            Resource::IBase a;
            a.time = 1.0;
            return a;
        }()
    );

    std::cout << material.TryGetFeature<Resource::IBase>()->time << std::endl;
}

void TestRenderQueue(){
    ECS::Entity a(1);
    ECS::Core::ComponentRegister::Instance().AddComponent<ECS::Component::MeshRenderer>(
        a.GetID(),[]{
            ECS::Component::MeshRenderer mesh;
            mesh.material = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get(
                ECS::Core::ResourceModule::FromGenerator<Resource::Material>(
                    "myMaterial",
                    [](Log::StackLogErrorHandle err)-> std::unique_ptr<Resource::Material> {
                        auto material = std::make_unique<Resource::Material>();
                        material->AddFeature<Resource::IBase>(
                            []{
                                Resource::IBase feature;
                                feature.time = 1.0;
                                return feature;
                            }()
                        );
                        return material;
                    }
                )
            );
            return mesh;
        }()
    );

    std::cout<< ECS::Core::ComponentRegister::Instance().GetComponent<ECS::Component::MeshRenderer>(a.GetID())->material->TryGetFeature<Resource::IBase>()->time << std::endl;
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

    int b = 2;

    std::function<std::unique_ptr<Resource::Shader>(Log::StackLogErrorHandle)> a(
        [](Log::StackLogErrorHandle) -> std::unique_ptr<Resource::Shader> {
            return std::make_unique<Resource::Shader>();
        }
    );

    auto c = [b](Log::StackLogErrorHandle) -> std::unique_ptr<Resource::Shader> {
            return std::make_unique<Resource::Shader>();
    };

    // TestShaderProgram1();
    // TestMaterial1();
    TestRenderQueue();

    // 清理
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}