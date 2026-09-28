# LuaModule

独立 `IModule` 实现，宿主通过 `Load / Call / Tick / TakeCommands` 使用，不依赖 GLFW、OpenGL 或 IWanna ECS 类型。Lua 5.4.9 从 `third_party` 静态编译，顶层工程启用 C 与 C++。

每个活动房间一个 VM。开放基础、table、string、math、utf8 库；不开放 io/os/package/debug，也不提供文件加载或原生库加载接口。事件和上下文的构造、函数调用均在 protected call 中执行；每次调用限制 100000 条 VM 指令，VM 分配上限 8 MiB。宿主队列限制 256 命令、128 计时器和 4096 once key，字符串长度也有限制。这用于限制关卡脚本错误，不宣称是运行任意恶意代码的完整安全边界。

`on_timer` 使用宿主传入的固定 dt，与渲染帧率无关。命令只是数据，由 IWanna 的 RoomWorld 在物理系统退出后处理。发生 Lua 错误后停用该 VM 的后续回调，重新加载脚本可恢复。关闭 VM 会释放计时器、命令与 once 状态。

房间数据接口、示例 Lua 与制作步骤见 `Asset/IWanna/Showcase/README.md`。
