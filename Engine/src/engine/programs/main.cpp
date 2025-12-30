#include <glad/glad.h>
#include <glfw/glfw3.h> //glfw3.h中引用glad.h,而重复引用glad.h,glad.h会报错,因此先引用glad.h
#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION    //是使预编译器include相关功能的代码
#include "stb_image.h"

int WIDTH = 800;
int HEIGHT = 600;

glm::vec3 camPostion(0.0f,0.0f,3.0f);
glm::vec3 camFront(0.0f,0.0f,-1.0f);
glm::vec3 camUp(0.0f,1.0f,0.0f);
//欧拉角
float pitch = 0.0f;
float yaw = -90.0f;
float roll = 0.0f;
//光标坐标
float lastX = WIDTH/2;
float lastY = HEIGHT/2;
float sensitivity = 0.1f;

float camSpeed = 0.02;
float deltaTime = 0.0f;
float latestTime = 0.0f;

glm::mat4 calculate_lookAt_matrix(glm::vec3 position, glm::vec3 target, glm::vec3 worldUp)
{
    // 1. Position = known
    // 2. Calculate cameraDirection
    glm::vec3 zaxis = glm::normalize(-target);
    // 3. Get positive right axis vector
    glm::vec3 xaxis = glm::normalize(glm::cross(glm::normalize(worldUp), zaxis));
    // 4. Calculate camera up vector
    glm::vec3 yaxis = glm::cross(zaxis, xaxis);

    // Create translation and rotation matrix
    // In glm we access elements as mat[col][row] due to column-major layout
    glm::mat4 translation = glm::mat4(1.0f); // Identity matrix by default
    translation[3][0] = -position.x; // Fourth column, first row
    translation[3][1] = -position.y;
    translation[3][2] = -position.z;
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


class Shader
{
public:
    Shader(int shaderType, const char *shaderName) : _shaderType(shaderType), _shaderName(shaderName)
    {
        // read shaderSource from file
        char *shaderPath = new char[std::strlen(_shaderBasePath) + std::strlen(shaderName)+1];
        shaderPath[0] ='\0';
        std::strcat(shaderPath,_shaderBasePath);
        std::strcat(shaderPath,shaderName);
        std::cout<<shaderPath<<std::endl; 
        std::ifstream infile(shaderPath,std::ios::binary);
        if (infile.is_open())
        {
            infile.seekg(0, std::ios::end);
            std::streampos file_size = infile.tellg();
            infile.seekg(0, std::ios::beg);
            char *shaderSource = new char[static_cast<int>(file_size)+1];
            infile.read(shaderSource, file_size);
            infile.close();
            shaderSource[static_cast<int>(file_size)] = '\0';
            _shaderID = glCreateShader(shaderType); 
            glShaderSource(_shaderID, 1, &shaderSource, NULL);
            glCompileShader(_shaderID);
            int success;
            char infolog[512];
            glGetShaderiv(_shaderID, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                glGetShaderInfoLog(_shaderID, 512, NULL, infolog);
                std::cout << "ERROR SHADER!:" << _shaderName << ":" << infolog;
            }
            delete[] shaderPath;
            delete[] shaderSource;
        }
        else
        {
            std::cout << "ERROR OPEN FILE!" << std::endl;
        }
    }
    int getShaderID()
    {
        return _shaderID;
    }
    const char *getShaderName()
    {
        return _shaderName;
    }
    unsigned int getShaderType(){
        return  _shaderType;
    }

private:
    static const char* _shaderBasePath;
    unsigned int _shaderType;
    unsigned int _shaderID;
    const char *_shaderName;
};

class ShaderProgram{
    public:
    ShaderProgram(Shader* vertexShader,Shader* fragShader,const char* ProgramName = ""){
        if(vertexShader->getShaderType() == GL_VERTEX_SHADER && fragShader->getShaderType() == GL_FRAGMENT_SHADER){
            _shaderProgramID =glCreateProgram();
            glAttachShader(_shaderProgramID,vertexShader->getShaderID());
            glAttachShader(_shaderProgramID,fragShader->getShaderID());
            glLinkProgram(_shaderProgramID);
            int success;
            char infolog[512];
            glGetProgramiv(_shaderProgramID, GL_LINK_STATUS, &success);
            if (!success)
            {
                glGetProgramInfoLog(_shaderProgramID, 512, NULL, infolog);
                std::cout << "shaderProgram link error:" << ProgramName << ":" << infolog;
            }
            _vertexshader = vertexShader;
            _fragShader = fragShader;
            shaderProgramOrder++;
            if(ProgramName==""){
                _shaderProgramName = new char[strlen(ProgramName)+1];
                strcpy(_shaderProgramName,ProgramName);
            }
            else{
                const char* pn = "ShaderProgram";
                int t = shaderProgramOrder;
                int c = 0;
                while(t/10>0)c++;
                char* bn = new char[c+2];
                bn[c+1] = '\0';
                for(int i=c;i>=0;i--){
                    bn[i] = '0'+t%10;
                    t/=10;
                }
                _shaderProgramName = new char[strlen(pn)+c+1];
                _shaderProgramName[0] = '\0';
                strcat(_shaderProgramName,pn);
                strcat(_shaderProgramName,bn);
            }
        
        }
        else{
            std::cout<<"ShaderProgram can't be created:"<<_shaderProgramID;
        }
    }
    ~ShaderProgram(){
        delete[] _shaderProgramName;
    }
    void Use(){
        glUseProgram(_shaderProgramID);
    }
    int getShaderProgramID(){
        return _shaderProgramID;
    }
    
    private:
        static int shaderProgramOrder;
        unsigned int _shaderProgramOrder;
        Shader* _vertexshader;
        Shader* _fragShader;
        unsigned int _shaderProgramID;
        char* _shaderProgramName;
};

void getMaxVertexAttrbs()
{
    int max;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max);
    std::cout << "MAX VERTEX ATTRIBUTS SUPPROT:" << max;
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
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

void disposal()
{
    glfwTerminate();
}

void processInput(GLFWwindow *window)
{
    camSpeed = deltaTime*2.5f;
    if(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)){
        camSpeed *= 2.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS and glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }
    if(glfwGetKey(window,GLFW_KEY_W)){
        camPostion += camFront*camSpeed;
    }
    if(glfwGetKey(window,GLFW_KEY_A)){
        camPostion -= glm::normalize(glm::cross(camFront,camUp))*camSpeed;
    }
    if(glfwGetKey(window,GLFW_KEY_S)){
        camPostion -= camFront*camSpeed;
    }
    if(glfwGetKey(window,GLFW_KEY_D)){
        camPostion += glm::normalize(glm::cross(camFront,camUp))*camSpeed;
    }
    if(glfwGetKey(window,GLFW_KEY_SPACE)){
        camPostion.y += camSpeed;
    }
}


const char* Shader::_shaderBasePath = "./shader/";
int ShaderProgram::shaderProgramOrder = 0;


int main()
{
    glfwInit();
    /*windowhint用于配置window,framebuffer,context,platform
    在glfwinit时设定为默认值
    在glfwCreateWindow之前有些hint作为额外args
    */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);                 // OPENGL主要版本
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);                 // OPENGL次要版本
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 使用OPENGL的核心模式
    GLFWwindow *window = glfwCreateWindow(800, 600, "first opengla", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Fail to creat glfw window";
        disposal();
        return 0;
    }
    glfwMakeContextCurrent(window);
    // glad 初始化
    // glfw建立窗口并返回opengl的指针，转化为gladloadproc来用glad管理这个指针
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    { // 判断指针是否转换
        std::cout << "Fail to initialize glad ";
        disposal();
        return 0;
    }

    glViewport(0, 0, WIDTH, HEIGHT); // 初始化视口

    //隐藏光标
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); 


    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback); // 注册缓冲区size变化事件
    glfwSetCursorPosCallback(window,mouse_callback);
    //构建着色器程序
    
    glEnable(GL_DEPTH_TEST);                                            //开启深度测试

    Shader v(GL_VERTEX_SHADER,"tex.vs");
    Shader f(GL_FRAGMENT_SHADER,"tex.fs");
    ShaderProgram program(&v,&f,"shaderProgram");
    
    //原始数据初始化
    // float vertices[] =  {0.5f, 0.5f, 0.0f,1.0f,0.0f,0.0f,1.0f,1.0f, // 四边形
    //                     0.5f, -0.5f, 0.0f,0.0f,1.0f,0.0f,1.0f,0.0f,
    //                     -0.5f, -0.5f, 0.0f,0.0f,0.0f,1.0f,0.0f,0.0f,
    //                     -0.5f, 0.5f, 0.0f,1.0f,0.0f,0.0f,0.0f,1.0f};
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
    unsigned int indices3[] = {0,1,2,0,2,3};
    
    unsigned int VAO3,VBO3,EBO3;
    glGenVertexArrays(1,&VAO3);
    glBindVertexArray(VAO3);
    glGenBuffers(1,&VBO3);
    glBindBuffer(GL_ARRAY_BUFFER,VBO3);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glGenBuffers(1,&EBO3);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO3);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices3),indices3,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    unsigned int texture1,texture2; 
    glGenTextures(1,&texture1);
    glGenTextures(1,&texture2);

    glBindTexture(GL_TEXTURE_2D,texture1);
    //构建纹理
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    int width,height,nrChannels;
    unsigned char* data = stbi_load("./images/container.jpg",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
    }
    glBindTexture(GL_TEXTURE_2D,texture2);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    data = stbi_load("./images/awesomeface.png",&width,&height,&nrChannels,0);
    if(data){
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
    else{
        std::cout<<"image cannot be opened";
    }
    program.Use();
    glUniform1i(glGetUniformLocation(program.getShaderProgramID(),"Texture1"),0);
    glUniform1i(glGetUniformLocation(program.getShaderProgramID(),"Texture2"),1);

    // glm::mat4 trans(1.0f);
    // glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"transform"),1,GL_FALSE,glm::value_ptr(trans));
    // float mixAmount = 0.5;
    // glUniform1f(glGetUniformLocation(program.getShaderProgramID(),"mixAmount"),mixAmount);

    // glm::mat4 model(1.0f);
    // model = glm::rotate(model,glm::radians(-90.0f),glm::vec3(1.0f,0.0f,0.0f));
    float ration = 1.333f;
    float FoV = 45.0f;
    float rotate = 0.0f;
    glm::mat4 view(1.0f);
    view = glm::translate(view,glm::vec3(0.0f,0.0f,-3.0f));
    glm::mat4 projection = glm::perspective(glm::radians(FoV),ration,0.1f,100.0f);
    projection = glm::perspective(glm::radians(FoV),ration,0.1f,100.0f);
    // glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
    
    

    while (!glfwWindowShouldClose(window))
    {
        //时间控制
        float currentTime = glfwGetTime();
        deltaTime = currentTime - latestTime;
        latestTime = currentTime; 

        processInput(window);
        if (glfwGetKey(window,GLFW_KEY_UP)){
            FoV +=  1;
            std::cout<<"FoV:"<<FoV<<std::endl;
            projection = glm::perspective(glm::radians(FoV),ration,0.1f,100.0f);
            glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
        }
        else if(glfwGetKey(window,GLFW_KEY_DOWN)){
            FoV -=  1;
            std::cout<<"FoV:"<<FoV<<std::endl;
            projection = glm::perspective(glm::radians(FoV),ration,0.1f,100.0f);
            glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
        }
        if (glfwGetKey(window,GLFW_KEY_RIGHT)){
            ration += 0.02;
            std::cout<<"ration:"<<ration<<std::endl;
            projection = glm::perspective(glm::radians(FoV),ration,0.1f,100.0f);
            glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
        }
        else if(glfwGetKey(window,GLFW_KEY_LEFT)){
            ration -= 0.02;
            std::cout<<"ration:"<<ration<<std::endl;
            projection = glm::perspective(glm::radians(FoV),ration,0.1f,100.0f);
            glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"projection"),1,GL_FALSE,glm::value_ptr(projection));
        }
        // if(glfwGetKey(window,GLFW_KEY_A)){
        //     rotate += 0.05;
        //     std::cout<<"rotation:"<<rotate<<std::endl;
        //     glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(glm::rotate(view,glm::radians(rotate),glm::vec3(0.0f,1.0f,0.0f))));
        // }
        // else if(glfwGetKey(window,GLFW_KEY_D)){
        //     rotate -= 0.05;
        //     std::cout<<"rotation:"<<rotate<<std::endl;
        //     glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(glm::rotate(view,glm::radians(rotate),glm::vec3(0.0f,1.0f,0.0f))));
        // }
        float radius = 10.0f;
        glm::vec3 direction;
        direction.x = glm::cos(glm::radians(pitch)) * glm::cos(glm::radians(yaw));
        direction.y = glm::sin(glm::radians(pitch));
        direction.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
        camFront = glm::normalize(direction);
        
       // view = glm::lookAt(camPostion,camFront+camPostion,camUp);
        glm::vec3 Right = glm::normalize(glm::cross(camUp,camFront));
        glm::vec3 Up = glm::cross(camFront,Right);
        view = 
         view = glm::mat4(
            glm::vec4(Right, 0.0f),
            glm::vec4(Up, 0.0f),
            glm::vec4(-camFront, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
        *  glm::translate(glm::mat4(1.0f), -camPostion);
        //view  = calculate_lookAt_matrix(camPostion,camFront,camUp);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(VAO3);
        program.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,texture1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,texture2);
        glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"view"),1,GL_FALSE,glm::value_ptr(view));
        for(int i = 0;i<10;i++){
            glm::mat4 model(1.0f);
            model = glm::translate(model,cubePositions[i]);
            model = glm::rotate(model,glm::radians(i*20.0f)*(float)glfwGetTime(),glm::vec3(0.3f,0.5f,1.0f));
            glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"model"),1,GL_FALSE,glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES,0,36);
        }
        
        
        
        // glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        // float time = glfwGetTime();
        // glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"transform"),1,GL_FALSE,glm::value_ptr(
        //     glm::translate(glm::rotate(trans,glm::radians((float)(time*10)),glm::vec3(0.0f,0.0f,1.0f)),glm::vec3(0.5f,0.5f,0.0f))));
        // glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        // glUniformMatrix4fv(glGetUniformLocation(program.getShaderProgramID(),"transform"),1,GL_FALSE,glm::value_ptr(
        //     glm::scale(glm::translate(trans,glm::vec3(-0.5f,0.5f,0.5f)),glm::vec3(glm::sin(time),glm::sin(time),0.0f))
        // ));
        // glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
    
        // 窗口更新
        glfwSwapBuffers(window); // 交换缓冲区
        glfwPollEvents();        // 相应事件
    }
    // 程序结束
    disposal();
    return 0;
}
