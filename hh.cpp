#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdio>
#include <thread>

#define ENHANCE 0.002

//窗口信息
const unsigned int SCR_WIDTH = 1024;
const unsigned int SCR_HEIGHT =1024;
//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void checkOpenGLError();
//camera
glm::vec3 camPos(0.0,3.0,0.0);
glm::vec3 camFront(0.0f,-0.99f,0.0f);
glm::vec3 camUp(0.0f,1.0f,0.0f);
float pitch = -90.0f;
float yaw = 0.0f;
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
int threadquit = 0;

int const level = 9;
float growRate = 0.0;
float weightBase = 1.0;

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
    //错误处理线程
    //std::thread errorThread(checkOpenGLError);
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
    glm::mat4 projection    = glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/(float)SCR_HEIGHT,0.1f,500.0f);

    glm::mat4 cubePos = glm::translate(model,glm::vec3(0.00));

    float vertex[] = {1.0,1.0,0.0,1.0,1.0,
                      -1.0,1.0,0.0,0.0,1.0,
                      -1.0,-1.0,0.0,0.0,0.0,
                      -1.0,-1.0,0.0,0.0,0.0,
                      1.0,-1.0,0.0,1.0,0.0,   
                      1.0,1.0,0.0,1.0,1.0, 
    };
    unsigned int indeies[] = {0,1,2,
                            2,3,0};

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

float planeVertices[] = {
        // positions            // normals         // texcoords
         25.0f, -0.5f,  25.0f,  0.0f, 1.0f, 0.0f,  25.0f,  0.0f,
        -25.0f, -0.5f,  25.0f,  0.0f, 1.0f, 0.0f,   0.0f,  0.0f,
        -25.0f, -0.5f, -25.0f,  0.0f, 1.0f, 0.0f,   0.0f, 25.0f,

         25.0f, -0.5f,  25.0f,  0.0f, 1.0f, 0.0f,  25.0f,  0.0f,
        -25.0f, -0.5f, -25.0f,  0.0f, 1.0f, 0.0f,   0.0f, 25.0f,
         25.0f, -0.5f, -25.0f,  0.0f, 1.0f, 0.0f,  25.0f, 25.0f
    };

    // plane VAO
    unsigned int planeVAO,planeVBO;
    glGenVertexArrays(1, &planeVAO);
    glGenBuffers(1, &planeVBO);
    glBindVertexArray(planeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, planeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(planeVertices), planeVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
 

   

    ShaderProgram sceneShaderProgram("AfterRender/single.vs","AfterRender/single.fs");
    ShaderProgram screenShaderProgram("AfterRender/after.vs","AfterRender/screen.fs");
    ShaderProgram filterShaderProgram("AfterRender/after.vs","AfterRender/filter.fs");
    ShaderProgram gaussShaderProgram("AfterRender/after.vs","AfterRender/gauss.fs");
    ShaderProgram blendShaderPrograme("AfterRender/after.vs","AfterRender/blend.fs");
    ShaderProgram upSampleProgram("AfterRender/after.vs","AfterRender/upSample.fs");
    ShaderProgram simpleBlendProgram("AfterRender/after.vs","AfterRender/simpleblend.fs");

    blendShaderPrograme.Use();
    ShaderU1i(blendShaderPrograme,"sceneColorBuffer",0);
    ShaderU1i(blendShaderPrograme,"outputColorBuffer",1);

    upSampleProgram.Use();
    ShaderU1i(upSampleProgram,"downTex",0);
    ShaderU1i(upSampleProgram,"preTex",1);
    ShaderU1f(upSampleProgram,"baseWeight",1.0);
    ShaderU1f(upSampleProgram,"growthRate",0.0);
    ShaderU1i(upSampleProgram,"level",level);
    

    unsigned int VBO,VAO;
    glGenVertexArrays(1,&VAO);
    glBindVertexArray(VAO);
    glGenBuffers(1,&VBO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,cube_vertex.size() * sizeof(float),&cube_vertex[0],GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(float),(void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    
    unsigned int QuadVBO,QuadVAO;
    glGenVertexArrays(1,&QuadVAO);
    glBindVertexArray(QuadVAO);
    glGenBuffers(1,&QuadVBO);
    glBindBuffer(GL_ARRAY_BUFFER,QuadVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertex),&vertex[0],GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5 * sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5 * sizeof(float),(void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    //多重采样缓冲区
    unsigned int framebuffer;
    glGenFramebuffers(1,&framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    unsigned int texMultColorBuffer;
    glGenTextures(1,&texMultColorBuffer);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,texMultColorBuffer);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,16,GL_RGBA16F,SCR_WIDTH,SCR_HEIGHT,0);
        //这里不需要给纹理设置纹理参数，因为此纹理仅仅作为渲染目标，没有用来读取
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D_MULTISAMPLE,texMultColorBuffer,0);
    unsigned int rbo;
    glGenRenderbuffers(1,&rbo);     //!!!注意不是glGenBuffers,否则出现崩溃
    glBindBuffer(GL_RENDERBUFFER,rbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,16,GL_DEPTH24_STENCIL8,SCR_WIDTH,SCR_HEIGHT);
    glBindBuffer(GL_RENDERBUFFER,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    unsigned int sceneFBO,sceneColorBuffer,sceneDepthBuffer;
    glGenFramebuffers(1,&sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO);
    glGenTextures(1,&sceneColorBuffer);
    glBindTexture(GL_TEXTURE_2D,sceneColorBuffer);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH,SCR_HEIGHT,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,sceneColorBuffer,0);
    glGenRenderbuffers(1,&sceneDepthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER,sceneDepthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,SCR_WIDTH,SCR_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL,GL_RENDERBUFFER,sceneDepthBuffer);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"error create sceneFBO"<<std::endl;
    }

    unsigned int blurFBO[level+1];
    unsigned int blurColorBuffer1[level+1];
    unsigned int blurColorBuffer2[level+1];
    unsigned int blurColorBuffer3[level+1];
    glGenFramebuffers(level+1,blurFBO);
    glGenTextures(level,blurColorBuffer1);
    glGenTextures(level,blurColorBuffer2);
    for(int i=0;i<level+1;i++){
        int scale = 1 << i;
        glBindFramebuffer(GL_FRAMEBUFFER,blurFBO[i]);
        unsigned int texture1;
        glGenTextures(1,&texture1);
        glBindTexture(GL_TEXTURE_2D,texture1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH/scale,SCR_HEIGHT/scale,0,GL_RGBA,GL_FLOAT,NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture1,0);
        blurColorBuffer1[i] = texture1;
        if(i!=0){
            unsigned int texture2;
            glGenTextures(1,&texture2);
            glBindTexture(GL_TEXTURE_2D,texture2);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH/scale,SCR_HEIGHT/scale,0,GL_RGBA,GL_FLOAT,NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,texture2,0);
            blurColorBuffer2[i] = texture2;
            unsigned int texture3;
            glGenTextures(1,&texture3);
            glBindTexture(GL_TEXTURE_2D,texture3);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH/scale,SCR_HEIGHT/scale,0,GL_RGBA,GL_FLOAT,NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT2,GL_TEXTURE_2D,texture3,0);
            blurColorBuffer3[i] = texture3;

        }
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
            std::cout<<"error create blurFBO"<<std::endl;
        }
    }

    int k = 0;
    bool flag_o = true;
    bool flag_p = true;
    bool show = true;
    bool b = true;

    glClearColor(0.0,0.0,0.0,0.0);
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
        if(glfwGetKey(window,GLFW_KEY_I)){
            if(b){
                b = false;
                show = !show;
            }
        }
        else{
            b = true;
        }
        if(glfwGetKey(window,GLFW_KEY_O)){
            if(flag_o){
                k+=1;
                if(k>level){
                    k = level;
                }
                flag_o = false;
            }
        }
        else{
            flag_o = true;
        }
        if(glfwGetKey(window,GLFW_KEY_P)){
            if(flag_p){
                k-=1;
                if(k<0){
                    k = 0;
                }
                flag_p = false;
            }
        }
        else{
            flag_p = true;
        }
        if(glfwGetKey(window,GLFW_KEY_V)){
            gaussShaderProgram.Use();
            ShaderU1i(gaussShaderProgram,"show",0);
        }
        else{
            gaussShaderProgram.Use();
            ShaderU1i(gaussShaderProgram,"show",1);
        }

        if(glfwGetKey(window,GLFW_KEY_UP)){
            growRate += ENHANCE;
            upSampleProgram.Use();
            ShaderU1f(upSampleProgram,"growthRate",growRate);
            std::cout<<"growRate:"<<growRate<<std::endl;
        }
        if(glfwGetKey(window,GLFW_KEY_DOWN)){
            growRate -= ENHANCE;
            std::cout<<"growRate:"<<growRate<<std::endl;
            upSampleProgram.Use();
            ShaderU1f(upSampleProgram,"growthRate",growRate);
        }
        if(glfwGetKey(window,GLFW_KEY_RIGHT)){
            weightBase += ENHANCE;
            std::cout<<"weightBase:"<<weightBase<<std::endl;
            upSampleProgram.Use();
            ShaderU1f(upSampleProgram,"baseWeight",weightBase);
        }
        if(glfwGetKey(window,GLFW_KEY_LEFT)){
            weightBase -= ENHANCE;
            std::cout<<"weightBase:"<<weightBase<<std::endl;
            upSampleProgram.Use();
            ShaderU1f(upSampleProgram,"baseWeight",weightBase);
        }

        
        //摄像机矩阵处理
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 
        view = glm::lookAt(camPos,camFront+camPos,camUp);

        //glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glBindVertexArray(planeVAO);
        sceneShaderProgram.Use();
        ShaderUmatf4(sceneShaderProgram,"projection",projection);
        ShaderUmatf4(sceneShaderProgram,"model",glm::scale(model,glm::vec3(0.4,0.2,0.1)));
        ShaderUmatf4(sceneShaderProgram,"view",view);
        glDrawArrays(GL_TRIANGLES,0,6);

        //纹理复制
        glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,sceneFBO);
        glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER,blurFBO[0]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindVertexArray(QuadVAO);
        filterShaderProgram.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,sceneColorBuffer);
        glDrawArrays(GL_TRIANGLES,0,6);
        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);

        gaussShaderProgram.Use();
        for(int i = 1;i<level+1;i++){
            int scale = 1 << i;
            glViewport(0,0,SCR_WIDTH/scale,SCR_HEIGHT/scale);
            glBindFramebuffer(GL_FRAMEBUFFER,blurFBO[i]);
            glBindVertexArray(QuadVAO);

            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glClear(GL_COLOR_BUFFER_BIT);
            screenShaderProgram.Use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[i-1]);
            glDrawArrays(GL_TRIANGLES,0,6);

            glDrawBuffer(GL_COLOR_ATTACHMENT1);
            glClear(GL_COLOR_BUFFER_BIT);
            gaussShaderProgram.Use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[i]);
            ShaderU1i(gaussShaderProgram,"isy",1);
            glDrawArrays(GL_TRIANGLES,0,6);

            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glClear(GL_COLOR_BUFFER_BIT);
            gaussShaderProgram.Use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer2[i]);
            ShaderU1i(gaussShaderProgram,"isy",0);
            glDrawArrays(GL_TRIANGLES,0,6);
        } 

        //上采样
        //初始
        glViewport(0,0,SCR_WIDTH/(1<<level),SCR_HEIGHT/(1<<level));
        glBindFramebuffer(GL_FRAMEBUFFER,blurFBO[level]);
        glDrawBuffer(GL_COLOR_ATTACHMENT1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[level]);
        gaussShaderProgram.Use();
        glBindVertexArray(QuadVAO);
        ShaderU1i(gaussShaderProgram,"isy",1);
        glDrawArrays(GL_TRIANGLES,0,6);
        glDrawBuffer(GL_COLOR_ATTACHMENT2);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,blurColorBuffer2[level]);
        ShaderU1i(gaussShaderProgram,"isy",0);
        glDrawArrays(GL_TRIANGLES,0,6);
        for(int i=level-1;i>0;i--){
            int scale = 1 << i;
            glViewport(0,0,SCR_WIDTH/scale,SCR_HEIGHT/scale);
            gaussShaderProgram.Use();
            glBindVertexArray(QuadVAO);
            glBindFramebuffer(GL_FRAMEBUFFER,blurFBO[i]);
            glDrawBuffer(GL_COLOR_ATTACHMENT1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[i]);
            ShaderU1i(gaussShaderProgram,"isy",1);
            glDrawArrays(GL_TRIANGLES,0,6);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer2[i]);
            ShaderU1i(gaussShaderProgram,"isy",0);
            glDrawArrays(GL_TRIANGLES,0,6);
            //上采样叠加
            upSampleProgram.Use();
            ShaderU1i(upSampleProgram,"currentLevel",i);
            glBindVertexArray(QuadVAO);
            glDrawBuffer(GL_COLOR_ATTACHMENT2);
            glClear(GL_COLOR_BUFFER_BIT);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[i]);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer3[i+1]);
            glDrawArrays(GL_TRIANGLES,0,6);
        }

       


        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(QuadVAO);
        
       
         if(show){
            blendShaderPrograme.Use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,sceneColorBuffer);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer3[1]);
        }
        else{
            screenShaderProgram.Use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blurColorBuffer3[k]);
        }
        glDrawArrays(GL_TRIANGLES,0,6);
        
        
        glfwSwapBuffers(window);                                //交换缓冲
        glfwPollEvents();                                       //事件响应
    }
    //threadquit = 1;
    //errorThread.join();
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



void checkOpenGLError() {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        switch (err) {
            case GL_NO_ERROR:
                std::cout << "No error." << std::endl;
                break;
            case GL_INVALID_ENUM:
                std::cout << "Invalid enum value: " << err << std::endl;
                break;
            case GL_INVALID_VALUE:
                std::cout << "Invalid value: " << err << std::endl;
                break;
            case GL_INVALID_OPERATION:
                std::cout << "Invalid operation: " << err << std::endl;
                break;
            case GL_OUT_OF_MEMORY:
                std::cout << "Out of memory: " << err << std::endl;
                break;
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                std::cout << "Invalid framebuffer operation: " << err << std::endl;
                break;
            default:
                std::cout << "Unknown error: " << err << std::endl;
                break;
        }
        if(threadquit){
            break;
        }
    }
}


 // glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        // glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO);
        // glDrawBuffer(GL_COLOR_ATTACHMENT0);
        // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // glEnable(GL_BLEND);
        // glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        // glBindVertexArray(QuadVAO);
        // simpleBlendProgram.use();
        
        // glActiveTexture(GL_TEXTURE0);
        // glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[k]);
        // glDrawArrays(GL_TRIANGLES,0,6);
        // for(int i = 1;i<level+1;i++){
        //     glActiveTexture(GL_TEXTURE0);
           
        //     // glBindTexture(GL_TEXTURE_2D,sceneColorBuffer);
        //     // glActiveTexture(GL_TEXTURE1);
        //     glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[i]);
        //     glDrawArrays(GL_TRIANGLES,0,6);
        // }
        // glDisable(GL_BLEND);
        // glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        // glBindFramebuffer(GL_FRAMEBUFFER,sceneColorBuffer);
        // glDrawBuffer(GL_COLOR_ATTACHMENT0);
        // glClear(GL_COLOR_BUFFER_BIT);
        // glBindVertexArray(QuadVAO);
        // blendShaderPrograme.use();
        // for(int i=1;i<level+1;i++){
        //     glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[i]);
        //     glDrawArrays(GL_TRIANGLES,0,6);
        // }

 // if(show){
        //     glActiveTexture(GL_TEXTURE0);
        //     glBindTexture(GL_TEXTURE_2D,sceneColorBuffer);
        // }
        // else{
        //     glActiveTexture(GL_TEXTURE0);
        //     glBindTexture(GL_TEXTURE_2D,blurColorBuffer1[k]);
        // }