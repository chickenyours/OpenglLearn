#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace Model::detail {

// The alternating atan series on |z| <= sqrt(2)-1 has a degree-21
// remainder <= |z|^23/23 < 6.831e-11 radians. Range reduction and double
// rounding leave the total absolute error below 7e-11 radians.
inline double FastAtan2(double y,double x){
    constexpr double pi=3.141592653589793238462643383279502884;
    if(!std::isfinite(x)||!std::isfinite(y))return std::atan2(y,x);
    if(y==0)return std::signbit(x)?std::copysign(pi,y):y;
    if(x==0)return std::copysign(pi/2,y);
    constexpr double tanPiOver8=.414213562373095048801688724209698079;
    const double ax=std::abs(x),ay=std::abs(y),ratio=std::min(ax,ay)/std::max(ax,ay);
    // CRT atan2 may round an extreme scaled ratio to signed zero while a
    // standalone division rounds to denorm_min. Preserve its underflow and
    // signed-zero behavior instead of passing subnormal ratios to the series.
    if(ratio<std::numeric_limits<double>::min())return std::atan2(y,x);
    const bool reduced=ratio>tanPiOver8;
    const double z=reduced?(ratio-1)/(ratio+1):ratio,z2=z*z;
    double polynomial=1.0/21;
    polynomial=-1.0/19+z2*polynomial;
    polynomial=1.0/17+z2*polynomial;
    polynomial=-1.0/15+z2*polynomial;
    polynomial=1.0/13+z2*polynomial;
    polynomial=-1.0/11+z2*polynomial;
    polynomial=1.0/9+z2*polynomial;
    polynomial=-1.0/7+z2*polynomial;
    polynomial=1.0/5+z2*polynomial;
    polynomial=-1.0/3+z2*polynomial;
    double angle=(reduced?pi/4:0)+z*(1+z2*polynomial);
    if(ay>ax)angle=pi/2-angle;
    if(x<0)angle=pi-angle;
    return std::copysign(angle,y);
}

} // namespace Model::detail
