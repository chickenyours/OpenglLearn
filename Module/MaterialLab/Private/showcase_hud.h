#pragma once
#include <cctype>
#include <sstream>
#include "MaterialLab/Public/lab_geometry.h"
#include "MaterialLab/Public/showcase_scene.h"

namespace MaterialLab {
// Small embedded bitmap alphabet: the laboratory needs no external font/UI
// dependency. Rasterize only when help, selection or light switches change.
inline std::array<unsigned char,7> HudGlyph(char c) {
    switch(std::toupper(static_cast<unsigned char>(c))) {
    case 'A':return {14,17,17,31,17,17,17};case 'B':return {30,17,17,30,17,17,30};
    case 'C':return {14,17,16,16,16,17,14};case 'D':return {30,17,17,17,17,17,30};
    case 'E':return {31,16,16,30,16,16,31};case 'F':return {31,16,16,30,16,16,16};
    case 'G':return {14,17,16,23,17,17,15};case 'H':return {17,17,17,31,17,17,17};
    case 'I':return {14,4,4,4,4,4,14};case 'J':return {7,2,2,2,18,18,12};
    case 'K':return {17,18,20,24,20,18,17};case 'L':return {16,16,16,16,16,16,31};
    case 'M':return {17,27,21,21,17,17,17};case 'N':return {17,25,21,19,17,17,17};
    case 'O':return {14,17,17,17,17,17,14};case 'P':return {30,17,17,30,16,16,16};
    case 'Q':return {14,17,17,17,21,18,13};case 'R':return {30,17,17,30,20,18,17};
    case 'S':return {15,16,16,14,1,1,30};case 'T':return {31,4,4,4,4,4,4};
    case 'U':return {17,17,17,17,17,17,14};case 'V':return {17,17,17,17,17,10,4};
    case 'W':return {17,17,17,21,21,21,10};case 'X':return {17,17,10,4,10,17,17};
    case 'Y':return {17,17,10,4,4,4,4};case 'Z':return {31,1,2,4,8,16,31};
    case '0':return {14,17,19,21,25,17,14};case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};case '5':return {31,16,16,30,1,1,30};
    case '6':return {14,16,16,30,17,17,14};case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};case '9':return {14,17,17,15,1,1,14};
    case ':':return {0,4,4,0,4,4,0};case '/':return {1,1,2,4,8,16,16};
    case '-':return {0,0,0,31,0,0,0};case '.':return {0,0,0,0,0,4,4};
    case '+':return {0,4,4,31,4,4,0};case '[':return {14,8,8,8,8,8,14};
    case ']':return {14,2,2,2,2,2,14};default:return {};
    }
}
inline std::string ShowcaseHudText(const ShowcaseSettings& s,bool gi,bool updating,
                                  glm::uvec2 renderSize={},glm::uvec2 outputSize={}) {
    std::ostringstream t;
    t<<"LIGHTING COURTYARD / GALLERY    F1 HELP\nSELECTED "<<s.selected+1<<": "<<ShowcaseMovable(s.selected).name;
    t<<"\nGI "<<(gi?(updating?"UPDATING":"READY"):"OFF")<<"  SUN "<<s.sunlight<<"  POINT "<<s.pointLights<<"  AREA "<<s.areaLights<<"  EMISSION "<<s.emission;
    if(renderSize.x&&outputSize.x)t<<"\nRENDER "<<renderSize.x<<"X"<<renderSize.y<<" / OUTPUT "<<outputSize.x<<"X"<<outputSize.y<<"  P SCALE";
    if(s.help)t<<"\nRMB LOOK  WASD MOVE  Q/E DOWN/UP  SHIFT FAST"
        "\n1 COURTYARD  2 INTERIOR  3 POOL  F FOCUS"
        "\nCLICK/TAB SELECT  ARROWS MOVE X/Z  PGUP/PGDN Y"
        "\nZ/X ROTATE  R RESET PROP  BACKSPACE RESET ALL"
        "\nF5 SUN  F6 POINT  F7 AREA  F8 EMISSION  F9 SKY"
        "\nL GI  B BLOOM  O AO  N AA  M BLUR  SPACE ANIMATE";
    return t.str();
}
inline TexturePixels RasterizeShowcaseHud(const std::string& text) {
    constexpr unsigned w=688,h=206;
    TexturePixels image{w,h,std::vector<std::uint8_t>(w*h*4,0)};
    const auto put=[&](unsigned x,unsigned y,unsigned char r,unsigned char g,unsigned char b,unsigned char a) {
        if(x>=w||y>=h)return;auto i=((h-1-y)*w+x)*4;
        image.rgba[i]=r;image.rgba[i+1]=g;image.rgba[i+2]=b;image.rgba[i+3]=a;
    };
    unsigned lines=1;for(char c:text)if(c=='\n')++lines;
    for(unsigned y=0;y<std::min(h,lines*20+8);++y)for(unsigned x=0;x<w;++x)put(x,y,10,15,20,190);
    unsigned col=0,row=0;
    for(char c:text) {
        if(c=='\n'){col=0;++row;continue;}auto glyph=HudGlyph(c);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<5;++x)if(glyph[y]&(1u<<(4-x)))
            for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx)
                put(8+col*12+x*2+sx,7+row*20+y*2+sy,row==1?255:205,row==1?207:225,row==1?104:235,255);
        ++col;
    }
    return image;
}
} // namespace MaterialLab
