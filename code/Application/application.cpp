#include "application.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "code/Scene/scene.h"

#include "code/ECS/Component/component_init.h"
#include "code/Resource/Material/material_init.h"

#include "code/Resource/Material/Interfaces/BPR.h"

#include "code/ECS/System/Systems/transform_change.h"

#include "code/ECS/System/Systems/static_mesh_render.h"

#include "code/Input/key_get.h"
#include "code/Input/mouse_input.h"

#include "code/Config/config.h"

#include "ToolAndAlgorithm/Performance/time.h"

#include "code/ECS/System/Camera/move_able.h"

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
    lTC.AddEntities(entities, *s.registry_);

    ECS::System::CameraMovable camMove;
    camMove.AddEntity(camEntity,*s.registry_);

    ECS::System::StaticMeshRender rd;
    rd.Init();
    rd.AddEntities(entities, *s.registry_);

    rd.SetCameraObject(camEntity, *s.registry_);

    do{
        Environment::Environment::Instance().Update();
        Input::KeyboardInput::Instance().Update();
        Input::MouseInput::Instance().Update();

        lTC.Update();
        camMove.Update();
        rd.Update();

        UpdateWindow();
        // std::cout << "camPos:" << "<" << cam->camPos.x << "," << cam->camPos.y << "," << cam->camPos.z << ">" << " camFront:" << "<" << cam->camFront.x << "," << cam->camFront.y << "," << cam->camFront.z << ">" << std::endl;
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