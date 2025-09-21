#pragma once

#include "code/ToolAndAlgorithm/Json/json_helper.h"
#include <unordered_map>
#include <string>



using MemoryInsert = int(*)(const Json::Value& args, void* addr);

class MemoryInsertManager{
    private:
        static std::unordered_map<std::string,MemoryInsert> functions;
    public:
        inline static const std::unordered_map<std::string,MemoryInsert>& GetFunctions(){
            return functions;
        }
};

int SetObjectProperty(const Json::Value& data, const Json::Value& classBluePrint, void* addr);