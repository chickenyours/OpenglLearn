#pragma once
#include "game_components.h"
#include <array>
#include <cmath>
namespace IWanna {
inline glm::vec2 RotateOffset(glm::vec2 point,float degrees){
    const float angle=degrees*.01745329252f,cs=std::cos(angle),sn=std::sin(angle);
    return {cs*point.x-sn*point.y,sn*point.x+cs*point.y};
}
inline std::array<glm::vec2,4> Corners(const Transform& transform){
    glm::vec2 half=transform.size*.5f;
    return {transform.position+RotateOffset({-half.x,-half.y},transform.rotation),
        transform.position+RotateOffset({half.x,-half.y},transform.rotation),
        transform.position+RotateOffset({half.x,half.y},transform.rotation),
        transform.position+RotateOffset({-half.x,half.y},transform.rotation)};
}
}
