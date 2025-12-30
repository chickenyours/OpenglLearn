#pragma once

#include "code/DebugTool/ConsoleHelp/color_log.h"

class Application{
    public:
        bool Init(Log::StackLogErrorHandle errHandle = nullptr);
        void Run();
        ~Application();
    private:
        bool isInited_ = false;
};