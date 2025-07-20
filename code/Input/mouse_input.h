#pragma once
#include <windows.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Input {

class MouseInput {
public:
    static MouseInput& Instance() {
        static MouseInput instance;
        return instance;
    }

    enum MouseButton {
        LEFT = 0,
        RIGHT = 1,
        MIDDLE = 2,
        X1 = 3,
        X2 = 4,
        BUTTON_COUNT = 5
    };

    enum ButtonState {
        NONE,
        PRESSED,    // 刚按下
        HELD,       // 持续按住
        RELEASED    // 刚释放
    };

    void Update() {
        static constexpr int vkMouse[BUTTON_COUNT] = {
            VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
        };

        POINT pt;
        GetCursorPos(&pt);

        offset = {pt.x - oldPos.x, pt.y - oldPos.y};
        oldPos = pt;

        for (int i = 0; i < BUTTON_COUNT; ++i) {
            bool isDown = (GetAsyncKeyState(vkMouse[i]) & 0x8000) != 0;

            if (isDown) {
                if (!_prevState[i]) {
                    _state[i] = PRESSED;
                } else {
                    _state[i] = HELD;
                }
            } else {
                if (_prevState[i]) {
                    _state[i] = RELEASED;
                } else {
                    _state[i] = NONE;
                }
            }

            _prevState[i] = isDown;
        }

        // 获取滚轮状态（只在注册了窗口回调时才能生效）
        // _wheelDelta = 0;
        // MSG msg;
        // while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        //     if (msg.message == WM_MOUSEWHEEL) {
        //         _wheelDelta += GET_WHEEL_DELTA_WPARAM(msg.wParam) / (float)WHEEL_DELTA;
        //     }
        //     TranslateMessage(&msg);
        //     DispatchMessage(&msg);
        // }
    }

    ButtonState GetButtonState(MouseButton button) const {
        return _state[button];
    }

    float GetWheelDelta() const {
        return _wheelDelta;
    }

    glm::ivec2 GetMouseOffset(){
        return offset;
    }

    glm::ivec2 GetMousePos(){
        return {oldPos.x,oldPos.y};
    }

private:
    MouseInput(){
        GetCursorPos(&oldPos);
    }

    bool _prevState[BUTTON_COUNT] = {};
    ButtonState _state[BUTTON_COUNT] = {};
    float _wheelDelta = 0.0f;
    POINT oldPos;
    glm::ivec2 offset = {};
};

} // namespace Input
