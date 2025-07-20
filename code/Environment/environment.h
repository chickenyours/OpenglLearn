#pragma once

#include <atomic>
#include <chrono>

#include "code/Config/config.h"

namespace Environment{
    class Environment{
        public:
            static Environment& Instance(){
                static Environment instance;
                return instance;
            }
            void Update(){

                // 更新时间
                auto now = std::chrono::high_resolution_clock::now();
                std::chrono::duration<float> delta = now - lastTime;
                accumulatedTime += delta.count();
                lastTime = now;

            }
            float GetTime(){
                return accumulatedTime;
            }

            bool isWindowSizeChange;
            glm::ivec2 windowSize = glm::ivec2(SCR_WIDTH, SCR_HEIGHT);
        private:
            Environment():accumulatedTime(0.0),lastTime(std::chrono::high_resolution_clock::now()){}
            float accumulatedTime; // 单位：秒
            std::chrono::high_resolution_clock::time_point lastTime;

    };
}