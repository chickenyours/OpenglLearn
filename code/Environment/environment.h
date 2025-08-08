#pragma once

#include <atomic>
#include <chrono>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/Config/config.h"

namespace Environment{
    class Environment{
        public:
            Environment():accumulatedTime(0.0),lastTime(std::chrono::high_resolution_clock::now()){}

            static Environment& Instance(){
                return *instance_;
            }

            static void SetInstance(Environment* instance){
                instance_ = instance;
            }

            void Update(){

                // 更新时间
                auto now = std::chrono::high_resolution_clock::now();
                std::chrono::duration<float> delta = now - lastTime;
                accumulatedTime += delta.count();
                updateIntervalTime = std::chrono::duration<float>(now - lastTime).count();
                lastTime = now;
            }
            float GetTime(){
                return accumulatedTime;
            }

            float GetUpdateIntervalTime(){
                return updateIntervalTime;
            }

            bool isWindowSizeChange;
            glm::ivec2 windowSize = glm::ivec2(SCR_WIDTH, SCR_HEIGHT);
        private:
            inline static Environment* instance_ = nullptr;
            float accumulatedTime; // 单位：秒
            float updateIntervalTime = 0.0;
            std::chrono::high_resolution_clock::time_point lastTime;

    };
}