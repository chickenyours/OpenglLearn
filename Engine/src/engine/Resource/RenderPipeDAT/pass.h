#pragma once

#include <glad/glad.h>
#include "engine/Resource/RenderPipeDAT/node.h"
#include "engine/ToolAndAlgorithm/DAT/target.h"
#include "engine/ToolAndAlgorithm/DAT/target_viewer.h"
#include "engine/ToolAndAlgorithm/Opengl/debug.h"
#include "engine/ToolAndAlgorithm/Opengl/api.h"

namespace Render{
    template <typename In,typename Out, typename Depend>
    class Pass : public Node<typename In,typename Out, typename Depend>{

    };
}

