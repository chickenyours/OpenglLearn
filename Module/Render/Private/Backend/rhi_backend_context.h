#pragma once

#include "Render/Private/rhi_command_system.h"
#include "Render/Private/rhi_resource_pool.h"

namespace Render{
    struct RHIBackContext{
        RHIResourcePool* resourcePool;
    };
}