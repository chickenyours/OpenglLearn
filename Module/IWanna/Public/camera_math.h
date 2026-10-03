#pragma once
#include "game_components.h"
#include <algorithm>
#include <cmath>

namespace IWanna {
inline constexpr glm::vec2 CameraHalfView{75.f,42.1875f};
inline glm::vec2 CameraProject(const Camera& camera,glm::vec2 point) {
    return (point-camera.center)*camera.zoom;
}
inline Transform CameraProject(const Camera& camera,Transform transform) {
    transform.position=CameraProject(camera,transform.position);
    transform.size*=camera.zoom;
    return transform;
}
inline glm::vec2 CameraClamp(glm::vec2 target,glm::vec2 origin,glm::vec2 extent,float zoom) {
    auto half=CameraHalfView/zoom;
    for(int axis=0;axis<2;++axis) {
        if(extent[axis]<=2*half[axis])target[axis]=origin[axis]+extent[axis]*.5f;
        else target[axis]=std::clamp(target[axis],origin[axis]+half[axis],origin[axis]+extent[axis]-half[axis]);
    }
    return target;
}
}
