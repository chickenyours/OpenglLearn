#pragma once

#include <string>
#include <fstream>

namespace Tool{

class FileLoader{
    public:
        static bool LoadFileToString(const std::string& filePath, std::string& target){
            std::ifstream file(filePath);
            if (!file.is_open()) {
                return false;
            }

            // 执行了一次流的全读取操作
            target.assign((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
            file.close();
            return true;
        }
};


}