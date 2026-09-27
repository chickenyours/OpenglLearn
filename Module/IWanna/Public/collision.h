#pragma once
#include "game_components.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace IWanna {
inline void UpdatePolygon(const Transform& t,Collider& c) {
    if(c.cacheValid && c.cachedPosition==t.position && c.cachedSize==t.size && c.cachedRotation==t.rotation && c.cachedLocal==c.points) return;
    c.cachedLocal=c.points;c.cachedPosition=t.position;c.cachedSize=t.size;c.cachedRotation=t.rotation;c.cacheValid=true;
    c.world.resize(c.points.size());const float a=t.rotation*.01745329252f,cs=std::cos(a),sn=std::sin(a);
    c.minimum=glm::vec2(std::numeric_limits<float>::max());c.maximum=-c.minimum;
    for(size_t i=0;i<c.points.size();++i) {
        const auto p=c.points[i]*t.size*.5f;auto w=t.position+glm::vec2(cs*p.x-sn*p.y,sn*p.x+cs*p.y);
        c.world[i]=w;c.minimum=glm::min(c.minimum,w);c.maximum=glm::max(c.maximum,w);
    }
}
inline bool BoundsOverlap(const Collider& a,const Collider& b) {
    return a.maximum.x>=b.minimum.x && b.maximum.x>=a.minimum.x && a.maximum.y>=b.minimum.y && b.maximum.y>=a.minimum.y;
}
inline std::vector<glm::vec2> WorldPolygon(const Transform& t,const Collider& c) {
    std::vector<glm::vec2> points; points.reserve(c.points.size());
    float a=t.rotation*.01745329252f, cs=std::cos(a),sn=std::sin(a);
    for(auto p:c.points) { p*=t.size*.5f; points.push_back(t.position+glm::vec2(cs*p.x-sn*p.y,sn*p.x+cs*p.y)); }
    return points;
}
// Separating-axis overlap and minimum translation. Normal points out of B.
inline bool Overlap(const std::vector<glm::vec2>& a,const std::vector<glm::vec2>& b,glm::vec2& mtv) {
    if(a.size()<3 || b.size()<3) return false;
    glm::vec2 amin=a[0],amax=a[0],bmin=b[0],bmax=b[0];
    for(auto p:a) { amin=glm::min(amin,p); amax=glm::max(amax,p); }
    for(auto p:b) { bmin=glm::min(bmin,p); bmax=glm::max(bmax,p); }
    if(amax.x<bmin.x || bmax.x<amin.x || amax.y<bmin.y || bmax.y<amin.y) return false;
    float best=std::numeric_limits<float>::max();
    for(const auto* polygon:{&a,&b}) for(size_t i=0;i<polygon->size();++i) {
        auto edge=(*polygon)[(i+1)%polygon->size()]-(*polygon)[i]; float len=glm::length(edge); if(len<1e-6f) continue;
        glm::vec2 n{-edge.y/len,edge.x/len};
        float loA=glm::dot(a[0],n),hiA=loA,loB=glm::dot(b[0],n),hiB=loB;
        for(auto p:a) {float q=glm::dot(p,n);loA=std::min(loA,q);hiA=std::max(hiA,q);}
        for(auto p:b) {float q=glm::dot(p,n);loB=std::min(loB,q);hiB=std::max(hiB,q);}
        if(hiA<loB || hiB<loA) return false;
        const float left=hiA-loB,right=hiB-loA;
        const float distance=std::min(left,right);
        if(distance<best) {best=distance; mtv=(left<right?-n:n)*(distance+.0001f);}
    }
    return best<std::numeric_limits<float>::max();
}
}
