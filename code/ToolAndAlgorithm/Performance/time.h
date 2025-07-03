#pragma once

#include <string>
#include <chrono>
#include <iostream>
#include <functional>

#include "code/DebugTool/ConsoleHelp/color_log.h"

template<typename Func>
auto MeasureTime(const std::string& label, Func&& func) -> decltype(func()) {
    auto start = std::chrono::high_resolution_clock::now();
    auto result = func();  // 执行目标函数
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // std::cout << label << " took " << duration.count() << " μs" << std::endl;
    LOG_INFO("MeasureTime", label + " took " + std::to_string(duration.count()) + " μs");
    return result; // 返回原始函数结果
}

template<typename Func>
auto MeasureTimeMs(const std::string& label, Func&& func, const std::string position) -> decltype(func()) {
    auto start = std::chrono::high_resolution_clock::now();
    auto result = func();
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    LOG_INFO("MeasureTimeMs", label + " took " + std::to_string(duration.count()) + " ms" + position);
    return result;
}

#define MEASURETIMEMS(label, func) MeasureTimeMs(label, [&](){ return func; }, std::string(" (") + __FILE__ + ":" + std::to_string(__LINE__) + ", @" + __func__ + ")" )