#include "Render/Public/RHICommand/FrameCommand/rhi_command_dispatcher.h"
#include "DebugTool/ConsoleHelp/color_log.h"



// ============================================================
// 5. 用户命令结构体
// ============================================================

struct DrawCmd {
    int x;
    int y;
};

struct ClearCmd {
    float r;
    float g;
    float b;
};

struct SetViewportCmd {
    int width;
    int height;
};

// ============================================================
// 6. 注册命令表：顺序就是自动编号
// ============================================================

using Commands = CommandTable<
    DrawCmd,        // CommandId{0}
    ClearCmd,       // CommandId{1}
    SetViewportCmd  // CommandId{2}
>;

// ============================================================
// 7. 执行函数：dispatch 会自动调用对应 overload
// ============================================================

void execute(const DrawCmd& cmd) {
    std::cout << "DrawCmd: " << cmd.x << ", " << cmd.y << "\n";
}

void execute(const ClearCmd& cmd) {
    std::cout << "ClearCmd: " << cmd.r << ", " << cmd.g << ", " << cmd.b << "\n";
}

void execute(const SetViewportCmd& cmd) {
    std::cout << "SetViewportCmd: " << cmd.width << "x" << cmd.height << "\n";
}

// ============================================================
// 8. 提交 + 执行
// ============================================================

int main() {
    static_assert(Commands::id_of<DrawCmd>() == CommandId{0});
    static_assert(Commands::id_of<ClearCmd>() == CommandId{1});
    static_assert(Commands::id_of<SetViewportCmd>() == CommandId{2});

    CommandBuffer cmd_buffer;

    cmd_buffer.push<Commands>(DrawCmd{10, 20});
    cmd_buffer.push<Commands>(ClearCmd{0.1f, 0.2f, 0.3f});
    cmd_buffer.push<Commands>(SetViewportCmd{1920, 1080});

    execute_buffer<Commands>(cmd_buffer);
}