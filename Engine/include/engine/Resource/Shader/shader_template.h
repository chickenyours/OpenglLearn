#pragma once

#include <string>

namespace Resource{
    struct ShaderTemplate{
        std::string version = "#version 460 core";

        std::string inputBlock;
        std::string fragmentInputName;

        

    };
}