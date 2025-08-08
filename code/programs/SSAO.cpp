#include <iostream>
#include <math.h>
#include <iomanip>
#include <random>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "code/Model/Model.h"
#include "stb_image.h"

float ourLerp(float a, float b, float f)
{
    return a + f * (b - a);
}

//窗口信息
const unsigned int SCR_WIDTH = 1600;
const unsigned int SCR_HEIGHT = 800;

// const unsigned int AFTER_RENDER_BUFFER_WITDH = SCR_WIDTH * 1.5;
// const unsigned int AFTERRENDERBUFFERHEIGHT = SCR_WIDTH * 1.5;

//基本事件
unsigned int loadTexture(char const * path,unsigned int warpS,unsigned int warpT);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
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

//perspective
const float near = 0.02;
const float far = 50.0;

//全局变量代码写在这里:

// Custom implementation of the LookAt function
glm::mat4 calculate_lookAt_matrix(glm::vec3 front, glm::vec3 worldUp,glm::vec3 campos)
{
    // 1. Position = known
    // 2. Calculate cameraDirection
    glm::vec3 zaxis = glm::normalize(-front);
    // 3. Get positive right axis vector
    glm::vec3 xaxis = glm::normalize(glm::cross(glm::normalize(worldUp), zaxis));
    // 4. Calculate camera up vector
    glm::vec3 yaxis = glm::cross(zaxis, xaxis);

    // Create translation and rotation matrix
    // In glm we access elements as mat[col][row] due to column-major layout
    glm::mat4 translation = glm::mat4(1.0f); // Identity matrix by default
    translation[3][0] = -campos.x; // Fourth column, first row
    translation[3][1] = -campos.y;
    translation[3][2] = -campos.z;
    glm::mat4 rotation = glm::mat4(1.0f);
    rotation[0][0] = xaxis.x; // First column, first row
    rotation[1][0] = xaxis.y;
    rotation[2][0] = xaxis.z;
    rotation[0][1] = yaxis.x; // First column, second row
    rotation[1][1] = yaxis.y;
    rotation[2][1] = yaxis.z;
    rotation[0][2] = zaxis.x; // First column, third row
    rotation[1][2] = zaxis.y;
    rotation[2][2] = zaxis.z; 

    // Return lookAt matrix as combination of translation and rotation matrix
    return rotation * translation; // Remember to read from right to left (first translation then rotation)
}


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
    //设置Opengl属性
    glEnable(GL_DEPTH_TEST);                                                    //深度测试
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
    glm::mat4 projection    = glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/(float)SCR_HEIGHT,near,far);

    float vertices[] = {
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f
};
glm::vec3 cubePositions[] = {
    glm::vec3( 0.0f,  0.0f,  0.0f), 
    glm::vec3( 2.0f,  5.0f, -15.0f), 
    glm::vec3(-1.5f, -2.2f, -2.5f),  
    glm::vec3(-3.8f, -2.0f, -12.3f),  
    glm::vec3( 2.4f, -0.4f, -3.5f),  
    glm::vec3(-1.7f,  3.0f, -7.5f),  
    glm::vec3( 1.3f, -2.0f, -2.5f),  
    glm::vec3( 1.5f,  2.0f, -2.5f), 
    glm::vec3( 1.5f,  0.2f, -1.5f), 
    glm::vec3(-1.3f,  1.0f, -1.5f)  
};

float quadVertex[] = {1.0,1.0,0.0,1.0,1.0,
                      -1.0,1.0,0.0,0.0,1.0,
                      -1.0,-1.0,0.0,0.0,0.0,
                      -1.0,-1.0,0.0,0.0,0.0,
                      1.0,-1.0,0.0,1.0,0.0,   
                      1.0,1.0,0.0,1.0,1.0, 
    };
    
    unsigned int VAO,VBO;
    glGenVertexArrays(1,&VAO);
    glBindVertexArray(VAO);
    glGenBuffers(1,&VBO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    unsigned int QuadVBO,QuadVAO;
    glGenVertexArrays(1,&QuadVAO);
    glBindVertexArray(QuadVAO);
    glGenBuffers(1,&QuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER,QuadVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quadVertex),&quadVertex[0],GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(float),(void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Set up G-Buffer
    // 3 textures:
    // 1. Positions + depth (RGBA)
    // 2. Color (RGB) 
    // 3. Normals (RGB) 
    GLuint gBuffer;
    glGenFramebuffers(1, &gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
    GLuint gPositionDepth, gNormal, gAlbedoSpec;
    // - Position + linear depth color buffer
    glGenTextures(1, &gPositionDepth);
    glBindTexture(GL_TEXTURE_2D, gPositionDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPositionDepth, 0);
    // - Normal color buffer
    glGenTextures(1, &gNormal);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);
    // - Albedo color buffer
    glGenTextures(1, &gAlbedoSpec);
    glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoSpec, 0);

    unsigned int rbo;
    glGenRenderbuffers(1,&rbo);
    glBindRenderbuffer(GL_RENDERBUFFER,rbo);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,SCR_WIDTH,SCR_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,rbo);






    // - Tell OpenGL which color attachments we'll use (of this framebuffer) for rendering 
    GLuint attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3,attachments);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    ShaderProgram GbufferShader("Final/baserender.vs","SSAO/Gbuffer.fs");
    GbufferShader.Use();
    ShaderUmatf4(GbufferShader,"projection",projection);
    ShaderU1i(GbufferShader,"diffuse",0);
    ShaderU1i(GbufferShader,"specualr",1);
    ShaderU1f(GbufferShader,"NEAR",near);
    ShaderU1f(GbufferShader,"FAR",far);

    //ShaderProgram SSAOShader("")

    ShaderProgram afterLightShader("Final/screen.vs","SSAO/buffer.fs");
    afterLightShader.Use();
    ShaderU1i(afterLightShader,"gAlbedoSpec",0);
    ShaderU1i(afterLightShader,"ssaoMap",1);

    ShaderProgram screenShader("Final/screen.vs","Final/screen.fs");
    screenShader.Use();
    ShaderU1i(screenShader,"colorBuffer",0);

    ShaderProgram modelDisplay("Final/baserender.vs","Final/modelDiaplay.fs");
    modelDisplay.Use();
    ShaderUmatf4(modelDisplay,"projection",projection);
    ShaderUmatf4(modelDisplay,"model",glm::scale(model,glm::vec3(1.0)));
    ShaderU1i(modelDisplay,"diffuse",0);
    ShaderU1i(modelDisplay,"specular",1);

    ShaderProgram ssaoBlur("Final/screen.vs","SSAO/ssaoBlur.fs");
    ssaoBlur.Use();
    ShaderU1i(ssaoBlur,"ssaoMap",0);

    float radius = 0.5;

    ShaderProgram s("Final/screen.vs","SSAO/ssao.fs");
    s.Use();
    ShaderU1i(s,"gPositionDepth",0);
    ShaderU1i(s,"gNormal",1);
    ShaderU1i(s,"texNoise",2);
    ShaderUmatf4(s,"projection",projection);
    ShaderU1f(s,"radius",radius);



    
    // ShaderU1i(ssaoBlur,"gAlbedoSpec",0);
    // ShaderU1i(ssaoBlur,"ssaoMap",1);


    // std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0); // 随机浮点数，范围0.0 - 1.0
    // std::default_random_engine generator;
    // std::vector<glm::vec3> ssaoKernel;
    // auto lerp = [](GLfloat a, GLfloat b, GLfloat f){return a + f * (b - a);};
    // for (GLuint i = 0; i < 64; ++i)
    // {
    //     glm::vec3 sample(
    //         randomFloats(generator) * 2.0 - 1.0, 
    //         randomFloats(generator) * 2.0 - 1.0, 
    //         randomFloats(generator)
    //     );
    //     sample = glm::normalize(sample);
    //     sample *= randomFloats(generator);
    //     GLfloat scale = GLfloat(i) / 64.0; 
    //     // Scale samples s.t. they're more aligned to center of kernel
    //     scale = lerp(0.1f, 1.0f, scale * scale);
    //     sample *= scale;
    //     ssaoKernel.push_back(sample);
    // }
    // generate sample kernel
    // ----------------------
    
    std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0); // generates random floats between 0.0 and 1.0
    std::default_random_engine generator;
    std::vector<glm::vec3> ssaoKernel;
    for (unsigned int i = 0; i < 64; ++i)
    {
        glm::vec3 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = float(i) / 64.0f;

        // scale samples s.t. they're more aligned to center of kernel
        scale = ourLerp(0.1f, 1.0f, scale * scale);
        sample *= scale;
        ssaoKernel.push_back(sample);
    }
    for(int i = 0 ;i<64;i++){
        ShaderUvec3(s,"samples[" + std::to_string(i) + "]",ssaoKernel[i]);
    }


    MyTool::Model m("models/stairs/StairsLat/obj/objStair.obj");    

    auto RenderScene = [&](ShaderProgram& Shader){
        Shader.Use();
        //ShaderUmatf4(Shader,"view",view);
        ShaderUvec3(Shader,"viewPos",camPos);

        ShaderUmatf4(Shader,"model",glm::scale(model,glm::vec3(1.0)));
        m.Draw();
        ShaderUmatf4(Shader,"model",glm::scale(glm::translate(model,glm::vec3(0.5)),glm::vec3(1.0)));
        m.Draw();

    };

    

    // std::vector<glm::vec3> ssaoNoise;
    // for (GLuint i = 0; i < 16; i++)
    // {
    //     glm::vec3 noise(
    //         randomFloats(generator) * 2.0 - 1.0, 
    //         randomFloats(generator) * 2.0 - 1.0, 
    //         0.0f); 
    //     ssaoNoise.push_back(noise);
    // }

    // GLuint noiseTexture; 
    // glGenTextures(1, &noiseTexture);
    // glBindTexture(GL_TEXTURE_2D, noiseTexture);
    // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    const int noiseSize = 4;

    std::vector<glm::vec3> ssaoNoise;
    for (unsigned int i = 0; i < noiseSize*noiseSize; i++)
    {
        glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f); // rotate around z-axis (in tangent space)
        ssaoNoise.push_back(noise);
    }
    unsigned int noiseTexture; 
    glGenTextures(1, &noiseTexture);
    glBindTexture(GL_TEXTURE_2D, noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, noiseSize, noiseSize, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);


    GLuint ssaoFBO;
    glGenFramebuffers(1, &ssaoFBO);  
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    GLuint ssaoColorBuffer;
    glGenTextures(1, &ssaoColorBuffer);
    glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorBuffer, 0);

    GLuint ssaoBlurFBO;
    glGenFramebuffers(1,&ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER,ssaoBlurFBO);
    GLuint ssaoColorBufferBlur;
    glGenTextures(1,&ssaoColorBufferBlur);
    glBindTexture(GL_TEXTURE_2D,ssaoColorBufferBlur);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,SCR_WIDTH,SCR_HEIGHT,0,GL_RGB,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,ssaoColorBufferBlur,0);

    glClearColor(0.00f, 0.00f, 0.00f, 1.0f);
    glm::mat4 a;
    a[3][0] = 1; // Fourth column, first row
    a[3][1] = 2;
    a[3][2] = 3;
    a[0][0] = 4; // Fourth column, first row
    a[1][0] = 5;
    a[2][0] = 6;
    for(int i = 0;i<16;i++){
        std::cout<<*(glm::value_ptr(a)+i)<<std::endl;
    }
    //循环
    while (!glfwWindowShouldClose(window))
    {
        //get currentTime & deltaTime
        float currentTime = glfwGetTime();
        deltaTime = currentTime - latestTime;
        latestTime = deltaTime + latestTime;
        std::stringstream titleStream;
        titleStream << "LearnOpenGL FPS: " << std::fixed << std::setprecision(2) << (int)(1.0/deltaTime);
        glfwSetWindowTitle(window, titleStream.str().c_str());
        //处理输入
        processInput(window);
        //摄像机变速
        float camMove;
        if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)){
            camSpeed += deltaTime*3;
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
        if(glfwGetKey(window,GLFW_KEY_RIGHT)){
            radius += 0.001;
            s.Use();
            ShaderU1f(s,"radius",radius);
            std::cout<<radius<<std::endl;
        }
        if(glfwGetKey(window,GLFW_KEY_LEFT)){
            s.Use();
            radius -= 0.001;
            ShaderU1f(s,"radius",radius);
            std::cout<<radius<<std::endl;
        }
        //摄像机矩阵处理
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 

        float z[3] = {-camFront.x,-camFront.y,-camFront.z};
        float zd = std::sqrt(z[0]*z[0] + z[1]*z[1] + z[2]*z[2]);
        z[0] /= zd;z[1] /= zd;z[2] /= zd;
        float up[3] = {0.0,1.0,0.0};
        float x[3] = {up[1]*z[2]-up[2]*z[1],up[2]*z[0]-up[0]*z[2],up[0]*z[1]-up[1]*z[0]};
        float xd = std::sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
        x[0] /= xd;x[1] /= xd;x[2] /= xd;
        float y[3] = {z[1]*x[2]-z[2]*x[1],z[2]*x[0]-z[0]*x[2],z[0]*x[1]-z[1]*x[0]};
        float yd = std::sqrt(y[0]*y[0] + y[1]*y[1] + y[2]*y[2]);
        y[0] /= yd;y[1] /= yd;y[2] /= yd;
        
        float translate[16] = {
            x[0],x[1],x[2],-camPos[0] * x[0] -camPos[1] * x[1] - camPos[2] * x[2],
            y[0],y[1],y[2],-camPos[0] * y[0] -camPos[1] * y[1] - camPos[2] * y[2],
            z[0],z[1],z[2],-camPos[0] * z[0] -camPos[1] * z[1] - camPos[2] * z[2],
            0,0,0,1.0
        };
     
       
        GbufferShader.Use();
        glUniformMatrix4fv(glGetUniformLocation(GbufferShader.getShaderProgramID(),"view"),1,GL_TRUE,&translate[0]);
        modelDisplay.Use();
        glUniformMatrix4fv(glGetUniformLocation(modelDisplay.getShaderProgramID(),"view"),1,GL_TRUE,&translate[0]);



        view = glm::lookAt(camPos,camFront+camPos,camUp);

        //逻辑处理代码写在这里：

        //渲染
        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER,gBuffer);
        glClearColor(0.0,0.0,100.0,1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.0,0.0,0.0,1.0);
        RenderScene(GbufferShader);
        //SSAO
        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER,ssaoFBO);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBindVertexArray(QuadVAO);
        s.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,gPositionDepth);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,gNormal);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D,noiseTexture);
        glDrawArrays(GL_TRIANGLES,0,6);
        //Main
            // glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
            // glBindFramebuffer(GL_FRAMEBUFFER,0);
            // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // RenderScene(modelDisplay);
        //ssaoBlur
        glBindFramebuffer(GL_FRAMEBUFFER,ssaoBlurFBO);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ssaoBlur.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,ssaoColorBuffer);
        glBindVertexArray(QuadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);
        //blend
        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        afterLightShader.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,gAlbedoSpec);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,ssaoColorBufferBlur);
        glBindVertexArray(QuadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);
        //UI
        glDisable(GL_DEPTH_TEST);
        screenShader.Use();
        glBindVertexArray(QuadVAO);
        glActiveTexture(GL_TEXTURE0);
        glViewport(0, SCR_HEIGHT * 0.75f, SCR_WIDTH * 0.25f, SCR_HEIGHT * 0.25f);
        glBindTexture(GL_TEXTURE_2D,gPositionDepth);
        glDrawArrays(GL_TRIANGLES,0,6);
        glViewport(0, 0, SCR_WIDTH * 0.25f, SCR_HEIGHT * 0.25f);
        glBindTexture(GL_TEXTURE_2D,gAlbedoSpec);
        glDrawArrays(GL_TRIANGLES,0,6);
        glViewport(0, SCR_HEIGHT * 0.25f, SCR_WIDTH * 0.25f, SCR_HEIGHT * 0.5f);
        glBindTexture(GL_TEXTURE_2D,gNormal);
        glDrawArrays(GL_TRIANGLES,0,6);
        glViewport(SCR_WIDTH * 0.75f, SCR_HEIGHT * 0.75f, SCR_WIDTH * 0.25f, SCR_HEIGHT * 0.25f);
        glBindTexture(GL_TEXTURE_2D,ssaoColorBufferBlur);
        glDrawArrays(GL_TRIANGLES,0,6);
        glEnable(GL_DEPTH_TEST);
        
        //渲染主要逻辑写在这里
        
        
        
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