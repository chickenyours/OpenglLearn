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

//窗口信息
const unsigned int SCR_WIDTH = 964;
const unsigned int SCR_HEIGHT = 460;
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
                      1.0,-1.0,0.0,1.0,0.0    
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
    // float light = 10.0; 
    // float HDR[] = {light,light,light,1.0};
    // //生成HDR纹理
    // unsigned int hdrTextureArray;
    // glGenTextures(1, &hdrTextureArray);
    // glBindTexture(GL_TEXTURE_2D_ARRAY, hdrTextureArray);
    // glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    // glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, width, height, layers, 0, GL_RGBA, GL_FLOAT, nullptr);

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

    unsigned VAO,VBO,EBO;
    glGenVertexArrays(1,&VAO);
    glBindVertexArray(VAO);
    glGenBuffers(1,&VBO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertex),vertex,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glGenBuffers(1,&EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indeies),indeies,GL_STATIC_DRAW);
    
    ShaderProgram sceneShaderProgram("AfterRender/single.vs","AfterRender/single.fs");
    ShaderProgram screenShaderProgram("AfterRender/after.vs","AfterRender/screen.fs");
    ShaderProgram filterShaderProgram("AfterRender/gauss.fs","AfterRender/filter.fs");
    ShaderProgram gaussShaderProgram("AfterRender/gauss.fs","AfterRender/gauss.fs");

    sceneShaderProgram.Use();
    glUniformMatrix4fv(glGetUniformLocation(sceneShaderProgram.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(cubePos));
    glUniformMatrix4fv(glGetUniformLocation(sceneShaderProgram.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    screenShaderProgram.Use();
    glUniform1i(glGetUniformLocation(sceneShaderProgram.getShaderProgramID(),"colorBuffer"),0);
    filterShaderProgram.Use();
    glUniform1i(glGetUniformLocation(filterShaderProgram.getShaderProgramID(),"colorBuffer"),0);
    gaussShaderProgram.Use();
    glUniform1i(glGetUniformLocation(gaussShaderProgram.getShaderProgramID(),"colorBuffer"),0);
    

    // //创建mipmap链缓对象
    // unsigned int mipFBO,mipColorBuffer;
    // glGenFramebuffers(1,&mipFBO);
    // glBindFramebuffer(GL_FRAMEBUFFER,mipFBO);
    // glGenTextures(1,&mipColorBuffer);
    // glBindTexture(GL_TEXTURE_2D,mipColorBuffer);
    // glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,SCR_WIDTH,SCR_HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,mipColorBuffer,0);
    // int level = 0;
    // unsigned int mipBuffers[level+1];
    // mipBuffers[0] = mipColorBuffer;
    // int mW=SCR_WIDTH,mH=SCR_HEIGHT;
    // for(int i=1;i<=level;i++){
    //     unsigned int mipMapBuffer;
    //     glGenTextures(1, &mipMapBuffer);
    //     glBindTexture(GL_TEXTURE_2D, mipMapBuffer);
    //     // 计算新的宽度和高度
    //     mW >>= 1; // 每次减半
    //     mH >>= 1; // 每次减半
    //     if (mW <= 0 || mH <= 0) {
    //          break;
    //     }
    //     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mW, mH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    //     glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mipMapBuffer, 0);
    //     mipBuffers[i] = mipMapBuffer;
    // }
    // if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
    //     std::cout<<"error create mipFBO"<<std::endl;
    // }
    // glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    //创建虚拟场景缓冲对象
    unsigned int sceneFBO,sceneColorBuffer,sceneRBO;
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
    glGenRenderbuffers(1,&sceneRBO);
    glBindRenderbuffer(GL_RENDERBUFFER,sceneRBO);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,SCR_WIDTH,SCR_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL,GL_RENDERBUFFER,sceneRBO);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"error create sceneFBO"<<std::endl;
    }

    //创建虚拟场景缓冲对象(乒乓交替渲染)
    unsigned int mip1FBO,mip1ColorBuffer,mip2ColorBuffer;
    glGenFramebuffers(1,&mip1FBO);
    glBindFramebuffer(GL_FRAMEBUFFER,mip1FBO);
    glGenTextures(1,&mip1ColorBuffer);
    glBindTexture(GL_TEXTURE_2D,mip1ColorBuffer);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH,SCR_HEIGHT,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,mip1ColorBuffer,0);
    glGenTextures(1,&mip2ColorBuffer);
    glBindTexture(GL_TEXTURE_2D,mip2ColorBuffer);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH,SCR_HEIGHT,0,GL_RGBA,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,mip2ColorBuffer,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"error create sceneFBO"<<std::endl;
    }
    //下采样（Downsampling）和上采样（Upsampling）结合使用
    // 创建下采样帧缓冲
    // 创建纹理附件
    int a = 0x1;
    constexpr int level = 6;
    GLuint mipFBOs[level+1];
    GLuint mipTexs[level+1];
    GLuint mipBackTexs[level+1];
    glGenFramebuffers(level+1,mipFBOs);
    bool flag = true;
    for(int i = 0;i<level+1;i++){
        glBindFramebuffer(GL_FRAMEBUFFER,mipFBOs[i]);

        GLuint downsampleTexture;
        glGenTextures(1, &downsampleTexture);
        glBindTexture(GL_TEXTURE_2D, downsampleTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH/a, SCR_HEIGHT/a, 0, GL_RGBA, GL_FLOAT, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, downsampleTexture, 0);
        mipTexs[i] = downsampleTexture;
        if(flag){
            flag = false;
        }
        else{
            GLuint backdownsampleTexture;
            glGenTextures(1, &backdownsampleTexture);
            glBindTexture(GL_TEXTURE_2D, backdownsampleTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH/a, SCR_HEIGHT/a, 0, GL_RGBA, GL_FLOAT, NULL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, backdownsampleTexture, 0);
            mipBackTexs[i] = backdownsampleTexture;
        }
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
            std::cout<<"error create mipFBOs"<<std::endl;
        }
        a<<=1;
    }


    
    
    GLuint smallFBO;
    glGenFramebuffers(1, &smallFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, smallFBO);
    GLuint small;
    glGenTextures(1, &small);
    glBindTexture(GL_TEXTURE_2D, small);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH / 2, SCR_HEIGHT / 2, 0, GL_RGBA, GL_FLOAT, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, small, 0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"error create smallFBO"<<std::endl;
    }

    unsigned int iFBO;
    glGenFramebuffers(1,&iFBO);
    glBindFramebuffer(GL_FRAMEBUFFER,iFBO);
    unsigned int t;
    glGenTextures(1,&t);
    glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0, GL_RGBA16F, SCR_WIDTH , SCR_HEIGHT, 0, GL_RGBA, GL_FLOAT, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"error create smalliFBO"<<std::endl;
    }


    int k = 0;
    bool flag_o = true;
    bool flag_p = true;
    bool show = true;
    bool b = true;
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

        
        //摄像机矩阵处理
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 
        view = glm::lookAt(camPos,camFront+camPos,camUp);

        //场景渲染
        glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(VAO);
        filterShaderProgram.Use();
        //glUniformMatrix4fv(glGetUniformLocation(sceneShaderProgram.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        //glDrawArrays(GL_TRIANGLES,0,36);

        // glBindFramebuffer(GL_FRAMEBUFFER,iFBO);
        // glDrawBuffer(GL_COLOR_ATTACHMENT0);
        // glClear(GL_COLOR_BUFFER_BIT);
        // glBindVertexArray(VAO);
        
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);

        //后期处理
        //进入迭代
        // glBindFramebuffer(GL_FRAMEBUFFER,mipFBOs[0]);
        // glDrawBuffer(GL_COLOR_ATTACHMENT0);
        // glClearColor(0.00f, 0.00f, 0.00f, 1.0f);
        // glClear(GL_COLOR_BUFFER_BIT);
        // glBindVertexArray(VAO);
        // filterShaderProgram.use();
        // glBindTexture(GL_TEXTURE_2D,sceneColorBuffer);
        // glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        // for(int i =1;i<level+1;i++){
        //     glViewport(0,0,SCR_WIDTH>>i, SCR_HEIGHT>>i);
        //     glBindFramebuffer(GL_FRAMEBUFFER,mipFBOs[i]);
        //     glBindVertexArray(VAO);
        //     glClearColor(0.00f, 0.00f, 0.00f, 1.0f);
        //     glClear(GL_COLOR_BUFFER_BIT);
        //     //下采样
        //     glDrawBuffer(GL_COLOR_ATTACHMENT0);
        //     gaussShaderProgram.use();
        //     glBindTexture(GL_TEXTURE_2D,mipTexs[i-1]);
        //     glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        //     //显压缩
        //     glDrawBuffer(GL_COLOR_ATTACHMENT0);
        //     screenShaderProgram.use();
        //     glBindTexture(GL_TEXTURE_2D,mipTexs[i-1]);
        //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        //     glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        //     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        //     //一维模糊
        //     gaussShaderProgram.use();
        //     glDrawBuffer(GL_COLOR_ATTACHMENT1);
        //     glBindTexture(GL_TEXTURE_2D,mipTexs[i]);
        //     glUniform1i(glGetUniformLocation(gaussShaderProgram.getShaderProgramID(),"isy"),1);
            
        //     //另一维模糊
        //     glDrawBuffer(GL_COLOR_ATTACHMENT0);
        //     glBindTexture(GL_TEXTURE_2D,mipBackTexs[i]);
        //     glUniform1i(glGetUniformLocation(gaussShaderProgram.getShaderProgramID(),"isy"),0);
        //     glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        //     glViewport(0,0,SCR_WIDTH, SCR_HEIGHT);
        // }
        // //混合
        // glEnable(GL_BLEND);
        // glBlendFunc(GL_ONE, GL_ONE);
        // glBindFramebuffer(GL_FRAMEBUFFER,sceneFBO);
        // glDrawBuffer(GL_COLOR_ATTACHMENT0);
       
        // screenShaderProgram.use();
        // for(int i =0;i<level+1;i++){
        //     glBindTexture(GL_TEXTURE_2D,mipTexs[i]);
        //     glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        // }
        // glDisable(GL_BLEND);
        //屏幕渲染
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClearColor(0.00f, 0.00f, 0.00f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(VAO);
        screenShaderProgram.Use();
        if(show){
            glBindTexture(GL_TEXTURE_2D,sceneColorBuffer);
        }
        else{
            glBindTexture(GL_TEXTURE_2D,t);
        }
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        
        



        
        
        
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
