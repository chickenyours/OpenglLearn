#pragma once

#include <glad/glad.h>
#include "code/Resource/RenderPipeDAT/node.h"
#include "code/ToolAndAlgorithm/DAT/target.h"
#include "code/ToolAndAlgorithm/DAT/target_viewer.h"
#include "code/ToolAndAlgorithm/Opengl/debug.h"
#include "code/ToolAndAlgorithm/Opengl/api.h"

namespace Render{
    template <typename In,typename Out, typename Depend>
    class Pass : public Node<typename In,typename Out, typename Depend>{

    };
}
