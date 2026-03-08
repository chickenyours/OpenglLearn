#include "application.h"

#include <windows.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "engine/Scene/scene.h"
#include "engine/ECS/Component/component_init.h"
#include "engine/Resource/Material/material_init.h"
#include "engine/ECS/System/Systems/transform_change.h"
#include "engine/ECS/System/Systems/static_mesh_render.h"
#include "engine/Config/config.h"
#include "engine/ToolAndAlgorithm/Performance/time.h"
#include "engine/ECS/System/Camera/move_able.h"
#include "engine/ModuleManager/module_manager.h"
#include "engine/Script/script_interface.h"
#include "engine/ECS/System/Script/script_system.h"
#include "engine/CodeMomeryInsert/memory_insert.h"
#include "engine/ECS/System/Collision/aabb_system.h"
#include "engine/ECS/System/Systems/transform_change_2d.h"
#include "engine/ECS/System/Camera/2d.h"
#include "engine/ECS/Component/component_view.h"
// #include "code/"


void framebuffer_size_callback(GLFWwindow* window, int width, int height){
    Environment::Environment::Instance().windowSize = glm::vec2(width,height);
    Environment::Environment::Instance().isWindowSizeChange = true;
}

static GLFWwindow* window;
static bool InitOpengl(Log::StackLogErrorHandle errHandle = nullptr){
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "OpenGL Context", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        REPORT_STACK_ERROR(errHandle,"Application->InitOpengl","GLFW window creation failed");
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        REPORT_STACK_ERROR(errHandle,"Application->InitOpengl","Failed to initialize GLAD");
        return false;
    }

    std::string version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    LOG_INFO("Application->InitOpengl",version);
    return true;
}

static void UpdateWindow(){
    glfwSwapBuffers(window);                                //交换缓冲
    glfwPollEvents();                                       //事件响应
}

static void ReleaseOpengl(){
    glfwDestroyWindow(window);
    glfwTerminate();
}

// 模块
// static ECS::Core::ResourceModule::ResourceManager rs;

// static Module::ModuleManager mod;

bool Application::Init(Log::StackLogErrorHandle errHandle){

#ifdef _WIN32
    Log::EnableAnsiColor();  // 👈 只需要调用一次
#endif
    if(!InitOpengl(errHandle)){
        REPORT_STACK_ERROR(errHandle, "Application->Init", "Failed to initialize OpenGL");
        return false;
    }

    // 初始化模块
    
    Module::ModuleManager::Instance().Init();

    ScriptManager::Instance().Init();

    isInited_ = true;
    return true;
    // ECS::Core::ResourceModule::ResourceManager::SetInstance(&rs);
}

typedef void(*set_dll_module_api)(const Module::ModuleHost& host);
typedef IScript*(*GetScript)();
typedef void(*DeleteScriptObject)(IScript*);

void Application::Run(){
    RegisterAllComponents();
    RegisterAllMaterial();
    ECS::Scene s;
    

    ECS::EntityHandle player = s.CreateNewEntity();
    ECS::EntityHandle MoveCamera = s.CreateNewEntity();

    ECS::Core::ComponentStorage<ECS::Component::Transform> moveTransform;
    ECS::Core::ComponentStorage<ECS::Component::Transform> cameraMoveTransform;
    ECS::Core::ComponentStorageView<ECS::Component::Transform> allTransformView;
    ECS::Core::ComponentStorage<ECS::Component::Camera> cameraComponent;
    ECS::Core::ComponentStorageView<ECS::Component::Camera> cameraComponentView;

    auto handle1 = moveTransform.Add(player.GetID());
    auto handle2 = moveTransform.Add(MoveCamera.GetID());
    allTransformView.PushHandle(handle1);
    allTransformView.PushHandle(handle2);

    auto camhandle = cameraComponent.Add(MoveCamera.GetID());
    cameraComponentView.PushHandle(camhandle);

    ECS::System::LocalTransformCalculator2D ltc;

    ECS::System::CameraMovable2D cm2D;


}

Application::~Application(){
    LOG_INFO("Application", "Application destructor called");
    if (isInited_) {
        ReleaseOpengl();
        LOG_INFO("Application", "OpenGL resources released successfully");
    }
}