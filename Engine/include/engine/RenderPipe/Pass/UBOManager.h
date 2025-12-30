#pragma once

#include <glad/glad.h>
#include <memory>
#include <vector>

#include "engine/RenderPipe/UniformBindings.h"

namespace Render {

class UBOManager {
public:
    static void Init();
    static GLuint GetUBO(int index);
    static void Release();
    ~UBOManager();

private:
    static std::unique_ptr<UBOManager> instance;
    std::vector<GLuint> UBOList;
};

}