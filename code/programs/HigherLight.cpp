#include <iostream>
#include <vector>
#include <iomanip>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "code/shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "code/Model/Model.h"
#include "include/stb_image.h"



//窗口信息
const unsigned int SCR_WIDTH = 1200;
const unsigned int SCR_HEIGHT = 800;
//基本事件
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow *window,double xpos,double ypos);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
unsigned int loadTexture(char const * path);
GLuint createConstantTexture();
void checkFramebufferStatus(GLenum target = GL_FRAMEBUFFER);

//camera
glm::vec3 camPos(0.0,0.0,3.0);
glm::vec3 camFront(0.0f,0.0f,-1.0f);
glm::vec3 camUp(0.0f,1.0f,0.0f);
float pitch = -0.0f;
float yaw = -90.0f;
float roll = -0.0f;
float lastX = SCR_WIDTH/2;
float lastY = SCR_HEIGHT/2;
float sensitivity = 0.1f;
float camfixSpeed = 2.5;
float camSpeed = 0;

//time
float deltaTime = 0.0f;
float latestTime = 0.0f;

unsigned int quadVAO = 0;
unsigned int quadVBO;

unsigned int cubeVAO,cubeVBO;

float heightScale = 0.00;

struct vertex{
    glm::vec3 pos;
    glm::vec2 texCoords;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

//全局变量代码写在这里:

int main()
{
    //初始化
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    //扩大缓冲区
    glfwWindowHint(GLFW_SAMPLES, 4);
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
    glEnable(GL_MULTISAMPLE);       //开启超采样
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);                                    //初始化视口
    //glEnable(GL_CULL_FACE);     //表面顺序剔除测试
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

    float quadVertices[] = {
            // positions        // texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
    // setup plane VAO
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

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
std::vector<glm::vec3> cubePositions = {
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

    unsigned int rockTexture = loadTexture("images/R.jpg");

    float cubeVertices[] = {
        // back face
        -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
            1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
            1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f, // bottom-right         
            1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
        -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
        -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f, // top-left
        // front face
        -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
            1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f, // bottom-right
            1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
            1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
        -1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f, // top-left
        -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
        // left face
        -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
        -1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-left
        -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
        -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
        -1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-right
        -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
        // right face
            1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
            1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
            1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-right         
            1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
            1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
            1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-left     
        // bottom face
        -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
            1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f, // top-left
            1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
            1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
        -1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f, // bottom-right
        -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
        // top face
        -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
            1.0f,  1.0f , 1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
            1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f, // top-right     
            1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
        -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
        -1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f  // bottom-left        
    };

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    // fill buffer
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    // link vertex attributes
    glBindVertexArray(cubeVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    unsigned int cubeTex = loadTexture("images/container2.png");

    // 立方体的顶点数据
float cubeTexvertices[] = {
    // 位置                // 纹理坐标
    -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,    1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,    1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,   -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,    1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,   -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f, -1.0f,   -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,   -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,
    1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,

     1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,    1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,    1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,    1.0f,  1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,    1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,    1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,   -1.0f, -1.0f,  1.0f,
    1.0f, -1.0f, -1.0f,    1.0f, -1.0f, -1.0f,
};
    unsigned int cubeTexVAO,cubeTexVBO;
    glGenVertexArrays(1, &cubeTexVAO);
    glGenBuffers(1, &cubeTexVBO);
    // fill buffer
    glBindBuffer(GL_ARRAY_BUFFER, cubeTexVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeTexvertices), cubeTexvertices, GL_STATIC_DRAW);
    // link vertex attributes
    glBindVertexArray(cubeTexVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);


// 法线贴图模型
vector<vertex> normalvertices;
// 顶点数据数组：每个顶点由 3 坐标 (X, Y, Z), 2 纹理坐标 (U, V), 3 法线 (Nx, Ny, Nz)
float brickvertices[] = {
    // 位置               // 纹理坐标   // 法线
    -0.5f, -0.5f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // 左下
    0.5f, -0.5f, 0.0f,    1.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // 右下
    0.5f,  0.5f, 0.0f,    1.0f, 1.0f,   0.0f, 0.0f, -1.0f,  // 右上
    -0.5f,  0.5f, 0.0f,   0.0f, 1.0f,   0.0f, 0.0f, -1.0f   // 左上
};

// 索引数据：绘制平面（两个三角形）
unsigned int brickindices[] = {
    0, 1, 2,  // 第一个三角形
    0, 2, 3   // 第二个三角形
};

// 计算每个三角面的切线和副切线
for (int i = 0; i < sizeof(brickindices) / sizeof(unsigned int); i += 3) {
    // 获取当前三角形的三个顶点索引
    unsigned int i0 = brickindices[i];
    unsigned int i1 = brickindices[i + 1];
    unsigned int i2 = brickindices[i + 2];

    // 获取顶点数据：位置，纹理坐标
    glm::vec3 v0(brickvertices[i0 * 8], brickvertices[i0 * 8 + 1], brickvertices[i0 * 8 + 2]);
    glm::vec3 v1(brickvertices[i1 * 8], brickvertices[i1 * 8 + 1], brickvertices[i1 * 8 + 2]);
    glm::vec3 v2(brickvertices[i2 * 8], brickvertices[i2 * 8 + 1], brickvertices[i2 * 8 + 2]);

    glm::vec2 uv0(brickvertices[i0 * 8 + 3], brickvertices[i0 * 8 + 4]);
    glm::vec2 uv1(brickvertices[i1 * 8 + 3], brickvertices[i1 * 8 + 4]);
    glm::vec2 uv2(brickvertices[i2 * 8 + 3], brickvertices[i2 * 8 + 4]);

    glm::vec3 n0(brickvertices[i0 * 8 + 5], brickvertices[i0 * 8 + 6], brickvertices[i0 * 8 + 7]);
    glm::vec3 n1(brickvertices[i1 * 8 + 5], brickvertices[i1 * 8 + 6], brickvertices[i1 * 8 + 7]);
    glm::vec3 n2(brickvertices[i2 * 8 + 5], brickvertices[i2 * 8 + 6], brickvertices[i2 * 8 + 7]);

    // 计算边
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;

    // 纹理坐标差异
    glm::vec2 deltaUV1 = uv1 - uv0;
    glm::vec2 deltaUV2 = uv2 - uv0;

    // 计算切线和副切线
    float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
    glm::vec3 tangent, bitangent;

    tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
    tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
    tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
    tangent = glm::normalize(tangent);
    
    bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
    bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
    bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
    bitangent = glm::normalize(bitangent);
    
    normalvertices.push_back({v0,uv0,n0,tangent,bitangent});
    normalvertices.push_back({v1,uv1,n1,tangent,bitangent});
    normalvertices.push_back({v2,uv2,n2,tangent,bitangent});
}
    unsigned int normalVBO, normalVAO,normalEBO;
    glGenVertexArrays(1, &normalVAO);
    glGenBuffers(1, &normalVBO);
    glBindVertexArray(normalVAO);
    glBindBuffer(GL_ARRAY_BUFFER, normalVBO);
    glBufferData(GL_ARRAY_BUFFER, normalvertices.size() * sizeof(vertex), &normalvertices[0], GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex,texCoords));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, tangent));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, bitangent));
    glEnableVertexAttribArray(4);   
    glGenBuffers(1,&normalEBO); //?
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,normalEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(brickindices),brickindices,GL_STATIC_DRAW);
    glBindVertexArray(0);

    unsigned int brickdiffusion = loadTexture("images/brickwall.jpg");
    unsigned int bricknormal = loadTexture("images/brickwall_normal.jpg");
    unsigned int brickspecular = createConstantTexture();


    //depthFBO
    const int bs = 4;
    const int SHADOW_WIDTH = 1024 * bs,SHADOW_HEIGHT = 1024 * bs;
    unsigned int depthFBO;
    glGenFramebuffers(1,&depthFBO);
    unsigned int depthMap;       //深度纹理贴图
    glGenTextures(1,&depthMap);
    glBindTexture(GL_TEXTURE_2D,depthMap);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,SHADOW_WIDTH,SHADOW_HEIGHT,0,GL_DEPTH_COMPONENT,GL_FLOAT,NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // attach depth texture as FBO's depth buffer
    glBindFramebuffer(GL_FRAMEBUFFER,depthFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depthMap,0);
    checkFramebufferStatus();
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);

    //万向纹理贴图
    const unsigned int CUBE_SHADOW_WIDTH = 2048, CUBE_SHADOW_HEIGHT = 2048;
    unsigned int cubeDepthMap;
    unsigned int CubeFBO;
    glGenFramebuffers(1,&CubeFBO);
    glGenTextures(1,&cubeDepthMap);
    glBindFramebuffer(GL_FRAMEBUFFER,CubeFBO);
    glBindTexture(GL_TEXTURE_CUBE_MAP,cubeDepthMap);
    for(int i=0;i<6;i++){
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i,0,GL_DEPTH_COMPONENT,CUBE_SHADOW_WIDTH,CUBE_SHADOW_HEIGHT,0,GL_DEPTH_COMPONENT,GL_FLOAT,NULL);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glFramebufferTexture(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,cubeDepthMap,0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    checkFramebufferStatus();
    glBindFramebuffer(GL_FRAMEBUFFER,0);

    glm::vec3 cubeLightPos(1.0,2.0,1.0);
    GLfloat aspect = (GLfloat)SHADOW_WIDTH/(GLfloat)SHADOW_HEIGHT;
    GLfloat near = 0.1f;
    GLfloat far = 25.0f;
    glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), aspect, near, far);
    std::vector<glm::mat4> shadowTransforms;
    // shadowTransforms.push_back(shadowProj * 
    //                 glm::lookAt(cubeLightPos, cubeLightPos + glm::vec3(1.0,0.0,0.0), glm::vec3(0.0,-1.0,0.0)));
    // shadowTransforms.push_back(shadowProj * 
    //                 glm::lookAt(cubeLightPos, cubeLightPos + glm::vec3(-1.0,0.0,0.0), glm::vec3(0.0,-1.0,0.0)));
    // shadowTransforms.push_back(shadowProj * 
    //                 glm::lookAt(cubeLightPos, cubeLightPos + glm::vec3(0.0,1.0,0.0), glm::vec3(0.0,0.0,1.0)));
    // shadowTransforms.push_back(shadowProj * 
    //                 glm::lookAt(cubeLightPos, cubeLightPos + glm::vec3(0.0,-1.0,0.0), glm::vec3(0.0,0.0,-1.0)));
    // shadowTransforms.push_back(shadowProj * 
    //                 glm::lookAt(cubeLightPos, cubeLightPos + glm::vec3(0.0,0.0,1.0), glm::vec3(0.0,-1.0,0.0)));
    // shadowTransforms.push_back(shadowProj * 
    //                 glm::lookAt(cubeLightPos, cubeLightPos + glm::vec3(0.0,0.0,-1.0), glm::vec3(0.0,-1.0,0.0)));
    
    shadowTransforms.push_back(shadowProj * 
                     glm::lookAt(glm::vec3(0.0f),glm::vec3(1.0,0.0,0.0), glm::vec3(0.0,-1.0,0.0)));
    shadowTransforms.push_back(shadowProj * 
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0,0.0,0.0), glm::vec3(0.0,-1.0,0.0)));
    shadowTransforms.push_back(shadowProj * 
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0,1.0,0.0), glm::vec3(0.0,0.0,1.0)));
    shadowTransforms.push_back(shadowProj * 
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0,-1.0,0.0), glm::vec3(0.0,0.0,-1.0)));
    shadowTransforms.push_back(shadowProj * 
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0,0.0,1.0), glm::vec3(0.0,-1.0,0.0)));
    shadowTransforms.push_back(shadowProj * 
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0,0.0,-1.0), glm::vec3(0.0,-1.0,0.0)));
    

    ShaderProgram light("HightLight/bling_Phone.vs","HightLight/bling_Phone.fs");
    light.Use();
    ShaderUmatf4(light,"projection",projection);
    ShaderUvec3(light,"material.color",glm::vec3(1.0,1.0,1.0));
    ShaderU1i(light,"material.diffuse",0);
    ShaderU1i(light,"material.specular",1);
    ShaderU1f(light,"material.shininess",1.0);
    ShaderUvec3(light,"plight.color",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(light,"plight.pos",cubeLightPos);
    ShaderU1f(light,"plight.constant",1.0);
    ShaderU1f(light,"plight.linear",0.07);
    ShaderU1f(light,"plight.quadratic",0.017);
    ShaderUvec3(light,"plight.ambient",glm::vec3(0.1,0.1,0.1));
    ShaderUvec3(light,"plight.diffuse",glm::vec3(0.8,0.8,0.8));
    ShaderUvec3(light,"plight.specular",glm::vec3(0.1,0.1,0.1));
    ShaderU1i(light,"use",1);

    ShaderProgram lightCube("HightLight/lightPos.vs","HightLight/lightPos.fs");
    lightCube.Use();
    ShaderUmatf4(lightCube,"projection",projection);
    ShaderUmatf4(lightCube,"model",glm::translate(glm::scale(model,glm::vec3(0.05)),cubeLightPos));

    ShaderProgram depth("HightLight/depth.vs","HightLight/depth.fs");
    float near_plane = -20.0f,far_plane = 20.0f;
    glm::vec3 lightShadowMapCenterPos(-4.29186,3.64642,-10.7668);
    glm::vec3 lightDirect(0.409756,-0.900319,0.146714);
    glm::mat4 lightView,lightProjection,lightSpaceMatrix;
    lightProjection = glm::ortho(-20.0f,20.0f,-20.0f,20.0f,near_plane,far_plane);
    lightView = glm::lookAt(lightShadowMapCenterPos,lightShadowMapCenterPos + glm::normalize(lightDirect),glm::vec3(0.0f,1.0f,0.0f));
    lightSpaceMatrix = lightProjection * lightView;
    depth.Use();
    ShaderUmatf4(depth,"lightSpaceMatrix",lightSpaceMatrix);

    ShaderProgram screen("HightLight/screen.vs","HightLight/screen.fs");
    screen.Use();
    ShaderU1f(screen,"near_plane",near_plane);
    ShaderU1f(screen,"far_plane",far_plane);
    ShaderU1i(screen,"depthMap",0);

    ShaderProgram simpleDepthShader("HightLight/lightWithShadow.vs","HightLight/lightWithShadow.fs");
    simpleDepthShader.Use();
    ShaderUmatf4(simpleDepthShader,"projection",projection);
    ShaderUmatf4(simpleDepthShader,"lightSpaceMatrix",lightSpaceMatrix);
    ShaderUvec3(simpleDepthShader,"material.color",glm::vec3(1.0,1.0,1.0));
    ShaderU1i(simpleDepthShader,"material.diffuse",0);
    ShaderU1i(simpleDepthShader,"material.specular",1);
    ShaderU1f(simpleDepthShader,"material.shininess",256.0);
    ShaderUvec3(simpleDepthShader,"plight.color",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(simpleDepthShader,"plight.pos",cubeLightPos);
    ShaderU1f(simpleDepthShader,"plight.constant",1.0); 
    ShaderU1f(simpleDepthShader,"plight.linear",0.01);
    ShaderU1f(simpleDepthShader,"plight.quadratic",0.005);
    ShaderUvec3(simpleDepthShader,"plight.ambient",glm::vec3(0.1,0.1,0.1));
    ShaderUvec3(simpleDepthShader,"plight.diffuse",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(simpleDepthShader,"plight.specular",glm::vec3(0.5,0.5,0.5));
    ShaderUvec3(simpleDepthShader,"dlight.color",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(simpleDepthShader,"dlight.direct",lightDirect);
    ShaderUvec3(simpleDepthShader,"dlight.ambient",glm::vec3(0.1,0.1,0.1));
    ShaderUvec3(simpleDepthShader,"dlight.diffuse",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(simpleDepthShader,"dlight.specular",glm::vec3(0.8,0.8,0.8));
    ShaderU1i(simpleDepthShader,"shadowMap",2);
    ShaderU1i(simpleDepthShader,"plight.depthMap",3);
    ShaderU1f(simpleDepthShader,"plight.shadowFarPlane",far);

    ShaderProgram normalShader("HightLight/normalcal.vs","HightLight/normal.fs");
    normalShader.Use();
    ShaderUmatf4(normalShader,"projection",projection);
    ShaderUmatf4(normalShader,"lightSpaceMatrix",lightSpaceMatrix);
    ShaderUvec3(normalShader,"material.color",glm::vec3(1.0,1.0,1.0));
    ShaderU1i(normalShader,"material.diffuse",0);
    ShaderU1i(normalShader,"material.specular",1);
    ShaderU1i(normalShader,"material.normal",2);
    ShaderU1f(normalShader,"material.shininess",64.0);
    ShaderUvec3(normalShader,"dlight.color",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(normalShader,"dlight.direct",lightDirect);
    ShaderUvec3(normalShader,"dlight.ambient",glm::vec3(0.1,0.1,0.1));
    ShaderUvec3(normalShader,"dlight.diffuse",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(normalShader,"dlight.specular",glm::vec3(0.8,0.8,0.8));
    ShaderU1i(normalShader,"dlight,shadowMap",3);
    ShaderUvec3(normalShader,"plight.color",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(normalShader,"plight.pos",cubeLightPos);
    ShaderU1f(normalShader,"plight.constant",1.0); 
    ShaderU1f(normalShader,"plight.linear",0.01);
    ShaderU1f(normalShader,"plight.quadratic",0.005);
    ShaderUvec3(normalShader,"plight.ambient",glm::vec3(0.1,0.1,0.1));
    ShaderUvec3(normalShader,"plight.diffuse",glm::vec3(1.0,1.0,1.0));
    ShaderUvec3(normalShader,"plight.specular",glm::vec3(0.5,0.5,0.5));
    ShaderU1i(normalShader,"plight.depthMap",4);
    ShaderU1f(normalShader,"plight.shadowFarPlane",far);

    ShaderProgram parallaxShader("HightLight/Parallax.vs","HightLight/Parallax.fs");
    parallaxShader.Use();
    ShaderUmatf4(parallaxShader,"projection",projection);
    ShaderUvec3(parallaxShader,"lightPos",cubeLightPos);
    ShaderU1i(parallaxShader,"diffuseMap",0);
    ShaderU1i(parallaxShader,"normalMap",1);
    ShaderU1i(parallaxShader,"parallaxMap",2);
    ShaderU1f(parallaxShader,"heightScale",heightScale);

    unsigned int bricksdiffse = loadTexture("images/bricks2.jpg");
    unsigned int bricksNormalMap = loadTexture("images/bricks2_normal.jpg");
    unsigned int bricksParallax = loadTexture("images/bricks2_disp.jpg");

    ShaderProgram cubeShadowShader("HightLight/cube.vs","HightLight/cube.fs","HightLight/cube.gs");
    cubeShadowShader.Use();
    for(int i = 0 ;i<6;i++){
        ShaderUmatf4(cubeShadowShader,"shadowMatrices["+std::to_string(i)+"]",shadowTransforms[i]);
    }
    ShaderUvec3(cubeShadowShader,"lightPos",cubeLightPos);
    ShaderU1f(cubeShadowShader,"far_plane",far);
    ShaderUvec3(cubeShadowShader,"CubeLightPos",cubeLightPos);

    ShaderProgram cubeTextureShader("HightLight/CubeTexture.vs","HightLight/CubeTexture.fs");
    cubeTextureShader.Use();
    ShaderU1i(cubeTextureShader,"cubeTexture",0);
    ShaderUmatf4(cubeTextureShader,"projection",projection);

    ShaderProgram texShader("HightLight/hh.vs","HightLight/hh.fs");
    texShader.Use();
    ShaderUmatf4(texShader,"projection",projection);
    ShaderU1i(texShader,"diffuse",0);

    MyTool::Model m("models/uu/Guam_NL_v1.01.pmx");

    // 查询 OpenGL 支持的 GLSL 版本
    const char* glslVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    std::cout << "GLSL 编译器版本: " << glslVersion << std::endl;

    //循环
    while (!glfwWindowShouldClose(window))
    {
        bool DEPTH = false;
        //get currentTime & deltaTime
        float currentTime = glfwGetTime();
        deltaTime = currentTime - latestTime;
        latestTime = deltaTime + latestTime;
        std::stringstream titleStream;
        titleStream << "FPS: " << std::fixed << std::setprecision(2) << (int)(1.0/deltaTime);
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
        if(glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)){

            light.Use();
            cubeLightPos = camPos + camFront * 0.2f;
            ShaderUvec3(light,"plight.pos",cubeLightPos);

            lightCube.Use();
            ShaderUmatf4(lightCube,"model",glm::scale(glm::translate(model,cubeLightPos),glm::vec3(0.05)));
            lightShadowMapCenterPos = camPos;
            lightDirect = camFront;
            lightView = glm::lookAt(lightShadowMapCenterPos,lightShadowMapCenterPos + glm::normalize(lightDirect),glm::vec3(0.0f,1.0f,0.0f));
            lightSpaceMatrix = lightProjection * lightView;

            depth.Use();
            ShaderUmatf4(depth,"lightSpaceMatrix",lightSpaceMatrix);

            simpleDepthShader.Use();
            ShaderUmatf4(simpleDepthShader,"lightSpaceMatrix",lightSpaceMatrix);
            ShaderUvec3(simpleDepthShader,"dlight.direct",lightDirect);

            cubeShadowShader.Use();
            ShaderUvec3(cubeShadowShader,"CubeLightPos",camPos);

            simpleDepthShader.Use();
            ShaderUvec3(simpleDepthShader,"plight.pos",camPos);

            normalShader.Use();
            ShaderUmatf4(normalShader,"lightSpaceMatrix",lightSpaceMatrix);
            ShaderUvec3(normalShader,"dlight.direct",lightDirect);
            ShaderUvec3(normalShader,"plight.pos",camPos);

            parallaxShader.Use();
            ShaderUvec3(parallaxShader,"lightPos",camPos);

            DEPTH = true;
        }
        else{
            DEPTH = false;
        }
        if(glfwGetKey(window,GLFW_KEY_B)){
            light.Use();
            ShaderU1i(light,"use",1);
        }
        if(glfwGetKey(window,GLFW_KEY_N)){
            light.Use();
            ShaderU1i(light,"use",0);
        }
        if(glfwGetKey(window,GLFW_KEY_O)){

            std::cout<<camPos.x<<" "<<camPos.y<<" "<<camPos.z<<std::endl;
            std::cout<<camFront.x<<" "<<camFront.y<<" "<<camFront.z<<std::endl;
        }
         if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            if (heightScale > 0.0f) 
                heightScale -= 0.02f;
            else{
                heightScale = 0.0f;
            }
            parallaxShader.Use();
            ShaderU1f(parallaxShader,"heightScale",heightScale);
        }
        else if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            if (heightScale < 1.0f) 
                heightScale += 0.02f;
            else{
                heightScale = 1.0f;
            } 
            parallaxShader.Use();
            ShaderU1f(parallaxShader,"heightScale",heightScale);
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
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // light.use();
        // ShaderUmatf4(light,"model",glm::scale(model,glm::vec3(0.05)));
        // ShaderUmatf4(light,"view",view);
        // ShaderUvec3(light,"eye",camPos);
        // m.Draw();

        // glBindVertexArray(VAO);
        // lightCube.use();
        // ShaderUmatf4(lightCube,"view",view);
        // glDrawArrays(GL_TRIANGLES,0,36);

        //绘制场景到深度贴图
        glBindFramebuffer(GL_FRAMEBUFFER,depthFBO);
        glViewport(0,0,SHADOW_WIDTH,SHADOW_HEIGHT);
        glClear(GL_DEPTH_BUFFER_BIT);
        depth.Use();
            //plane
            glBindVertexArray(planeVAO);
            ShaderUmatf4(depth,"model",glm::translate(model,glm::vec3(0.0)));
            glDrawArrays(GL_TRIANGLES,0,6);
            //cube
            glBindVertexArray(cubeVAO);
            for(int i = 0 ;i<cubePositions.size();i++){
                ShaderUmatf4(depth,"model",glm::translate(model,cubePositions[i]));
                glDrawArrays(GL_TRIANGLES,0,36);
            }
            //barrel
            ShaderUmatf4(depth,"model",glm::scale(glm::translate(model,glm::vec3(5)),glm::vec3(0.05)));
            m.Draw();
        //绘制立方体贴图
        glBindFramebuffer(GL_FRAMEBUFFER,CubeFBO);
        glViewport(0,0,CUBE_SHADOW_WIDTH,CUBE_SHADOW_HEIGHT);
        glClear(GL_DEPTH_BUFFER_BIT);
        cubeShadowShader.Use();
            //plane
            glBindVertexArray(planeVAO);
            ShaderUmatf4(cubeShadowShader,"model",glm::translate(model,glm::vec3(0.0)));
            glDrawArrays(GL_TRIANGLES,0,6);
            //cube
            glBindVertexArray(cubeVAO);
            for(int i = 0 ;i<cubePositions.size();i++){
                ShaderUmatf4(cubeShadowShader,"model",glm::translate(model,cubePositions[i]));
                glDrawArrays(GL_TRIANGLES,0,36);
            }
            //barrel
            ShaderUmatf4(cubeShadowShader,"model",glm::scale(glm::translate(model,glm::vec3(5)),glm::vec3(0.05)));
            m.Draw();

        
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        
        if(DEPTH){
            // glBindVertexArray(quadVAO);
            // screen.use();
            // glActiveTexture(GL_TEXTURE0);
            // glBindTexture(GL_TEXTURE_2D,depthMap);
            // glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(cubeTexVAO);
            cubeTextureShader.Use();
            ShaderUmatf4(cubeTextureShader,"view",view);
            ShaderUmatf4(cubeTextureShader,"model",glm::translate(model,camPos));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP,cubeDepthMap);
            glDrawArrays(GL_TRIANGLES,0,36);
        }
        else{
            simpleDepthShader.Use();
            ShaderUmatf4(simpleDepthShader,"view",view);
            ShaderUvec3(simpleDepthShader,"vpos",camPos);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D,depthMap);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_CUBE_MAP,cubeDepthMap);
            //plane
                glBindVertexArray(planeVAO);
                ShaderUmatf4(simpleDepthShader,"model",glm::translate(model,glm::vec3(0.0)));
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D,rockTexture);
                glDrawArrays(GL_TRIANGLES,0,6);
            //cube
                glBindVertexArray(cubeVAO);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D,cubeTex);
                for(int i = 0 ;i<cubePositions.size();i++){
                    ShaderUmatf4(simpleDepthShader,"model",glm::translate(model,cubePositions[i]));
                    glDrawArrays(GL_TRIANGLES,0,36);
                }

            // //barrel
            // ShaderUmatf4(simpleDepthShader,"model",glm::scale(glm::translate(model,glm::vec3(5)),glm::vec3(0.05)));
            // m.Draw();

        // //wall
        // glBindFramebuffer(GL_FRAMEBUFFER,0);
        // glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
        // //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // glBindVertexArray(normalVAO);
        // parallaxShader.use();
        // ShaderUmatf4(parallaxShader,"model",glm::scale(model,glm::vec3(10.0)));
        // ShaderUmatf4(parallaxShader,"view",view);
        // ShaderUvec3(parallaxShader,"viewPos",camPos);
        // glActiveTexture(GL_TEXTURE0);
        // glBindTexture(GL_TEXTURE_2D,bricksdiffse);
        // glActiveTexture(GL_TEXTURE1);
        // glBindTexture(GL_TEXTURE_2D,bricksNormalMap);
        // glActiveTexture(GL_TEXTURE2);
        // glBindTexture(GL_TEXTURE_2D,bricksParallax);
        // //glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        // glDrawArrays(GL_TRIANGLES,0,6);


        //cube
        glDisable(GL_DEPTH_TEST);
        lightCube.Use();
        glBindVertexArray(cubeVAO);
        ShaderUmatf4(lightCube,"model",glm::scale(glm::translate(model,cubeLightPos),glm::vec3(0.05)));
        ShaderUmatf4(lightCube,"view",view);
        glDrawArrays(GL_TRIANGLES,0,36);
        glEnable(GL_DEPTH_TEST);
        
        }
        

        

        
        //渲染主要逻辑写在这里
        
        
        
        glfwSwapBuffers(window);                                //交换缓冲
        glfwPollEvents();                                       //事件响应
    }
    glfwTerminate();
    return 0;
}



// void renderScene(const ShaderProgram &program)
// {
//     // floor
//     glm::mat4 model = glm::mat4(1.0f);
//     shader
//     program.setMat4("model", model);
//     glBindVertexArray(planeVAO);
//     glDrawArrays(GL_TRIANGLES, 0, 6);
//     // // cubes
//     // model = glm::mat4(1.0f);
//     // model = glm::translate(model, glm::vec3(0.0f, 1.5f, 0.0));
//     // model = glm::scale(model, glm::vec3(0.5f));
//     // shader.setMat4("model", model);
//     // renderCube();
//     // model = glm::mat4(1.0f);
//     // model = glm::translate(model, glm::vec3(2.0f, 0.0f, 1.0));
//     // model = glm::scale(model, glm::vec3(0.5f));
//     // shader.setMat4("model", model);
//     // renderCube();
//     // model = glm::mat4(1.0f);
//     // model = glm::translate(model, glm::vec3(-1.0f, 0.0f, 2.0));
//     // model = glm::rotate(model, glm::radians(60.0f), glm::normalize(glm::vec3(1.0, 0.0, 1.0)));
//     // model = glm::scale(model, glm::vec3(0.25));
//     // shader.setMat4("model", model);
//     // renderCube();
// }



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

unsigned int loadTexture(char const * path)
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

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT); // for this tutorial: use GL_CLAMP_TO_EDGE to prevent semi-transparent borders. Due to interpolation it takes texels from next repeat 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT);
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

void checkFramebufferStatus(GLenum target) {
    GLenum status = glCheckFramebufferStatus(target);

    switch (status) {
        case GL_FRAMEBUFFER_COMPLETE:
            std::cout << "Framebuffer is complete and ready for rendering." << std::endl;
            break;
        case GL_FRAMEBUFFER_UNDEFINED:
            std::cerr << "Framebuffer undefined. Target is the default framebuffer, but it is not available." << std::endl;
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            std::cerr << "Framebuffer incomplete: One or more framebuffer attachment points are incomplete." << std::endl;
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            std::cerr << "Framebuffer incomplete: No images are attached to the framebuffer." << std::endl;
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            std::cerr << "Framebuffer incomplete: Draw buffer configuration is not correct." << std::endl;
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            std::cerr << "Framebuffer incomplete: Read buffer configuration is not correct." << std::endl;
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            std::cerr << "Framebuffer unsupported: The combination of internal formats used by attachments is not supported." << std::endl;
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            std::cerr << "Framebuffer incomplete: Multisample settings are inconsistent." << std::endl;
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
            std::cerr << "Framebuffer incomplete: Layered framebuffer targets are not consistent." << std::endl;
            break;
        default:
            std::cerr << "Unknown framebuffer error: " << status << std::endl;
            break;
    }
}

GLuint createConstantTexture() {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // 创建一个 1x1 的纹理，并将其填充为恒定的 1.0（白色）
    GLfloat data[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA 各通道都是 1.0
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_FLOAT, data);

    // 设置纹理参数（例如，不需要过滤）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // 解绑定纹理
    glBindTexture(GL_TEXTURE_2D, 0);

    return textureID;
}



// glBindFramebuffer(GL_FRAMEBUFFER,0);
//         glViewport(0,0,SCR_WIDTH,SCR_HEIGHT);
//         glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

//         glBindVertexArray(normalVAO);
//         normalShader.use();
//         ShaderUmatf4(normalShader,"model",glm::scale(model,glm::vec3(10.0)));
//         ShaderUmatf4(normalShader,"view",view);
//         ShaderUvec3(normalShader,"viewPos",camPos);
//         glActiveTexture(GL_TEXTURE0);
//         glBindTexture(GL_TEXTURE_2D,brickdiffusion);
//         glActiveTexture(GL_TEXTURE1);
//         glBindTexture(GL_TEXTURE_2D,brickspecular);
//         glActiveTexture(GL_TEXTURE2);
//         glBindTexture(GL_TEXTURE_2D,bricknormal);
//         //glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
//         glDrawArrays(GL_TRIANGLES,0,6);

//         normalShader.use();
//         ShaderUmatf4(normalShader,"model",glm::scale(glm::translate(model,glm::vec3(0.0,0.0,4.0)),glm::vec3(0.05)));
//         m.Draw();