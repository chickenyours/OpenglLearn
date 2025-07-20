#pragma once

#include "code/DebugTool/ConsoleHelp/color_log.h"
#include <GL/gl.h>
#include <string>

inline bool CheckGLErrorFunc(const char* step) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("OpenGL API ERROR", std::string(step) + " : " + std::to_string(err));
        return true;
    }
    return false;
}

#define CHECK_GL_ERROR(step) CheckGLErrorFunc(step)

// std::cerr << "OpenGL Error after " << step << ": " << std::hex << err << std::endl; 