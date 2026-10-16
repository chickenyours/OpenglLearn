#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>

namespace Model::detail {

// Animation time is not physics time while an animation is held and cloth
// settles. Retain the submitted piecewise-linear clock until physics consumes
// it, so suspension can discard pending work at the last displayed pose.
class PreviewAnimationClock {
    struct Sample {double seconds=0,animation=0;};
    std::deque<Sample> samples_;
public:
    void Clear(){samples_.clear();}
    void Reset(double seconds,double animation){samples_.clear();samples_.push_back({seconds,animation});}
    std::size_t Size()const noexcept{return samples_.size();}
    void Record(double seconds,double animation){
        if(samples_.empty()){Reset(seconds,animation);return;}
        if(seconds<=samples_.back().seconds)return;
        if(samples_.size()>1){const auto& a=samples_[samples_.size()-2];const auto& b=samples_.back();
            const long double expected=static_cast<long double>(b.animation)+
                (static_cast<long double>(b.animation)-a.animation)*(seconds-b.seconds)/(b.seconds-a.seconds);
            const double scale=std::max({1.,std::abs(a.animation),std::abs(b.animation),std::abs(animation)});
            // Compact ordinary continuous playback/settling instead of storing
            // one point per rendered frame. Only floating roundoff is merged.
            if(std::abs(expected-animation)<=64*std::numeric_limits<double>::epsilon()*scale){samples_.back()={seconds,animation};return;}
        }
        samples_.push_back({seconds,animation});
    }
    double At(double seconds,double fallback=0)const{
        if(samples_.empty())return fallback;
        if(seconds<=samples_.front().seconds)return samples_.front().animation;
        for(std::size_t i=1;i<samples_.size();++i)if(seconds<=samples_[i].seconds){const auto& a=samples_[i-1];const auto& b=samples_[i];
            return a.animation+(b.animation-a.animation)*std::clamp((seconds-a.seconds)/(b.seconds-a.seconds),0.,1.);}
        return samples_.back().animation;
    }
    void Consume(double seconds){while(samples_.size()>2&&samples_[1].seconds<=seconds)samples_.pop_front();}
    double Rebase(double completedSeconds,double fallback=0){const double animation=At(completedSeconds,fallback);Reset(completedSeconds,animation);return animation;}
};

} // namespace Model::detail
