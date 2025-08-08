#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdlib.h>
#include <regex>
#include <functional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define LOG_INFO_COLOR    "\033[94m" // 亮蓝色
#define LOG_WARNING_COLOR "\033[93m" // 亮黄色
#define LOG_ERROR_COLOR   "\033[91m" // 亮红色
#define LOG_FATAL_COLOR   "\033[95m" // 亮紫色
#define LOG_COLOR_RESET   "\033[0m"   // 重置颜色
#define LOG_COLOR_BLACK   "\033[30m" // 黑色
#define LOG_COLOR_RED     "\033[31m" // 红色
#define LOG_COLOR_GREEN   "\033[32m" // 绿色
#define LOG_COLOR_YELLOW  "\033[33m" // 黄色
#define LOG_COLOR_BLUE    "\033[34m" // 蓝色
#define LOG_COLOR_MAGENTA "\033[35m" // 品红色
#define LOG_COLOR_CYAN    "\033[36m" // 青色
#define LOG_COLOR_WHITE   "\033[37m" // 白色

#define LOG_COLOR_BRIGHT_BLACK   "\033[90m" // 亮黑色（灰色）
#define LOG_COLOR_BRIGHT_RED     "\033[91m" // 亮红色
#define LOG_COLOR_BRIGHT_GREEN   "\033[92m" // 亮绿色
#define LOG_COLOR_BRIGHT_YELLOW  "\033[93m" // 亮黄色
#define LOG_COLOR_BRIGHT_BLUE    "\033[94m" // 亮蓝色
#define LOG_COLOR_BRIGHT_MAGENTA "\033[95m" // 亮品红色
#define LOG_COLOR_BRIGHT_CYAN    "\033[96m" // 亮青色
#define LOG_COLOR_BRIGHT_WHITE   "\033[97m" // 亮白色

#define LOG_COLOR_BG_BLACK   "\033[40m" // 背景黑色
#define LOG_COLOR_BG_RED     "\033[41m" // 背景红色
#define LOG_COLOR_BG_GREEN   "\033[42m" // 背景绿色
#define LOG_COLOR_BG_YELLOW  "\033[43m" // 背景黄色
#define LOG_COLOR_BG_BLUE    "\033[44m" // 背景蓝色
#define LOG_COLOR_BG_MAGENTA "\033[45m" // 背景品红色
#define LOG_COLOR_BG_CYAN    "\033[46m" // 背景青色
#define LOG_COLOR_BG_WHITE   "\033[47m" // 背景白色

#define LOG_COLOR_BG_BRIGHT_BLACK   "\033[100m" // 背景亮黑色（灰色）
#define LOG_COLOR_BG_BRIGHT_RED     "\033[101m" // 背景亮红色
#define LOG_COLOR_BG_BRIGHT_GREEN   "\033[102m" // 背景亮绿色
#define LOG_COLOR_BG_BRIGHT_YELLOW  "\033[103m" // 背景亮黄色
#define LOG_COLOR_BG_BRIGHT_BLUE    "\033[104m" // 背景亮蓝色
#define LOG_COLOR_BG_BRIGHT_MAGENTA "\033[105m" // 背景亮品红色
#define LOG_COLOR_BG_BRIGHT_CYAN    "\033[106m" // 背景亮青色
#define LOG_COLOR_BG_BRIGHT_WHITE   "\033[107m" // 背景亮白色

namespace Log {

    // base code

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

    // common log functions, using in any programs
    #define LOG_INFO(module, message)    Log::Info(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")
    #define LOG_WARNING(module, message) Log::Warning(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")
    #define LOG_ERROR(module, message)   Log::Error(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")
    #define LOG_FATAL(module, message)   Log::Fatal(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")


    // common template log functions
    #define LOG_INFO_TEMPLATE(module, message) \
        Log::Info(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")

    #define LOG_WARNING_TEMPLATE(module, message) \
        Log::Warning(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")

    #define LOG_ERROR_TEMPLATE(module, message) \
        Log::Error(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")

    #define LOG_FATAL_TEMPLATE(module, message) \
        Log::Fatal(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", " + std::string(__PRETTY_FUNCTION__) + ")")
    
    class StackLogErrorHandle{
        public:
            StackLogErrorHandle(std::nullptr_t){
                isSource = true;
                stackErrorChainInfoArray_ = new std::vector<std::pair<std::string,std::string>>();
            }
            StackLogErrorHandle(const StackLogErrorHandle& other){
                isSource = false;
                other.isLatest = false;
                stackErrorChainInfoArray_ = other.stackErrorChainInfoArray_;
            }
            // StackLogErrorHandle(StackLogErrorHandle&&) = delete;

            StackLogErrorHandle(StackLogErrorHandle&& other) noexcept
                : StackLogErrorHandle(other) {
                // 实际上调用 copy 构造：制造新的链节点
            }

            void ReportError(const std::string& moudleName, const std::string& errMsg){
                
                if(stackErrorChainInfoArray_){
                    stackErrorChainInfoArray_->push_back(
                        {moudleName,errMsg}
                    );
                }
                else{
                    std::cout<<"hh" << std::endl;
                }

                if(isSource){
                    std::stringstream ss;
                    for(int i = stackErrorChainInfoArray_->size() - 1; i >= 0; i--){
                        ss << "\t[" + (*stackErrorChainInfoArray_)[i].first + "]" + " " + (*stackErrorChainInfoArray_)[i].second + "\n";
                    }
                    std::cerr << "\033[91m[ERROR] Traceback:\n" << ss.str() << "\033[0m";
                }
                else if(isLatest){
                    // std::cerr << "\033[91m[ERROR] An error occurred, awaiting error chain processing\n" << "\033[0m";
                }
            }
            ~StackLogErrorHandle(){
                if(isSource && stackErrorChainInfoArray_){
                    delete stackErrorChainInfoArray_;
                    stackErrorChainInfoArray_ = nullptr;
                }
            }
        private:
            bool isSource = false;
            mutable bool isLatest = true;
            std::vector<std::pair<std::string,std::string>>* stackErrorChainInfoArray_;
    };

    #define REPORT_STACK_ERROR(handle, module, message) \
        (handle).ReportError(module, std::string(message) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")")

    #define STACK_RETURN_IF_FAIL(funcCall, handle, module, message) \
    if (!(funcCall)) { \
        REPORT_STACK_ERROR(handle, module, message); \
        return false; \
    }


} // namespace Log
