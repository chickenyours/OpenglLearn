#include <iostream>
#include <iomanip>
#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "stb_image.h" 

#include "code/Model/Model.h"

//窗口信息
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;
//函数
unsigned int loadTexture(char const * path,unsigned int warpS,unsigned int warpT, bool verticalflip = false);
//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
//camera
glm::vec3 camPos(0.0,0.0,0.0);
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

float roughness = 0.5f;
float metallic = 0.14f;

//全局变量代码写在这里:
bool useBPR = true; // 初始使用 BPR 着色器
bool yKeyPressed = false; // 用于避免抖动、
bool pkeyPressed = false; // 用于避免抖动
bool useIrradianceMap = true; // 是否使用辐照度贴图

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
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);                                     //立方体贴图无缝
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);                                    //初始化视口
    //设置对话窗口属性
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);                //隐藏鼠标
    glfwSetCursorPos(window,SCR_WIDTH/2,SCR_HEIGHT/2);                          //设定鼠标初始位置
    //注册基本事件
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);          
    glfwSetCursorPosCallback(window,mouse_callback);
    glfwSetKeyCallback(window, keyCallback);
    //初始化的代码写在这里：
    const glm::mat4 model(1.0f);
    glm::mat4 view          = glm::lookAt(camPos,camPos+camFront,camUp);
    glm::mat4 projection    = glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/(float)SCR_HEIGHT,0.1f,500.0f);

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

float quad[] = {
    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,

    -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 0.0f, 1.0f, 1.0f
};

const float cubeWidth = 5.0f;
const float cubeHeight = 5.0f;
const int cubeNum = 10;

float cubeWidthinterval = cubeWidth/cubeNum;
float cubeHeightinterval = cubeHeight/cubeNum;
std::vector<glm::vec3> cubePositions;
// for(int i = 0;i<cubeNum;i++){
//     for(int j = 0;j<cubeNum;j++){
//         cubePositions.push_back(glm::vec3(i * cubeWidthinterval * 2.0f - cubeWidth,j * cubeHeightinterval * 2.0f - cubeHeight,5.0f));
//     }
// }
cubePositions.push_back(glm::vec3( 0.0f,  0.0f,  3.0f));

// glm::vec3 cubePositions[] = {
//     glm::vec3( 0.0f,  0.0f,  0.0f), 
//     glm::vec3( 2.0f,  5.0f, -15.0f), 
//     glm::vec3(-1.5f, -2.2f, -2.5f),  
//     glm::vec3(-3.8f, -2.0f, -12.3f),  
//     glm::vec3( 2.4f, -0.4f, -3.5f),  
//     glm::vec3(-1.7f,  3.0f, -7.5f),  
//     glm::vec3( 1.3f, -2.0f, -2.5f),  
//     glm::vec3( 1.5f,  2.0f, -2.5f), 
//     glm::vec3( 1.5f,  0.2f, -1.5f), 
//     glm::vec3(-1.3f,  1.0f, -1.5f)  
// };
    
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

    unsigned int quadVAO,quadVBO;
    glGenVertexArrays(1,&quadVAO);
    glBindVertexArray(quadVAO);
    glGenBuffers(1,&quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER,quadVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    MyTool::Model m("./models/Oil_barrel/Oil_barrel.obj");
    GLint albedoMap = loadTexture("./models/Oil_barrel/textures/Oil_barrel_Albedo.png",GL_REPEAT,GL_REPEAT);
    GLint normalMap = loadTexture("./models/Oil_barrel/textures/Oil_barrel_Normal.png",GL_REPEAT,GL_REPEAT);
    GLint metallicMap = loadTexture("./models/Oil_barrel/textures/Oil_barrel_Metallic.png",GL_REPEAT,GL_REPEAT);
    GLint roughnessMap = loadTexture("./models/Oil_barrel/textures/Oil_barrel_Roughness.png",GL_REPEAT,GL_REPEAT);
    GLint aoMap = loadTexture("./models/Oil_barrel/textures/Oil_barrel_AO.png",GL_REPEAT,GL_REPEAT);

    ShaderProgram shaderProgram("Final/base_render_world.vs","Final/single_color.fs");
    shaderProgram.Use();
    ShaderUmatf4(shaderProgram,"model",model);
    ShaderUmatf4(shaderProgram,"projection",projection);

    ShaderProgram BPR("Final/base_render_world.vs","Final/bprat.fs");
    BPR.Use();
    ShaderUmatf4(BPR,"model",model);
    ShaderUmatf4(BPR,"projection",projection);
    const float lightdistance = 3.0f;
    glm::vec3 lightPositions[] = {
        glm::vec3(lightdistance,lightdistance, 0.0f),
        glm::vec3(-lightdistance,lightdistance, 0.0f),
        glm::vec3(lightdistance,-lightdistance, 0.0f),
        glm::vec3(-lightdistance,-lightdistance, 0.0f)
    };
    const float lightinsensity = 50.0f;
    glm::vec3 lightColors[] = {
        glm::vec3(lightinsensity, lightinsensity, lightinsensity),
        glm::vec3(lightinsensity, lightinsensity, lightinsensity),
        glm::vec3(lightinsensity, lightinsensity, lightinsensity),
        glm::vec3(lightinsensity, lightinsensity, lightinsensity)
    };

    for (unsigned int i = 0; i < 4; ++i) {
        std::string posName = "lightPositions[" + std::to_string(i) + "]";
        std::string colorName = "lightColors[" + std::to_string(i) + "]";
        ShaderUvec3(BPR, posName.c_str(), lightPositions[i]);
        ShaderUvec3(BPR, colorName.c_str(), lightColors[i]);
    }

    ShaderUvec3(BPR, "camPos", camPos);
    ShaderUvec3(BPR, "albedo", glm::vec3(1.0, 0.0, 1.0));
    ShaderU1f(BPR, "metallic", metallic);
    ShaderU1f(BPR, "roughness", roughness);
    ShaderU1f(BPR, "ao", 1.0f);
    ShaderU1i(BPR,"irradianceMap",0);
    ShaderU1i(BPR,"prefilterMap",1);
    ShaderU1i(BPR,"brdfLUT",2);
    ShaderU1i(BPR,"albedoMap",3);
    ShaderU1i(BPR,"normalMap",4);
    ShaderU1i(BPR,"metallicMap",5);
    ShaderU1i(BPR,"roughnessMap",6);
    ShaderU1i(BPR,"aoMap",7);

    ShaderProgram blingPhoneShader("Final/base_render_world.vs","Final/bling_phone.fs");
    blingPhoneShader.Use();
    ShaderUmatf4(blingPhoneShader, "model", model);
    ShaderUmatf4(blingPhoneShader, "projection", projection);
    ShaderUvec3(blingPhoneShader, "viewPos", camPos);
    ShaderUvec3(blingPhoneShader,"material.color",glm::vec3(1.0f, 0.0f, 0.0f));
    ShaderUvec3(blingPhoneShader, "material.diffuse", glm::vec3(1.0));
    ShaderUvec3(blingPhoneShader, "material.specular", glm::vec3(1.0f));
    ShaderU1f(blingPhoneShader, "material.shininess", 32.0f);
    for (unsigned int i = 0; i < 4; ++i) {
        std::string posName = "plight[" + std::to_string(i) + "].pos";
        std::string colorName = "plight[" + std::to_string(i) + "].color";
        ShaderUvec3(blingPhoneShader, posName.c_str(), lightPositions[i]);
        ShaderUvec3(blingPhoneShader, colorName.c_str(), glm::vec3(1.0,0.0,0.0));
    }
    for (unsigned int i = 0; i < 4; ++i) {
        std::string constantName = "plight[" + std::to_string(i) + "].constant";
        std::string linearName = "plight[" + std::to_string(i) + "].linear";
        std::string quadraticName = "plight[" + std::to_string(i) + "].quadratic";
        std::string ambientName = "plight[" + std::to_string(i) + "].ambient";
        std::string diffuseName = "plight[" + std::to_string(i) + "].diffuse";
        std::string specularName = "plight[" + std::to_string(i) + "].specular";

        ShaderU1f(blingPhoneShader, constantName.c_str(), 1.0f);
        ShaderU1f(blingPhoneShader, linearName.c_str(), 0.09f);
        ShaderU1f(blingPhoneShader, quadraticName.c_str(), 0.032f);
        ShaderUvec3(blingPhoneShader, ambientName.c_str(), glm::vec3(0.1f, 0.1f, 0.1f));
        ShaderUvec3(blingPhoneShader, diffuseName.c_str(), glm::vec3(0.8f, 0.8f, 0.8f));
        ShaderUvec3(blingPhoneShader, specularName.c_str(), glm::vec3(1.0f, 1.0f, 1.0f));
    }

    ShaderProgram skyShader("Final/skybox.vs","Final/skybox.fs");
    skyShader.Use();
    ShaderUmatf4(skyShader,"projection",projection);
    ShaderUmatf4(skyShader,"model",model);
    ShaderU1i(skyShader,"cubeMap",0);


    unsigned int empty_play_room_texture = loadTexture("./images/studio_small_03_1k.hdr", GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);

    // 渲染资源初始化

    unsigned int captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);  
    unsigned int envCubemap;
    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        // note that we store each face with 16 bit floating point values
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 
                    512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = 
    {
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    // convert HDR equirectangular environment map to cubemap equivalent
    ShaderProgram equiRectangularToCubemapShader("Final/other/projection.vs", "Final/other/equirectangular_2_cube.fs");
    equiRectangularToCubemapShader.Use();
    ShaderU1i(equiRectangularToCubemapShader, "equirectangularMap", 0);
    ShaderUmatf4(equiRectangularToCubemapShader, "projection", captureProjection);

   
    // pbr: create an irradiance cubemap, and re-scale capture FBO to irradiance scale.
    glViewport(0, 0, 512, 512); 
    glBindVertexArray(VAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, empty_play_room_texture);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        ShaderUmatf4(equiRectangularToCubemapShader,"view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);  
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    // 环境贴图漫反射辐照度计算
    //创建一个辐照度贴图
    unsigned int irradianceMap;
    glGenTextures(1, &irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);
    // 计算辐照度贴图
    ShaderProgram irradianceShader("Final/other/projection.vs","Final/other/cubemap_irradianceIBLMap.fs");
    irradianceShader.Use();
    ShaderU1i(irradianceShader,"environmentMap",0);
    ShaderUmatf4(irradianceShader,"projection",captureProjection);
    glViewport(0, 0, 32, 32);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        ShaderUmatf4(irradianceShader,"view",captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind the framebuffer

    // 创建一个预过滤环境贴图
    unsigned int prefilterMap;
    glGenTextures(1, &prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 128, 128, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); 
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // 计算预过滤环境贴图
    ShaderProgram prefilterShader("Final/other/projection.vs","Final/other/cubemap_prefilterIBLMap.fs");
    prefilterShader.Use();
    ShaderU1i(prefilterShader,"environmentMap",0);
    ShaderUmatf4(prefilterShader,"projection",captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMipLevels = 5;
    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipWidth  = 128 * std::pow(0.5, mip);
        unsigned int mipHeight = 128 * std::pow(0.5, mip);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);
        float roughness = (float)mip / (float)(maxMipLevels - 1);
        ShaderU1f(prefilterShader,"roughness",roughness);
        for (unsigned int i = 0; i < 6; ++i)
        {
            ShaderUmatf4(prefilterShader,"view",captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindVertexArray(VAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    //预计算 BRDF 2D LUT 贴图
    ShaderProgram brdfShader("Final/screen.vs","Final/other/brdf.fs");
    unsigned int brdfLUTTexture;
    glGenTextures(1, &brdfLUTTexture);
    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);

    glViewport(0, 0, 512, 512);
    brdfShader.Use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);  
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    
    // 渲染资源初始化结束

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
        // 实时修改 roughness 和 metallic
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            roughness += 0.01f;
            if (roughness > 1.0f) roughness = 1.0f;
            std::cout << "Roughness: " << roughness << std::endl;
            BPR.Use();
            ShaderU1f(BPR, "roughness", roughness);
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            roughness -= 0.01f;
            if (roughness < 0.0f) roughness = 0.0f;
            std::cout << "Roughness: " << roughness << std::endl;
            BPR.Use();
            ShaderU1f(BPR, "roughness", roughness);
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            metallic += 0.01f;
            if (metallic > 1.0f) metallic = 1.0f;
            std::cout << "Metallic: " << metallic << std::endl;
            BPR.Use();
            ShaderU1f(BPR, "metallic", metallic);
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            metallic -= 0.01f;
            if (metallic < 0.0f) metallic = 0.0f;
            std::cout << "Metallic: " << metallic << std::endl;
            BPR.Use();
            ShaderU1f(BPR, "metallic", metallic);
        }

        // 切换着色器
        if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS && !yKeyPressed) {
            useBPR = !useBPR;
            yKeyPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_RELEASE) {
            yKeyPressed = false;
        }
        // 切换辐照度贴图
        if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !pkeyPressed) {
            useIrradianceMap = !useIrradianceMap;
            pkeyPressed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE) {
            pkeyPressed = false;
        }
        //摄像机矩阵处理
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 
        view = glm::lookAt(camPos,camFront+camPos,camUp);

        //逻辑处理代码写在这里：
        
        //渲染
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        //天空盒
        glDepthFunc(GL_LEQUAL);
        skyShader.Use();
        ShaderUmatf4(skyShader, "view", view);
        glActiveTexture(GL_TEXTURE0);
        //glBindTexture(GL_TEXTURE_CUBE_MAP, useIrradianceMap ? irradianceMap : envCubemap);
        glBindTexture(GL_TEXTURE_CUBE_MAP,envCubemap);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthFunc(GL_LESS);
    
        if (useBPR) {
            BPR.Use();
            ShaderUmatf4(BPR, "view", view);
            ShaderUvec3(BPR,"viewPos",camPos);
        } else {
            blingPhoneShader.Use();
            ShaderUmatf4(blingPhoneShader, "view", view);
            ShaderUvec3(blingPhoneShader,"viewPos",camPos);
        }

        // 绘制场景中的物体
        for (unsigned int i = 0; i <cubePositions.size(); i++) {
            // float matelic = 0.98 *(float)(i/cubeNum) / cubeNum ;
            // float roughness = (float)(i%cubeNum) / cubeNum;
            if (useBPR) {
                BPR.Use();
                ShaderU1f(BPR, "roughness", roughness);
                ShaderU1f(BPR, "metallic", metallic);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_CUBE_MAP,irradianceMap);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_CUBE_MAP,prefilterMap);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D,brdfLUTTexture);
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D,albedoMap);
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D,normalMap);
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D,metallicMap);
                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_2D,roughnessMap);
                glActiveTexture(GL_TEXTURE7);
                glBindTexture(GL_TEXTURE_2D,aoMap);
            } else {
                blingPhoneShader.Use();
                ShaderU1f(blingPhoneShader, "material.shininess", 32.0f * metallic);
            }
            ShaderUmatf4(useBPR ? BPR : blingPhoneShader, "model",glm::rotate(glm::scale(glm::translate(model, cubePositions[i]),glm::vec3(0.2f)),3.1415f/2.0f,glm::vec3(0.0f,1.0f,0.0f)));
            for(int j = 0 ;j<m._meshArray.size();j++){
                glBindVertexArray(m._meshArray[j].VAO);
                glDrawElements(GL_TRIANGLES,m._meshArray[j].indicesCount,GL_UNSIGNED_INT,0);
            }
        }
        

        // 绘制光源位置的小正方体
        shaderProgram.Use();
        ShaderUmatf4(shaderProgram, "view", view);
        for (unsigned int i = 0; i < 4; i++) {
            glm::mat4 lightModel = glm::mat4(1.0f);
            lightModel = glm::translate(lightModel, lightPositions[i]);
            lightModel = glm::scale(lightModel, glm::vec3(0.2f)); // 缩小正方体
            ShaderUmatf4(shaderProgram, "model", lightModel);
            glBindVertexArray(VAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        // //查看 BRDF LUT 贴图
        // glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // brdfShader.use();
        // glBindVertexArray(quadVAO);
        // glActiveTexture(GL_TEXTURE0);
        // glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
        // glDrawArrays(GL_TRIANGLES, 0, 6);


        // 渲染结束

        // 渲染主要逻辑写在这里

        glfwSwapBuffers(window); // 交换缓冲
        glfwPollEvents(); // 事件响应
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

unsigned int loadTexture(char const * path,unsigned int warpS,unsigned int warpT, bool verticalflip)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(verticalflip);
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
        //glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, warpS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, warpS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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