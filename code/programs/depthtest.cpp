#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#include "code/Model/Model.h"

#define STB_IMAGE_IMPLEMENTATION    //是使预编译器include相关功能的代码
#include "stb_image.h"

//窗口信息
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;

//全局用户FBO
struct UserFBO{
    unsigned int FBO;
    unsigned int colorTexutre;
    unsigned int rbo;
} userFBO;

void AdjustUserFBO(int width,int height);

//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
//纹理加载函数
unsigned int loadTexture(char const * path,unsigned int warpS,unsigned int warpT);
unsigned int loadCubemap(vector<std::string> faces);
//camera
glm::vec3 camPos(0.0,3.0,0.0);
glm::vec3 camFront(0.0f,0.0f,-1.0f);
glm::vec3 camUp(0.0f,1.0f,0.0f);
float pitch = 0.0f;
float yaw = -90.0f;
float roll = 0.0f;
float lastX = SCR_WIDTH/2;
float lastY = SCR_HEIGHT/2;
float sensitivity = 0.1f;
float camfixSpeed = 2.5;
float camSpeed = 0;

//time
float deltaTime = 0.0f;
float latestTime = 0.0f;

//全局变量代码写在这里:

int main()
{
    //初始化
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
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

    //设置Opengl测试属性(程序中约定的默认行为，特殊需要特定代码修改，但要改回去)
    glEnable(GL_DEPTH_TEST);                                                    //深度测试
    glEnable(GL_BLEND);                                                         //混合测试
    glEnable(GL_STENCIL_TEST);                                                  //模板测试
    glEnable(GL_CULL_FACE);                                                     //开启剔除面测试
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);                                       //定义使用颜色填充
    glDepthMask(GL_TRUE);                                                       //启用深度写入,如果为false则禁止写入，但是，不影响深度测试
    glDepthFunc(GL_LEQUAL);                                                     //深度测试函数(小于且等于阈值通过)
    glStencilMask(0xFF);                                                        //默认写入
    glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);                               //深度和模板策略
    glStencilFunc(GL_ALWAYS, 1, 0xFF);             
        //GL_ALWAYS: 指定测试始终通过。
        //1: 指定模板值为 1，所有通过测试的片段将使用这个值更新模板缓冲区。
        //0xFF: 这是一个掩码，表示所有位（8 位）都参与模板测试，通常用于确定哪些位将被写入。
        //只要通过模板测试就会执行相应操作，不管深度测试是否通过
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);                          //混合测试逻辑 
    glCullFace(GL_FRONT);                                                       //裁剪前后
    glFrontFace(GL_CW);                                                         //裁剪顺序

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);                                    //初始化视口
    //设置对话窗口属性
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);                //隐藏鼠标
    glfwSetCursorPos(window,SCR_WIDTH/2,SCR_HEIGHT/2);                          //设定鼠标初始位置
    //注册基本事件
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);          
    glfwSetCursorPosCallback(window,mouse_callback);
    glfwSetKeyCallback(window, keyCallback);
    //初始化的代码写在这里：
    glm::mat4 model(1.0f);
    glm::mat4 view          = glm::lookAt(camPos,camPos+camFront,camUp);
    glm::mat4 camLookview   = glm::lookAt(glm::vec3(0.0),camFront,camUp);
    glm::mat4 projection    = glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/(float)SCR_HEIGHT,0.1f,500.0f);

    //shader
    Shader v(GL_VERTEX_SHADER,"depthTest/.vs");
    Shader f(GL_FRAGMENT_SHADER,"depthTest/.fs");
    Shader sf(GL_FRAGMENT_SHADER,"depthTest/single.fs");
    Shader screenVs(GL_VERTEX_SHADER,"depthTest/screenShader.vs");
    Shader screenFs(GL_FRAGMENT_SHADER,"depthTest/screenShader.fs");
    Shader skyBoxVs(GL_VERTEX_SHADER,"depthTest/skyBox.vs");
    Shader skyBoxFs(GL_FRAGMENT_SHADER,"depthTest/skyBox.fs");
    Shader skyBoxRVs(GL_VERTEX_SHADER,"depthTest/skyBoxReflect.vs");
    Shader skyBoxRFs(GL_FRAGMENT_SHADER,"depthTest/skyBoxReflect.fs");
    ShaderProgram program(&v,&f); 
    ShaderProgram singelProgram(&v,&sf);
    ShaderProgram screenShaderProgram(&screenVs,&screenFs);
    ShaderProgram skyBoxShaderProgram(&skyBoxVs,&skyBoxFs);
    ShaderProgram skyBoxRShaderProgram(&skyBoxRVs,&skyBoxRFs);


    //barrel
    //glm::mat4 barrelPos     = glm::translate(model,glm::vec3(.0f,2.0f,.0f));  
    //glm::mat4 STENCIL_panel_barrelPos = glm::translate(model,glm::vec3(.0f,2.0f,.0f)) * glm::scale(model,glm::vec3(1.02));
    
    //panel 
    glm::mat4 panelPos      = glm::translate(model,glm::vec3(.0f,0.0f,.0f)); 
    glm::mat4 STENCIL_panelPos = glm::translate(model,glm::vec3(.0f,0.0f,.0f)) * glm::scale(model,glm::vec3(1.02));
    float texWidth = 5.0;           //长度
    float texHeight = 5.0;          //宽度
    float panelWidth = 200.0;       //宽截距像素
    float panelHeight = 200.0;      //高截距像素
    std::vector<float> panel_vertex = {
        -panelWidth/2.0f,0.0f,-panelHeight/2.0f,   0.0f,0.0f,                           //0.0f,1.0f,0.0f,
        panelWidth/2.0f,0.0f,-panelHeight/2.0f,    panelWidth/texWidth,0.0f,            //0.0f,1.0f,0.0f,
        -panelWidth/2.0f,0.0f,panelHeight/2.0f,     0.0f,panelHeight/texHeight,         // 0.0f,1.0f,0.0f,
        // panelWidth/2.0f,0.0f,-panelHeight/2.0f,    panelWidth/texWidth,0.0f,            0.0f,1.0f,0.0f,
        // -panelWidth/2.0f,0.0f,panelHeight/2.0f,     0.0f,panelHeight/texHeight,          0.0f,1.0f,0.0f,
        panelWidth/2.0f,0.0f,panelHeight/2.0f,      panelHeight/texHeight,panelHeight/texHeight,    //0.0f,1.0f,0.0f,
    };
    std::vector<int> panel_element = {
        2,1,0,
        1,2,3
    };

    //grass
    vector<glm::mat4> grassListPos;
    grassListPos.push_back(glm::translate(model,glm::vec3(-1.5f,  0.5f, -0.48f)));
    grassListPos.push_back(glm::translate(model,glm::vec3( 1.5f,  0.5f,  0.51f)));
    grassListPos.push_back(glm::translate(model,glm::vec3( 0.0f,  0.5f,  0.7f)));
    grassListPos.push_back(glm::translate(model,glm::vec3(-0.3f,  0.5f, -2.3f)));
    grassListPos.push_back(glm::translate(model,glm::vec3( 0.5f,  0.5f, -0.6f)));
    std::vector<float> grass_vertex = {
        0.5,0.5,0.0,1.0,0.0,
        0.5,-0.5,0.0,1.0,1.0,
        -0.5,-0.5,0.0,0.0,1.0,
        -0.5,0.5,0.0,0.0,0.0
    };
    std::vector<int> grass_element = {
        0,1,2,
        2,3,0
    };

    //cubes
    glm::mat4 cube_1        = glm::translate(model,glm::vec3(.0f,2.0f,.0f));  
    glm::mat4 cube_2        = glm::translate(model,glm::vec3(1.0f,1.0f,1.5f));
    glm::mat4 skyCubePos      = glm::translate(model,glm::vec3(0.0f,5.0f,0.0f));
    std::vector<float> cube_vertex = {
    // back face (CCW winding)
         0.5f, -0.5f, -0.5f,  0.0f, 0.0f, // bottom-left
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, // bottom-right
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, // top-right
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, // top-right
         0.5f,  0.5f, -0.5f,  0.0f, 1.0f, // top-left
         0.5f, -0.5f, -0.5f,  0.0f, 0.0f, // bottom-left
        // front face (CCW winding)
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, // bottom-left
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, // bottom-right
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, // top-right
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, // top-right
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, // top-left
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, // bottom-left
        // left face (CCW)
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, // bottom-left
        -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, // bottom-right
        -0.5f,  0.5f,  0.5f,  1.0f, 1.0f, // top-right
        -0.5f,  0.5f,  0.5f,  1.0f, 1.0f, // top-right
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, // top-left
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, // bottom-left
        // right face (CCW)
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f, // bottom-left
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, // bottom-right
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, // top-right
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, // top-right
         0.5f,  0.5f,  0.5f,  0.0f, 1.0f, // top-left
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f, // bottom-left
        // bottom face (CCW)      
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, // bottom-left
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, // bottom-right
         0.5f, -0.5f,  0.5f,  1.0f, 1.0f, // top-right
         0.5f, -0.5f,  0.5f,  1.0f, 1.0f, // top-right
        -0.5f, -0.5f,  0.5f,  0.0f, 1.0f, // top-left
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, // bottom-left
        // top face (CCW)
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, // bottom-left
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, // bottom-right
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, // top-right
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, // top-right
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, // top-left
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, // bottom-left
};

float cubeVertices[] =  {
    // back face (CCW winding)
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        // front face (CCW winding)
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
        // left face (CCW)
        -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,
        // right face (CCW)
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f, 1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
        // bottom face (CCW)      
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
        // top face (CCW)
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f, 0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, 0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, 0.0f,  1.0f,  0.0f,
};



float quadVertices[] = { // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
/*
屏幕坐标
(-1,1)    (1,1)

(-1,-1)   (1,-1)

*/
float backQuadVertices[] = { // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, 0.5f,  0.0f, 0.0f,
         -0.5f, 0.5f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         -0.5f, 0.5f,  1.0f, 0.0f,
         -0.5f,  1.0f,  1.0f, 1.0f
    };

float skyboxVertices[] = {
    // positions          
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f
};

    //load panel_vertex
    unsigned int panelVAO,panelVBO,panelEBO;
    glGenVertexArrays(1,&panelVAO);
    glBindVertexArray(panelVAO);
    glGenBuffers(1,&panelVBO);
    glBindBuffer(GL_ARRAY_BUFFER,panelVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(float) * panel_vertex.size(),panel_vertex.data(),GL_STATIC_DRAW);
    glGenBuffers(1,&panelEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,panelEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(int)*panel_element.size(),panel_element.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    //load cube_vertex
    unsigned int cubeVAO,cubeVBO;
    glGenVertexArrays(1,&cubeVAO);
    glBindVertexArray(cubeVAO);
    glGenBuffers(1,&cubeVBO);
    glBindBuffer(GL_ARRAY_BUFFER,cubeVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(float) * cube_vertex.size(),cube_vertex.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    

    //load grass_vertex
    unsigned int grassVAO,grassVBO,grassEBO;
    glGenVertexArrays(1,&grassVAO);
    glBindVertexArray(grassVAO);
    glGenBuffers(1,&grassVBO);
    glBindBuffer(GL_ARRAY_BUFFER,grassVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(float) * grass_vertex.size(),grass_vertex.data(),GL_STATIC_DRAW);
    glGenBuffers(1,&grassEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,grassEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(int)*grass_element.size(),grass_element.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // screen quad VAO
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

    // screen backQuad VAO
    unsigned int backQuadVAO, backQuadVBO;
    glGenVertexArrays(1, &backQuadVAO);
    glGenBuffers(1, &backQuadVBO);
    glBindVertexArray(backQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, backQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(backQuadVertices), &backQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // skyBox VAO
    unsigned int skyBoxVAO,skyBoxVBO;
    glGenVertexArrays(1,&skyBoxVAO);
    glBindVertexArray(skyBoxVAO);
    glGenBuffers(1,&skyBoxVBO);
    glBindBuffer(GL_ARRAY_BUFFER,skyBoxVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(skyboxVertices),&skyboxVertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);

    //normalCube VAO
    unsigned int normalCubeVAO,normalCubeVBO;
    glGenVertexArrays(1,&normalCubeVAO);
    glBindVertexArray(normalCubeVAO);
    glGenBuffers(1,&normalCubeVBO);
    glBindBuffer(GL_ARRAY_BUFFER,normalCubeVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(cubeVertices),&cubeVertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(3*sizeof(float)));

    unsigned int texture = loadTexture("./images/R.jpg",GL_REPEAT,GL_REPEAT);
    unsigned int texture1 = loadTexture("./images/J.jpg",GL_REPEAT,GL_REPEAT);
    unsigned int grassTexture = loadTexture("./images/grass.png",GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE);                                   
    unsigned int blending_transparent_window = loadTexture("./images/blending_transparent_window.png",GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE);

    
    //load model
    //MyTool::Model m("./models/survival-guitar-backpack/source/Survival_BackPack_2/Survival_BackPack_2.fbx");
    MyTool::Model m("./models/Oil_barrel/Oil_barrel.fbx");
    glm::mat4 modelPos = glm::translate(model,glm::vec3(0.0,5.0,0.0));

    std::cout<<m._meshArray[0].textures.size()<<std::endl;
    



    //skyBox
    std::vector<std::string> faces
    {
        "./images/skybox/right.jpg",
        "./images/skybox/left.jpg",
        "./images/skybox/top.jpg",
        "./images/skybox/bottom.jpg",
        "./images/skybox/front.jpg",
        "./images/skybox/back.jpg"
    };
    unsigned int skyBoxTexture = loadCubemap(faces);
   
    //帧缓冲区对象
    unsigned int frameBuffer;
    glGenFramebuffers(1,&frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,frameBuffer);

    //创建颜色附件
    unsigned int textureColorBuffer;
    glGenTextures(1,&textureColorBuffer);
    glBindTexture(GL_TEXTURE_2D,textureColorBuffer);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,SCR_WIDTH,SCR_HEIGHT,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textureColorBuffer,0);     //纹理绑定到FBO中的颜色附件GL_COLOR_ATTACHMENT0
    //创建rob储存深度和模板附件
    unsigned int rbo;
    glGenRenderbuffers(1,&rbo);
    glBindRenderbuffer(GL_RENDERBUFFER,rbo);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,SCR_WIDTH,SCR_HEIGHT);            //分配空间
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,rbo);  //渲染缓冲区绑定到FBO中的GL_DEPTH_STENCIL_ATTACHMENT
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"FBO有问题"<<std::endl;
    }

    userFBO.FBO = frameBuffer;
    userFBO.rbo = rbo;
    userFBO.colorTexutre = textureColorBuffer;

    glBindFramebuffer(GL_FRAMEBUFFER,0);                 //切换会默认FBO

    
    program.Use();
    glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    singelProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(singelProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    screenShaderProgram.Use();
    glUniform1i(glGetUniformLocation(screenShaderProgram.getShaderProgramID(),"screenTexture"),0);
    skyBoxShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(skyBoxShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform1i(glGetUniformLocation(skyBoxShaderProgram.getShaderProgramID(),"skybox"),0);
    skyBoxRShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(skyBoxRShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    glUniform1i(glGetUniformLocation(skyBoxRShaderProgram.getShaderProgramID(),"skyCube"),0);
    

    //循环
    while (!glfwWindowShouldClose(window))
    {
        //get currentTime & deltaTime
        float currentTime = glfwGetTime();
        deltaTime = currentTime - latestTime;
        latestTime = deltaTime + latestTime;
        //处理输入
        processInput(window);
        //摄像机变速
        float camMove;
        if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)){
            camSpeed += deltaTime*3+camSpeed*0.001;
            camMove  = deltaTime*(camSpeed+camfixSpeed*4);
        }
        else{
            camSpeed = 0.0;
            camMove = deltaTime * camfixSpeed;
        }
        //摄像机移动输入处理
        if(glfwGetKey(window,GLFW_KEY_W)){
            camPos += camFront*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_A)){
            camPos -= glm::normalize(glm::cross(camFront,camUp))*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_S)){
            camPos -= camFront*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_D)){
            camPos += glm::normalize(glm::cross(camFront,camUp))*camMove;
        }
        if(glfwGetKey(window,GLFW_KEY_SPACE)){
            camPos.y += camMove;
        }
        //摄像机矩阵处理
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 
        view = glm::lookAt(camPos,camFront+camPos,camUp);
        camLookview=   glm::lookAt(glm::vec3(0.0),camFront,camUp);

        //逻辑处理代码写在这里：

        //设置绘制目标为frameBuffer
        glBindFramebuffer(GL_FRAMEBUFFER,frameBuffer);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        program.Use();
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        singelProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(singelProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        skyBoxShaderProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(skyBoxShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        skyBoxRShaderProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(skyBoxRShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        //panel
        glBindVertexArray(panelVAO);
        program.Use();
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(panelPos));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture);
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        //cube
        glStencilMask(0x00);            
        glBindVertexArray(cubeVAO);
        program.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture1);
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(cube_1));
        glDrawArrays(GL_TRIANGLES,0,36);
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(cube_2));
        glDrawArrays(GL_TRIANGLES,0,36);
        glStencilMask(0xFF); 
        //skycube
        glStencilMask(0x00);   
        glBindVertexArray(normalCubeVAO);
        skyBoxRShaderProgram.Use();
        glBindTexture(GL_TEXTURE_CUBE_MAP,skyBoxTexture);
        glUniform3f(glGetUniformLocation(skyBoxRShaderProgram.getShaderProgramID(),"EyePos"),camPos[0],camPos[1],camPos[2]);
        glUniformMatrix4fv(glGetUniformLocation(skyBoxRShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(skyCubePos));
        glDrawArrays(GL_TRIANGLES,0,36);
        glUniformMatrix4fv(glGetUniformLocation(skyBoxRShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(modelPos));
        m.Draw();
        glStencilMask(0xFF);      
        //绘制天空盒
        glStencilMask(0x00);
        glBindVertexArray(skyBoxVAO);
        skyBoxShaderProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(skyBoxShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(camLookview));
        glBindTexture(GL_TEXTURE_CUBE_MAP,skyBoxTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glStencilMask(0xFF);
        //STENCIL_panel
        glDisable(GL_DEPTH_TEST);                       
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF); 
        glBindVertexArray(panelVAO);
        singelProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(singelProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(STENCIL_panelPos));                            
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        glEnable(GL_DEPTH_TEST);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);           
        //window
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE); 
        glStencilMask(0x00);
        glBindVertexArray(grassVAO);
        program.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,blending_transparent_window);
        for(int i =0;i<grassListPos.size();i++){
                glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(grassListPos[i]));
            glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);  
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE); 
        glStencilMask(0xFF);
        //切换绘制目标为默认缓冲区(屏幕)
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindVertexArray(quadVAO);
        screenShaderProgram.Use();
        glBindTexture(GL_TEXTURE_2D,textureColorBuffer);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); 

        //摄像机方向反转
        glBindFramebuffer(GL_FRAMEBUFFER,frameBuffer);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        view =  glm::lookAt(camPos,-camFront+camPos,camUp);
        program.Use();
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        singelProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(singelProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        skyBoxShaderProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(skyBoxShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));

        //panel
        glBindVertexArray(panelVAO);
        program.Use();
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(panelPos));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture);
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        //STENCIL_panel
        glDisable(GL_DEPTH_TEST);                       
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);  
        singelProgram.Use();
        glUniformMatrix4fv(glGetUniformLocation(singelProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(STENCIL_panelPos));                            
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        glEnable(GL_DEPTH_TEST);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        //cube
        glStencilMask(0x00);            
        glBindVertexArray(cubeVAO);
        program.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture1);
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(cube_1));
        glDrawArrays(GL_TRIANGLES,0,36);
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(cube_2));
        glDrawArrays(GL_TRIANGLES,0,36);
        glStencilMask(0xFF);       
        //window
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE); 
        glStencilMask(0x00);
        glBindVertexArray(grassVAO);
        program.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,blending_transparent_window);
        for(int i =0;i<grassListPos.size();i++){
                glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(grassListPos[i]));
            glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);  
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE); 
        glStencilMask(0xFF);
        //切换绘制目标为默认缓冲区(屏幕)
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glBindVertexArray(backQuadVAO);
        screenShaderProgram.Use();
        glBindTexture(GL_TEXTURE_2D,textureColorBuffer);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);

        
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
    AdjustUserFBO(width,height);
}

void mouse_callback(GLFWwindow *window,double xpos,double ypos){
    
    float xOffset =  xpos - lastX;
    float yOffset =  lastY - ypos;
    lastX = xpos;
    lastY = ypos;
    yaw += sensitivity * xOffset;
    pitch += sensitivity * yOffset;
    if(pitch>89.0f)pitch = 89.0f;
    if(pitch<-89.0f)pitch = -89.0f;
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

unsigned int loadTexture(char const * path,unsigned int warpS,unsigned int warpT)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data)
    {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, warpS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, warpS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }

    return textureID;
}

unsigned int loadCubemap(vector<std::string> faces)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++)
    {
        unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
                         0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data
            );
            stbi_image_free(data);
        }
        else
        {
            std::cout << "Cubemap tex failed to load at path: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}  

void AdjustUserFBO(int width,int height){
    if(userFBO.FBO){
       // 1. 绑定帧缓冲对象
        glBindFramebuffer(GL_FRAMEBUFFER,userFBO.FBO);
        
        // 2. 更新颜色纹理
        glBindTexture(GL_TEXTURE_2D, userFBO.colorTexutre);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        
        // 3. 更新深度/模板缓冲区
        glBindRenderbuffer(GL_RENDERBUFFER, userFBO.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        
        // 4. 解绑帧缓冲对象
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}