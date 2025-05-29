#pragma once
#include <iostream>
#include <string>
#include <stdlib.h>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#endif

#define LOG_INFO_COLOR    "\033[94m" // 亮蓝色
#define LOG_WARNING_COLOR "\033[93m" // 亮黄色
#define LOG_ERROR_COLOR   "\033[91m" // 亮红色
#define LOG_FATAL_COLOR   "\033[95m" // 亮紫色

namespace Log {

    enum class Level {
        Info,
        Warning,
        Error,
        Fatal  // <-- 新增Fatal等级
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
            case Level::Fatal:   return "\033[95m"; // 亮紫色（Fatal用紫色，看起来更明显）
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
            case Level::Fatal:   prefix = "[FATAL]"; break;   // <-- 加Fatal
        }
        std::cerr << GetColorCode(level) << prefix
                  << " [" << module << "] "
                  << message << "\033[0m" << std::endl;

        if (level == Level::Fatal) {
            std::abort();  // <-- 如果是Fatal，直接终止程序
        }
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

    inline void Fatal(const std::string& module, const std::string& message) {
        Print(Level::Fatal, module, message);
        
    }

    #define LOG_INFO(module, message)    Log::Info(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")
    #define LOG_WARNING(module, message) Log::Warning(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")
    #define LOG_ERROR(module, message)   Log::Error(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")
    #define LOG_FATAL(module, message)   Log::Fatal(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")

    // =================== 轻量级模板版本 ===================
    #define LOG_INFO_TEMPLATE(module, message) \
        Log::Info(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")

    #define LOG_WARNING_TEMPLATE(module, message) \
        Log::Warning(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")

    #define LOG_ERROR_TEMPLATE(module, message) \
        Log::Error(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")

    #define LOG_FATAL_TEMPLATE(module, message) \
        Log::Fatal(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")



} // namespace Log
