#include "application.h"

#include <windows.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "code/Scene/scene.h"
#include "code/ECS/Component/component_init.h"
#include "code/Resource/Material/material_init.h"
#include "code/ECS/System/Systems/transform_change.h"
#include "code/ECS/System/Systems/static_mesh_render.h"
#include "code/Config/config.h"
#include "ToolAndAlgorithm/Performance/time.h"
#include "code/ECS/System/Camera/move_able.h"
#include "code/ModuleManager/module_manager.h"
#include "code/Script/script_interface.h"
#include "code/ECS/System/Script/script_system.h"
#include "code/CodeMomeryInsert/memory_insert.h"
#include "code/ECS/System/Collision/aabb_system.h"
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
    
    s.LoadFromConfigFile("./Scenes/.json");

    ECS::EntityID camEntity = s.CreateNewEntity();
    auto trans = s.registry_->AddComponent<ECS::Component::Transform>(camEntity);
    auto cam = s.registry_->AddComponent<ECS::Component::Camera>(camEntity);
    s.hierarchySystem_->ApplyToRoot(camEntity);

    s.hierarchySystem_->Print();

    std::vector<ECS::EntityID> entities;
    entities.reserve(s.GetCount());
    for(ECS::EntityID i = 1; i <= s.GetCount(); i++){
        entities.push_back(i);
    }

    ECS::System::LocalTransformCalculator lTC;
    lTC.Init(&s);
    lTC.AddEntities(entities);

    ECS::System::ScriptSystem ss;
    ss.Init(&s);
    ss.AddEntities(entities);

    ECS::System::CameraMovable camMove;
    camMove.Init(&s);
    camMove.AddEntities({camEntity});

    ECS::System::StaticMeshRender rd;
    rd.Init(&s);
    rd.AddEntities(entities);

    rd.SetCameraObject(camEntity);

    ECS::System::AABBSystem aabbs;
    aabbs.Init(&s);
    aabbs.SetEventSub(&ss.queue);
    aabbs.AddEntities(entities);

    ECS::EntityID e = s.GetEntity("123");
    auto ren = s.registry_->GetComponent<ECS::Component::MeshRenderer>(e);

    auto script = s.registry_->GetComponent<ECS::Component::Script>(e);
    auto interface = script->scriptInterface;
    if(!interface){
        LOG_ERROR("main","hh");
        return;
    }

    // Json::Value data;
    // if(!Tool::JsonHelper::LoadJsonValueFromFile("./Scripts/Data/1.json",data)){
    //     LOG_ERROR("main","hh");
    //     return;
    // }

    // Json::Value blueprint;
    // if(!Tool::JsonHelper::LoadJsonValueFromFile("./Scripts/BluePrint/XuanZhuan.json",blueprint)){
    //     LOG_ERROR("main","XuanZhuang");
    //     return;
    // }

    // SetObjectProperty(data,blueprint,interface);

    // 物体控制器变量
    int currentControl = 0;
    auto currentTransform = s.registry_->GetComponent<ECS::Component::Transform>(entities[currentControl]);
    float controlSpeed = 1.0;

    do{
        Environment::Environment::Instance().Update();
        Input::KeyboardInput::Instance().Update();
        Input::MouseInput::Instance().Update();

        if(Input::KeyboardInput::Instance().GetKeyState('O') == Input::KeyboardInput::KeyState::PRESSED){
            currentControl = std::min((int)entities.size() - 1, currentControl + 1);
            currentTransform = s.registry_->GetComponent<ECS::Component::Transform>(entities[currentControl]);
            std::cout << "switch to " << entities[currentControl] << std::endl;
        }
        else if (Input::KeyboardInput::Instance().GetKeyState('P') == Input::KeyboardInput::KeyState::PRESSED){
            currentControl = std::max(0, currentControl - 1);
            currentTransform = s.registry_->GetComponent<ECS::Component::Transform>(entities[currentControl]);
            std::cout << "switch to " << entities[currentControl] << std::endl;
        }
        if(currentTransform){
            float deltaTime = Environment::Environment::Instance().GetUpdateIntervalTime();
            if(Input::KeyboardInput::Instance().GetKeyState(VK_UP) == Input::KeyboardInput::KeyState::HELD){
                currentTransform->position.z += controlSpeed * deltaTime;
            }
            if(Input::KeyboardInput::Instance().GetKeyState(VK_DOWN) == Input::KeyboardInput::KeyState::HELD){
                currentTransform->position.z -= controlSpeed * deltaTime;
            }
            if(Input::KeyboardInput::Instance().GetKeyState(VK_LEFT) == Input::KeyboardInput::KeyState::HELD){
                currentTransform->position.x += controlSpeed * deltaTime;
            }
            if(Input::KeyboardInput::Instance().GetKeyState(VK_RIGHT) == Input::KeyboardInput::KeyState::HELD){
                currentTransform->position.x -= controlSpeed * deltaTime;
            }
            if(Input::KeyboardInput::Instance().GetKeyState('N') == Input::KeyboardInput::KeyState::HELD){
                currentTransform->position.y += controlSpeed * deltaTime;
            }
            if(Input::KeyboardInput::Instance().GetKeyState('M') == Input::KeyboardInput::KeyState::HELD){
                currentTransform->position.y -= controlSpeed * deltaTime;
            }
        }

        // 更新碰撞
        aabbs.Update();
        // 更新脚本
        ss.Update();
        lTC.Update();
        s.hierarchySystem_->Update();
        camMove.Update();
        // if(ren){
        //     ren->uboData.values[0] = glm::vec4(cam->camFront,1.0);
        // }

        rd.Update();

        UpdateWindow();
    }while(Input::KeyboardInput::Instance().GetKeyState(VK_ESCAPE) != Input::KeyboardInput::KeyState::PRESSED);
    
}

Application::~Application(){
    LOG_INFO("Application", "Application destructor called");
    if (isInited_) {
        ReleaseOpengl();
        LOG_INFO("Application", "OpenGL resources released successfully");
    }
}