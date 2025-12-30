#pragma once

namespace Setting{
    struct Setting
    {
        public:
            static Setting& Instance(){
                static Setting instance;
                return instance;
            }
            float sensitivity = 0.1;
        private:
            Setting() = default;
    };
    
}