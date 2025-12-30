#pragma once

#include <string>
#include "code/DebugTool/ConsoleHelp/color_log.h"

#define FATAL_IF(condition, message) \
    do { \
        if (!(condition)) { \
            Log::Fatal(std::string(__FILE__), std::string(__LINE__), std::string(message)); \
            std::abort(); \
        } \
    } while(0) 
    
