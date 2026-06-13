#pragma once


#include "engine/DebugTool/ConsoleHelp/color_log.h"


struct CommandDoubleBuffer{
    
    struct Buffer{
        char* data;
        size_t data_used_size = 0;
        
        size_t capcity;
        Buffer(size_t size):capcity(size){ data = new char[capcity]; }
        ~Buffer(){ delete[] data; }
        void Reset(){ data_used_size = 0; }
        bool Push(size_t size, void* from){
            if(data_used_size + size > capcity){
                LOG_ERROR("CommandDoubleBuffer","oversize: " + std::to_string(data_used_size) + " " + std::to_string(size));
                return false;
            }
            std::memcpy(from,reinterpret_cast<void*>(data[data_used_size - 1]),size);
            data_used_size += size;
            return true;
        }
    };

    Buffer buffer1;
    Buffer buffer2;
    
    Buffer* frontBuffer;
    Buffer* backBuffer;
    size_t back_used = 0;

    CommandDoubleBuffer(size_t size):buffer1(size),buffer2(size){}
    CommandDoubleBuffer(const CommandDoubleBuffer&) = delete;
    CommandDoubleBuffer(CommandDoubleBuffer&&) = delete;
    ~CommandDoubleBuffer(){}

    bool Push(size_t size, void* from){
        return frontBuffer->Push(size,from);
    }

    bool Pop(size_t size){
        if(back_used + size + 1 > backBuffer->data_used_size){
            LOG_ERROR("CommandDoubleBuffer","oversize: " + std::to_string(backBuffer->data_used_size) + " " + std::to_string(size));
            return false;
        }
        back_used += size;
        return true;
    }

    bool Peer(size_t size, void* to){
        if(back_used + size > backBuffer->data_used_size){
            LOG_ERROR("CommandDoubleBuffer","oversize: " + std::to_string(backBuffer->data_used_size) + " " + std::to_string(size));
            return false;
        }
        std::memcpy(reinterpret_cast<void*>(backBuffer->data[back_used]),to,size);
        return true;
    }

    void Switch(){
        Buffer* temp = frontBuffer;
        frontBuffer = backBuffer;
        backBuffer = temp;
        back_used = 0;
    }


};

namespace Render{
    class RHICommandSystem{
        public:
            CommandDoubleBuffer createResourceBuffer;
            CommandDoubleBuffer deleteResourceBuffer;
            CommandDoubleBuffer graphicsBuffer;
    };
}