#pragma once
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Log {

    enum class Level {
        Info,
        Warning,
        Error
    };

    inline void EnableAnsiColor() {
#ifdef _WIN32
        static bool enabled = false;
        if (!enabled) {
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
                enabled = true;
            }
        }
#endif
    }

    inline std::string GetColorCode(Level level) {
        switch (level) {
            case Level::Info:    return "\033[94m"; // 亮蓝色
            case Level::Warning: return "\033[93m"; // 亮黄色
            case Level::Error:   return "\033[91m"; // 亮红色
            default:             return "";
        }
    }

    inline void Print(Level level, const std::string& module, const std::string& message) {
        EnableAnsiColor();
        std::string prefix;
        switch (level) {
            case Level::Info:    prefix = "[INFO]"; break;
            case Level::Warning: prefix = "[WARNING]"; break;
            case Level::Error:   prefix = "[ERROR]"; break;
        }
        std::cerr << GetColorCode(level) << prefix
                  << " [" << module << "] "
                  << message << "\033[0m" << std::endl;
    }

    inline void Info(const std::string& module, const std::string& message) {
        Print(Level::Info, module, message);
    }

    inline void Warning(const std::string& module, const std::string& message) {
        Print(Level::Warning, module, message);
    }

    inline void Error(const std::string& module, const std::string& message) {
        Print(Level::Error, module, message);
    }

} // namespace Log
