#include <iostream>
#include <iomanip>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/Model/Model.h"

//窗口信息
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;
//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
//函数
glm::mat4 calculate_model_matrix(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale);
//camera
glm::vec3 camPos(0.0,0.0,1.0);
glm::vec3 camFront(0.0f,0.0f,-1.0f);
glm::vec3 camUp(0.0f,1.0f,0.0f);
float pitch = 0.0f;
float yaw   = -90.0f;
float roll  = 0.0f;
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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
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
    const glm::mat4 model(1.0f);
    glm::mat4 view          = glm::lookAt(camPos,camPos+camFront,camUp);
    glm::mat4 projection    = glm::perspective(glm::radians(45.0f),(float)SCR_WIDTH/(float)SCR_HEIGHT,0.1f,500.0f);

    ShaderProgram solidShader("Final/base_render_world.vs","Final/single_color.fs");
    ShaderProgram transparentShader("Final/base_render_world.vs","transparent/transparent.fs");
    ShaderProgram compositeShader("Final/screen.vs","transparent/composite.fs");
    ShaderProgram screenShader("Final/screen.vs","Final/screen.fs");

    solidShader.Use();
    ShaderUmatf4(solidShader,"model",model);
    ShaderUmatf4(solidShader,"view",view);
    ShaderUmatf4(solidShader,"projection",projection);



    transparentShader.Use();
    ShaderUmatf4(transparentShader,"model",model);
    ShaderUmatf4(transparentShader,"view",view);
    ShaderUmatf4(transparentShader,"projection",projection);

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

glm::mat4 modelA = calculate_model_matrix(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
glm::mat4 modelB = calculate_model_matrix(glm::vec3(0.2f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
glm::mat4 modelC = calculate_model_matrix(glm::vec3(0.4f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
glm::vec4 colorA = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
glm::vec4 colorB = glm::vec4(0.0f, 1.0f, 0.0f, 0.5f);
glm::vec4 colorC = glm::vec4(0.0f, 0.0f, 1.0f, 0.5f); 
    
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

    unsigned int quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glBindVertexArray(quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    //创建混合缓冲对象
    unsigned int opaueFBO,transparentFBO;
    glGenFramebuffers(1,&opaueFBO);
    glGenFramebuffers(1,&transparentFBO);

    unsigned int opaqueTexture;
    glGenTextures(1,&opaqueTexture);
    glBindTexture(GL_TEXTURE_2D,opaqueTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH,SCR_HEIGHT,0,GL_RGBA,GL_HALF_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D,0);

    unsigned int depthTexture;
    glGenTextures(1,&depthTexture);
    glBindTexture(GL_TEXTURE_2D,depthTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,SCR_WIDTH,SCR_HEIGHT,0,GL_DEPTH_COMPONENT,GL_FLOAT,NULL);
    glBindTexture(GL_TEXTURE_2D,0);

    glBindFramebuffer(GL_FRAMEBUFFER,opaueFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,opaqueTexture,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depthTexture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"ERROR::FRAMEBUFFER:: Opaque framebuffer is not complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);

    unsigned int accumTexture;
    glGenTextures(1,&accumTexture);
    glBindTexture(GL_TEXTURE_2D,accumTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,SCR_WIDTH,SCR_HEIGHT,0,GL_RGBA,GL_HALF_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D,0);

    unsigned int revealTexture;
    glGenTextures(1,&revealTexture);
    glBindTexture(GL_TEXTURE_2D,revealTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,SCR_WIDTH,SCR_HEIGHT,0,GL_RED,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D,0);

    glBindFramebuffer(GL_FRAMEBUFFER,transparentFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,accumTexture,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,revealTexture,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depthTexture,0);

    const GLenum transparentDrawBuffers[2] = {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2,transparentDrawBuffers);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        std::cout<<"ERROR::FRAMEBUFFER:: Transparent framebuffer is not complete!" << std::endl;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER,0);

    glm::vec4 zeroFillerVec(0.0f);
	glm::vec4 oneFillerVec(1.0f);

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
        //摄像机矩阵处理
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction); 
        view = glm::lookAt(camPos,camFront+camPos,camUp);

        //逻辑处理代码写在这里：
        
        //渲染

        //不透明物体渲染
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glBindFramebuffer(GL_FRAMEBUFFER,opaueFBO);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        solidShader.Use();
        ShaderUmatf4(solidShader,"view",view);
        ShaderUvec4(solidShader,"color",colorA);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);

        //透明物体渲染
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunci(0,GL_ONE,GL_ONE);
        glBlendFunci(1,GL_ZERO,GL_ONE_MINUS_SRC_COLOR);
        glBindFramebuffer(GL_FRAMEBUFFER,transparentFBO);
        glClearBufferfv(GL_COLOR,0,&zeroFillerVec[0]);
        glClearBufferfv(GL_COLOR,1,&oneFillerVec[0]);
        transparentShader.Use();
        glBindVertexArray(quadVAO);
        ShaderUmatf4(transparentShader,"view",view);
        ShaderUmatf4(transparentShader,"model",modelB);
        ShaderUvec4(transparentShader,"color",colorB);
        glDrawArrays(GL_TRIANGLES,0,6);
        ShaderUmatf4(transparentShader,"model",modelC);
        ShaderUvec4(transparentShader,"color",colorC);
        glDrawArrays(GL_TRIANGLES,0,6);

        //混合
        glDepthFunc(GL_ALWAYS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glBindFramebuffer(GL_FRAMEBUFFER,opaueFBO);
        compositeShader.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,accumTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,revealTexture);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);



        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        screenShader.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,opaqueTexture);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);


        

        
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

glm::mat4 calculate_model_matrix(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale)
{
	glm::mat4 trans = glm::mat4(1.0f);

	trans = glm::translate(trans, position);
	trans = glm::rotate(trans, glm::radians(rotation.x), glm::vec3(1.0, 0.0, 0.0));
	trans = glm::rotate(trans, glm::radians(rotation.y), glm::vec3(0.0, 1.0, 0.0));
	trans = glm::rotate(trans, glm::radians(rotation.z), glm::vec3(0.0, 0.0, 1.0));
	trans = glm::scale(trans, scale);

	return trans;
}