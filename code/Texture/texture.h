#pragma once

#include <string>

using namespace std;

namespace Render
{
    class Texture {
        public:
            Texture(string type,string path = "default");
            string GetPath();
            unsigned int GetID();
            bool Load();
            ~Texture();
            void Print(int tabs);
        private:
            unsigned int _id;
            string _path;
            // 通常对应着色器的采样器类型
            string _type;
    }; 
} // namespace Render


