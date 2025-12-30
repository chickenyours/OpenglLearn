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
    ShaderDescription operator+(const ShaderDescription& other) const {
        ShaderDescription result;
        result.macros = this->macros;
        result.macros.insert(result.macros.end(), other.macros.begin(), other.macros.end());
        return result;
    }

    ShaderDescription(const std::vector<std::string>& macros):macros(macros){}
    ShaderDescription() = default;
};

} // namespace Resource
