#pragma once

#include <string>

namespace Render
{
    class Texture {
        public:
            Texture(std::string type,std::string path = "default");
            std::string GetPath();
            unsigned int GetID();
            bool Load();
            ~Texture();
            void Print(int tabs);
        private:
            unsigned int _id;
            std::string _path;
            // 通常对应着色器的采样器类型
            std::string _type;
    }; 
} // namespace Render


