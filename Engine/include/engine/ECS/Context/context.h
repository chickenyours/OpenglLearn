#pragma once

namespace ECS::Core
{
    class Scene;

    struct ECSCoreContext
    {
        Scene* scene = nullptr;
    };

    // 每个工作线程各自持有自己的 ECS 上下文
    extern thread_local ECSCoreContext globalECSCoreContext;

} // namespace ECS::Core