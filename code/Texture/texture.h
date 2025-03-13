#pragma once

#include <string>

using namespace std;

namespace Render
{
    class Texture {
        public:
            Texture(string type,string path = "default", unsigned int channel = 0);
            string GetPath();
            unsigned int GetID();
            inline unsigned int GetChannel(){return _channel;}
            bool Load();
            ~Texture();
            void Print();
        private:
            unsigned int _id;
            unsigned int _channel;
            string _path;
            // 通常对应着色器的采样器类型
            string _type;
    }; 
} // namespace Render


