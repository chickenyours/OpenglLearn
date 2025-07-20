#pragma once
#include <windows.h>

namespace Input{
    // 键盘输入状态缓存模块
class KeyboardInput {
    public:
        static KeyboardInput& Instance(){
            static KeyboardInput instance;
            return instance;
        }

        static constexpr int KEY_COUNT = 256;

        enum KeyState {
            NONE,
            PRESSED,    // 刚按下
            HELD,       // 持续按住
            RELEASED    // 刚释放
        };

        void Update() {
            for (int vk = 0; vk < KEY_COUNT; ++vk) {
                bool isDown = (GetAsyncKeyState(vk) & 0x8000) != 0;

                if (isDown) {
                    if (_prevState[vk] == false) {
                        _state[vk] = PRESSED;
                    } else {
                        _state[vk] = HELD;
                    }
                } else {
                    if (_prevState[vk] == true) {
                        _state[vk] = RELEASED;
                    } else {
                        _state[vk] = NONE;
                    }
                }

                _prevState[vk] = isDown;
            }
        }

        KeyState GetKeyState(int vk_code) const {
            return _state[vk_code];
        }

    private:
        KeyboardInput() = default;
        bool _prevState[KEY_COUNT] = {};
        KeyState _state[KEY_COUNT] = {};
};
} // Input
