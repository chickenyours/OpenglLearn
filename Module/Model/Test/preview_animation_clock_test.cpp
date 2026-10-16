#include "Model/Private/preview_animation_clock.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using Model::detail::PreviewAnimationClock;
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool Near(double a,double b){return std::abs(a-b)<1e-8;}
void Run(){
    PreviewAnimationClock clock;Check(clock.At(10,7)==7,"Empty clock lost its caller fallback");
    clock.Reset(0,7);clock.Record(2,9);clock.Record(5,9);clock.Record(8,12);
    Check(Near(clock.At(1),8)&&Near(clock.At(3.5),9)&&Near(clock.At(6),10),
        "Animation hold time was incorrectly advanced with settling physics");
    // The target is far ahead. Pause is asynchronous: one final two-tick job
    // finishes after the first completed-clock read. Keep the original mapping
    // until that zero-time resume publication acknowledges its final clock.
    clock.Reset(0,4);clock.Record(10,14);
    const double initialRead=2,finalRead=initialRead+2./60;
    Check(Near(clock.At(initialRead),6),"Pause used the uncompleted target animation time");
    const double resumed=clock.Rebase(finalRead);
    Check(Near(resumed,4+finalRead)&&clock.Size()==1&&Near(clock.At(100),resumed),
        "The final two-tick pause race retained queued animation time");
    clock.Record(finalRead+1./60,resumed+1./60);
    Check(Near(clock.At(finalRead+1./120),resumed+1./120),"Resuming introduced a clock discontinuity");
    // Generations restart at a seek or loop transition; old physics seconds
    // must never select animation from the preceding generation.
    clock.Reset(0,.25);Check(clock.Size()==1&&clock.At(0)==.25,"Seek retained the previous generation");
    clock.Record(1,1.25);clock.Reset(0,0);Check(clock.At(0)==0&&clock.At(99)==0,"Loop reset retained a stale clock segment");
    // Long continuous input is compact even when physics has not caught up.
    clock.Reset(0,7);double seconds=0,animation=7;
    for(int i=0;i<100000;++i){seconds+=1./120;animation+=1./120;clock.Record(seconds,animation);}
    Check(clock.Size()<=3&&Near(clock.At(seconds*.5),7+seconds*.5),"Continuous pending input grew an unbounded frame history");
    // Playback/hold transitions remain exact, while consumed history is pruned.
    clock.Reset(0,0);seconds=0;animation=0;
    for(int i=0;i<10000;++i){seconds+=.125;if(i%4<2)animation+=.125;clock.Record(seconds,animation);clock.Consume(seconds);
        Check(clock.Size()<=2,"Completed clock transitions were retained indefinitely");}
    Check(Near(clock.At(seconds),animation),"Pruning discarded the completed animation time");
    clock.Clear();Check(clock.Size()==0&&clock.At(0,3)==3,"Clearing an outfit preview retained clock data");
}
}
int main(){try{Run();std::cout<<"Preview animation clock passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
