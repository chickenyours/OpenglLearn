#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

// 这是把你图里的 GLUT 绘制函数原样迁移过来的版本
void myDisplay()
{
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glColor3f(1.0f, 1.0f, 1.0f);
    glRectf(-0.5f, -0.5f, 0.5f, 0.5f);

    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f);  glVertex2f( 0.0f,  1.0f);
        glColor3f(0.0f, 1.0f, 0.0f);  glVertex2f( 0.8f, -0.5f);
        glColor3f(0.0f, 0.0f, 1.0f);  glVertex2f(-0.8f, -0.5f);
    glEnd();

    glPointSize(3.0f);
    glBegin(GL_POINTS);
        glColor3f(1.0f, 0.0f, 0.0f);  glVertex2f(-0.4f, -0.4f);
        glColor3f(0.0f, 1.0f, 0.0f);  glVertex2f( 0.0f,  0.0f);
        glColor3f(0.0f, 0.0f, 1.0f);  glVertex2f( 0.4f,  0.4f);
    glEnd();
}

int main()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    // 关键：为了兼容 glBegin/glEnd 等旧接口，使用兼容模式
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#ifdef __APPLE__
    // macOS 对兼容旧版支持很差，这份代码通常不适合 macOS
#else
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
#endif

    GLFWwindow* window = glfwCreateWindow(800, 600, "GLFW + GLAD OpenGL", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glViewport(0, 0, 800, 600);

    while (!glfwWindowShouldClose(window))
    {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        myDisplay();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}