#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace IWanna {
// Original stroke glyphs baked to signed-distance alpha; no font dependency.
inline std::vector<unsigned char> GlyphSdf(int glyph,int width=48,int height=64) {
    static constexpr std::string_view paths[]={
        "10 30 41 45 36 16 05 01 10|14 32", "11 20 26|16 36", "01 10 30 41 42 06 46", "00 40 23 33 44 45 36 16 05", "30 04 44|30 36",
        "40 00 03 33 44 45 36 16 05", "40 20 01 05 16 36 45 44 33 03", "00 40 16", "10 30 41 42 33 13 02 01 10|13 04 05 16 36 45 44 33", "43 13 02 01 10 30 41 45 26 06",
        "06 20 46|14 34", "00 06 36 45 44 33 03|00 30 41 42 33", "41 30 10 01 05 16 36 45", "00 06 26 45 41 20 00", "40 00 06 46|03 33",
        "40 00 06|03 33", "41 30 10 01 05 16 36 45 43 23", "00 06|40 46|03 43", "10 30|20 26|16 36", "10 40 45 36 16 05",
        "00 06|40 03 46", "00 06 46", "06 00 23 40 46", "06 00 46 40", "10 30 41 45 36 16 05 01 10",
        "06 00 30 41 42 33 03", "10 30 41 45 36 16 05 01 10|34 46", "06 00 30 41 42 33 03|23 46", "41 30 10 01 02 13 33 44 45 36 16 05", "00 40|20 26",
        "00 05 16 36 45 40", "00 26 40", "00 16 23 36 40", "00 46|40 06", "00 23 40|23 26", "00 40 06 46"};
    std::vector<std::pair<glm::vec2,glm::vec2>> segments;glm::vec2 last{};bool hasLast=false;
    auto path=paths[glyph];
    for(size_t i=0;i<path.size();) {
        if(path[i]=='|') {hasLast=false;++i;continue;}
        if(path[i]==' ') {++i;continue;}
        glm::vec2 p{float(path[i]-'0'),float(path[i+1]-'0')};i+=2;
        if(hasLast) segments.push_back({last,p});last=p;hasLast=true;
    }
    std::vector<unsigned char> pixels(size_t(width)*height*4,255);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        glm::vec2 p{(x+.5f)/width*6-1,(y+.5f)/height*8-1};float distance=100;
        for(auto [a,b]:segments) {auto e=b-a;float t=std::clamp(glm::dot(p-a,e)/glm::dot(e,e),0.f,1.f);distance=std::min(distance,glm::length(p-a-e*t));}
        pixels[(size_t(y)*width+x)*4+3]=static_cast<unsigned char>(std::clamp(.5f+(.28f-distance)*.4f,0.f,1.f)*255);
    }
    return pixels;
}
}
