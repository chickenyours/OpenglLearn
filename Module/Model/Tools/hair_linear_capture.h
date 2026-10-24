#pragma once
#include <json/json.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace Model::EditorPreview {
// Offline diagnostics only. Data comes from the completed Render/RHI frame;
// this writer has no graphics API and is never invoked by normal rendering.
inline Json::Value WriteHairLinearTexture(const std::filesystem::path& directory,
    const char* name,const char* meaning,const char* alpha,
    unsigned width,unsigned height,const std::vector<float>& pixels) {
    if(!width||!height||pixels.size()!=std::size_t(width)*height*4)
        throw std::runtime_error("Hair linear capture dimensions do not match the RHI texture");
    for(float value:pixels)if(!std::isfinite(value))
        throw std::runtime_error("Non-finite value in hair linear RHI capture");
    const auto filename=std::string(name)+".rgba32f";
    std::ofstream file(directory/filename,std::ios::binary);
    file.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()*sizeof(float)));
    if(!file)throw std::runtime_error("Could not write hair linear RHI capture");
    Json::Value item;item["file"]=filename;item["width"]=width;item["height"]=height;
    item["channels"]=4;item["dtype"]="float32-le";item["origin"]="bottom-left";
    item["meaning"]=meaning;item["alpha"]=alpha;
    return item;
}
}
