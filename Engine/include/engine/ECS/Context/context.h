#pragma once

namespace ECS::Core
{
    class Scene;
    class JobSystemSchedule;

    struct LocalECSCoreContext
    {
        Scene* scene = nullptr;
    };

    struct GlobalECSCoreContext
    {
        JobSystemSchedule* jobSystemSchedule = nullptr;
    };

    // 每个工作线程各自持有自己的 ECS 上下文
    extern thread_local LocalECSCoreContext localECSCoreContext;
    extern GlobalECSCoreContext globalECSCoreContext;

} // namespace ECS::Core