#pragma once
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>
namespace IWanna {
using Clock = std::chrono::steady_clock;
inline double Milliseconds(Clock::duration duration) {return std::chrono::duration<double,std::milli>(duration).count();}
struct FrameSample {double simulation,build,render,total;size_t commands;};
struct FrameMetrics {
    std::vector<FrameSample> samples;
    void Save(const std::string& path) const {
        if(samples.size()<=5) return;
        std::vector<double> total;double sim=0,build=0,render=0,commands=0;
        std::ofstream csv;if(!path.empty()) {csv.open(path);csv<<"simulation_ms,build_ms,render_thread_ms,total_ms,commands\n";}
        for(size_t i=5;i<samples.size();++i) {const auto& s=samples[i];total.push_back(s.total);sim+=s.simulation;build+=s.build;render+=s.render;commands+=s.commands;
            if(csv) csv<<s.simulation<<','<<s.build<<','<<s.render<<','<<s.total<<','<<s.commands<<'\n';}
        auto n=double(total.size());auto mean=std::accumulate(total.begin(),total.end(),0.0)/n;std::sort(total.begin(),total.end());
        std::cout<<"BENCHMARK frames="<<total.size()<<" simulation_ms="<<sim/n<<" build_ms="<<build/n<<" render_thread_ms="<<render/n
            <<" mean_ms="<<mean<<" p95_ms="<<total[size_t((total.size()-1)*.95)]<<" effective_fps="<<1000/mean<<" commands="<<commands/n<<'\n';
    }
};
}
