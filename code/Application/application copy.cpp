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

    ECS::EntityID e = s.GetEntity("123");
    auto ren = s.registry_->GetComponent<ECS::Component::MeshRenderer>(e);

    // // 插入脚本
    // HMODULE hDll = LoadLibraryA("script.dll");
    // if(!hDll){
    //     // std::cerr << "Failed to load DLL!" << std::endl;
    //     LOG_ERROR("Application","Failed to load DLL!");
    //     return;
    // }
    
    // set_dll_module_api setmod = (set_dll_module_api)GetProcAddress(hDll,"SetModule"); 
    // GetScript get_script_XuanZhuang = (GetScript)GetProcAddress(hDll, "create_plugin_object_XuanZhuang");
    // DeleteScriptObject delete_script = (DeleteScriptObject)GetProcAddress(hDll,"DeleteScriptObject");

    // if(!setmod || !get_script_XuanZhuang || !delete_script){
    //     LOG_ERROR("Application","Failed to load functions!");
    //     return;
    // }

    // // 初始化插件
    // setmod(Module::ModuleManager::Instance().Export());
    // // 创建插件脚本对象
    // std::unique_ptr<IScript, DeleteScriptObject> script(get_script_XuanZhuang(), delete_script);
    // std::unique_ptr<IScript, DeleteScriptObject> script2(get_script_XuanZhuang(), delete_script);

    // script->OnStart(&s, camEntity);
    // script2->OnStart(&s, e);

    do{
        Environment::Environment::Instance().Update();
        Input::KeyboardInput::Instance().Update();
        Input::MouseInput::Instance().Update();

        // 更新脚本
        // script->OnUpdate(&s,e);
        // script2->OnUpdate(&s,e);
        ss.Update();

        lTC.Update();
        s.hierarchySystem_->Update();
        camMove.Update();
        if(ren){
            ren->uboData.values[0] = glm::vec4(cam->camFront,1.0);
        }

        rd.Update();

        UpdateWindow();
    }while(Input::KeyboardInput::Instance().GetKeyState(VK_ESCAPE) != Input::KeyboardInput::KeyState::PRESSED);


    // ECS::System::StaticMeshRender mr;
    // mr.AddEntities(entities, *s.registry_);

    // while (Input::KeyboardInput::Instance().GetKeyState(VK_ESCAPE) != Input::KeyboardInput::KeyState::PRESSED){
    //     Input::KeyboardInput::Instance().Update();
    //     Input::MouseInput::Instance().Update();
    //     auto left = Input::MouseInput::Instance().GetButtonState(Input::MouseInput::MouseButton::LEFT);
    //     if(left == Input::MouseInput::ButtonState::PRESSED){
    //         std::cout << "left == Input::MouseInput::ButtonState::PRESSED" << std::endl;
    //     }
    //     if(left == Input::MouseInput::ButtonState::RELEASED){
    //         std::cout << "left == Input::MouseInput::ButtonState::RELEASED" << std::endl;
    //     }

    //     auto offset = Input::MouseInput::Instance().GetMouseOffset();
    //     if(offset != glm::ivec2(0)){
    //         std::cout << "<" << offset.x << "," << offset.y << ">" << std::endl;
    //     }
    // }

    // while (Input::KeyboardInput::Instance().GetKeyState(VK_ESCAPE) != Input::KeyboardInput::KeyState::PRESSED)
    // {
        
    //     Input::KeyboardInput::Instance().Update();
    //     auto w = Input::KeyboardInput::Instance().GetKeyState('W');      
    //     // auto a = Input::KeyboardInput::Instance().GetKeyState('A');
    //     // auto s = Input::KeyboardInput::Instance().GetKeyState('S');
    //     // auto d = Input::KeyboardInput::Instance().GetKeyState('D');
    
    //     if(w == Input::KeyboardInput::KeyState::PRESSED){
    //         std::cout << "w == Input::KeyboardInput::KeyState::PRESSED" << std::endl;
    //     }
    //     else if(w == Input::KeyboardInput::KeyState::RELEASED){
    //         std::cout << "w == Input::KeyboardInput::KeyState::RELEASED" << std::endl;
    //     }
    // }
    



    // ECS::EntityID a = s.GetEntity("123");
   
    // if(a){
    //     float material = s.registry_->GetComponent<ECS::Component::MeshRenderer>(a)->materialList[0]->TryGetFeature<Resource::IBPR>()->property.metallic;
    //     std::cout<< material << std::endl;
    // }
    // s.hierarchySystem_->Print();
    // std::string info = s.registry_->GetComponent<ECS::Component::StaticModel>(a)->model->GetInfo();
    // LOG_INFO("Run", info);

    // lTC.Update();
    
    // s.hierarchySystem_->Update();

    
    // auto printTransform = [](const glm::mat4& matrix){
    //     for (int i = 0; i < 4; ++i) {
    //         for (int j = 0; j < 4; ++j) {
    //             std::cout << matrix[j][i] << " ";
    //         }
    //         std::cout << std::endl;
    //     }
    // };

    // for(const std::string& it : {"123","456","789"}){
    //     ECS::EntityID a = s.GetEntity(it);
    //     auto transform = s.registry_->GetComponent<ECS::Component::Transform>(a);
    //     auto local = transform->localMatrix;
    //     auto world = transform->worldMatrix;
    //     std::cout<< it + " local matrix" << std::endl;
    //     printTransform(local);
    //     std::cout<< it + " world matrix" << std::endl;
    //     printTransform(world);
    // }




    
}

Application::~Application(){
    LOG_INFO("Application", "Application destructor called");
    if (isInited_) {
        ReleaseOpengl();
        LOG_INFO("Application", "OpenGL resources released successfully");
    }
}