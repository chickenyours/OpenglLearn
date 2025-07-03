#include "application.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "code/Scene/scene.h"

#include "code/ECS/Component/component_init.h"
#include "code/Resource/Material/material_init.h"

#include "code/Resource/Material/Interfaces/BPR.h"

static GLFWwindow* window;
static bool InitOpengl(Log::StackLogErrorHandle errHandle = nullptr){
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(800, 600, "OpenGL Context", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        REPORT_STACK_ERROR(errHandle,"Application->InitOpengl","GLFW window creation failed");
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        REPORT_STACK_ERROR(errHandle,"Application->InitOpengl","Failed to initialize GLAD");
        return false;
    }

    std::string version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    LOG_INFO("Application->InitOpengl",version);
    return true;
}

static void ReleaseOpengl(){
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Application::Init(Log::StackLogErrorHandle errHandle){

#ifdef _WIN32
    Log::EnableAnsiColor();  // 👈 只需要调用一次
#endif
    if(!InitOpengl(errHandle)){
        REPORT_STACK_ERROR(errHandle, "Application->Init", "Failed to initialize OpenGL");
        return false;
    }
    isInited_ = true;
    return true;

}

void Application::Run(){
    RegisterAllComponents();
    RegisterAllMaterial();
    ECS::Scene s;
    
    s.LoadFromConfigFile("./Scenes/.json");
    ECS::EntityID a = s.GetEntity("123");
    // if(a){
    //     float material = s.registry_->GetComponent<ECS::Component::MeshRenderer>(a)->material->TryGetFeature<Resource::IBPR>()->property.metallic;
    //     std::cout<< material << std::endl;
    // }
    // s.hierarchySystem_->Print();
    // std::string info = s.registry_->GetComponent<ECS::Component::StaticModel>(a)->model->GetInfo();
    // LOG_INFO("Run", info);

    GLuint VAO = 
    
}

Application::~Application(){
    LOG_INFO("Application", "Application destructor called");
    if (isInited_) {
        ReleaseOpengl();
        LOG_INFO("Application", "OpenGL resources released successfully");
    }
}