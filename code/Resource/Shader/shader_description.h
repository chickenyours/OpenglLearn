#pragma once

#include <vector>
#include <string>

namespace Resource{

struct ShaderDescription{
    std::vector<std::string> macros;
    std::string GetInfo(){
        std::string info = "Shader Macros:\n";
        for (const auto& macro : macros) {
            info += "- " + macro + "\n";
        }
        return info;
    }
};

} // namespace Resource
