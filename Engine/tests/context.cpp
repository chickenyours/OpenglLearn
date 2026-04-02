#include "engine/ECS/Context/context.h"

namespace ECS::Core
{
    thread_local LocalECSCoreContext localECSCoreContext{};
    GlobalECSCoreContext globalECSCoreContext{};
}