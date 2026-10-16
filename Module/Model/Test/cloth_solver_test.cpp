#include "Model/Public/cloth_asset.h"
#include "Model/Public/vmd_animation.h"
#include "../Private/cloth_math.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

namespace {
using namespace Model;
int failures=0;
bool Expect(bool condition,const char* message){std::printf("  %-86s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;}
bool Near(glm::dvec3 a,glm::dvec3 b,double tolerance=1e-7){return glm::length(a-b)<tolerance;}
bool Finite(glm::dvec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
struct Fixture {ClothAsset cloth;StaticModel mesh;SkeletalPose pose;};
struct ScopedEnvironment {
    const char* name;std::string previous;bool existed=false;
    ScopedEnvironment(const char* variable,const char* value):name(variable){
#ifdef _WIN32
        char* stored=nullptr;std::size_t length=0;if(_dupenv_s(&stored,&length,name)==0&&stored){existed=true;previous=stored;}std::free(stored);_putenv_s(name,value);
#else
        if(const auto* stored=std::getenv(name)){existed=true;previous=stored;}setenv(name,value,1);
#endif
    }
    ~ScopedEnvironment(){
#ifdef _WIN32
        _putenv_s(name,existed?previous.c_str():"");
#else
        if(existed)setenv(name,previous.c_str(),1);else unsetenv(name);
#endif
    }
};
Fixture Sheet(){
    Fixture f;f.cloth.id="hanging-sheet";f.cloth.unitsPerMeter=1;f.mesh.meshes.resize(1);f.mesh.instances.push_back({"sheet",0,glm::dmat4(1)});
    auto& mesh=f.mesh.meshes.front();mesh.name="sheet";
    constexpr int width=7,height=8;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const glm::dvec3 p((x-(width-1)*.5)*.1,1,y*.1);const auto index=std::uint32_t(f.cloth.particles.size());
        f.cloth.particles.push_back({p,y==0?0.0:1.0,y==0?0.0:1.2,0});StaticVertex vertex;vertex.position=p;vertex.normal={0,1,0};vertex.uv={float(x)/(width-1),float(y)/(height-1)};mesh.vertices.push_back(vertex);
        f.cloth.renderBindings.push_back({0,index,index});if(y)f.cloth.tethers.push_back({index,std::uint32_t(x),y*.1});}
    for(int y=0;y<height-1;++y)for(int x=0;x<width-1;++x){const std::uint32_t a=y*width+x,b=a+1,c=a+width,d=c+1;
        f.cloth.triangles.push_back({a,c,b});f.cloth.triangles.push_back({b,c,d});mesh.indices.insert(mesh.indices.end(),{a,c,b,b,c,d});}
    std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
    for(const auto& t:f.cloth.triangles)for(int e=0;e<3;++e){auto a=t[e],b=t[(e+1)%3];if(a>b)std::swap(a,b);edges.insert({a,b});}
    for(auto [a,b]:edges)f.cloth.stretch.push_back({a,b,glm::length(f.cloth.particles[a].restPosition-f.cloth.particles[b].restPosition)});
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const std::uint32_t a=y*width+x;if(y+2<height)f.cloth.bend.push_back({a,a+2*width,.2});if(x+2<width)f.cloth.bend.push_back({a,a+2,.2});}
    f.cloth.settings.collision=false;return f;
}
Fixture Point(glm::dvec3 p){Fixture f;f.cloth.id="point";f.mesh.meshes.resize(1);StaticVertex v;v.position=p;f.mesh.meshes[0].vertices.push_back(v);f.cloth.particles.push_back({p,1,2,0});f.cloth.renderBindings.push_back({0,0,0});f.cloth.settings.gravity={0,0,0};return f;}
bool Advance(ClothSimulator& sim,const Fixture& f,int frames,double dt,std::string& error){for(int i=0;i<frames;++i)if(!sim.Step(f.cloth,f.pose,f.mesh,dt,f.cloth.settings,&error)){std::printf("    %s\n",error.c_str());return false;}return true;}
void AngularMath(){
    std::puts("Bounded-error double angular math:");double maximumError=0;bool ok=true;std::uint64_t samples=0;
    auto compare=[&](double y,double x){const double expected=std::atan2(y,x),actual=detail::FastAtan2(y,x);++samples;
        if(std::isnan(expected)){ok&=std::isnan(actual);return;}
        ok&=std::isfinite(actual);maximumError=std::max(maximumError,std::abs(actual-expected));
        if(expected==0)ok&=actual==0&&std::signbit(actual)==std::signbit(expected);};
    const double values[]={0.,-0.,1.,-1.,std::numeric_limits<double>::denorm_min(),-std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::min(),-std::numeric_limits<double>::min(),std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()};
    for(double y:values)for(double x:values)compare(y,x);
    bool exactAxes=true;
    for(double coordinate:values)if(std::isfinite(coordinate))for(double zero:{0.,-0.}){
        for(const auto pair:{std::array<double,2>{zero,coordinate},std::array<double,2>{coordinate,zero}}){const double expected=std::atan2(pair[0],pair[1]),actual=detail::FastAtan2(pair[0],pair[1]);exactAxes&=actual==expected&&std::signbit(actual)==std::signbit(expected);}}
    Expect(exactAxes,"All finite positive/negative axes and signed-zero combinations exactly match CRT atan2");
    for(const auto pair:{std::array<double,2>{-5.4448122243936965e-238,1.2965743990580182e86},std::array<double,2>{3.8805726078557626e-225,1.3024458624151072e99}}){
        const double expected=std::atan2(pair[0],pair[1]),actual=detail::FastAtan2(pair[0],pair[1]);
        Expect(actual==expected&&std::signbit(actual)==std::signbit(expected),"Extreme subnormal ratios preserve the CRT's exact result and signed-zero rounding");compare(pair[0],pair[1]);}
    constexpr double boundary=.414213562373095048801688724209698079;
    for(double ratio:{std::nextafter(boundary,0.),boundary,std::nextafter(boundary,1.)})for(double x:{-1.,1.})for(double y:{-ratio,ratio}){compare(y,x);compare(x,y);}
    std::uint64_t state=0xadc014942c761b3dull;
    auto random=[&](){state=state*6364136223846793005ull+1442695040888963407ull;return state;};
    auto sample=[&](){const auto bits=random();const double fraction=double(bits>>11)*0x1p-53;const int exponent=int((random()>>32)%2098)-1074;return std::copysign(std::ldexp(.5+fraction*.5,exponent),(bits>>63)?-1.:1.);};
    for(int i=0;i<200000;++i){const double y=sample(),x=sample();compare(y,x);}
    for(int i=0;i<=20000;++i){const double ratio=double(i)/20000;for(double x:{-1.,1.})for(double y:{-ratio,ratio}){compare(y,x);compare(x,y);}}
    Expect(ok&&maximumError<7e-11,"FastAtan2 stays below 7e-11 radians across quadrants, reduction boundaries and extreme doubles");
    std::printf("    %llu angle samples; maximum absolute error %.12g radians\n",static_cast<unsigned long long>(samples),maximumError);
}
void Convergence(){
    std::puts("Quality-driven iteration convergence:");std::string error;auto still=Sheet();still.cloth.settings.gravity={0,0,0};still.cloth.settings.wind={0,0,0};still.cloth.settings.iterations=16;
    auto full=still;full.cloth.settings.convergenceTolerance=0;ClothSimulator adaptive,complete;
    bool ok=Advance(adaptive,still,1,1.0/60,error)&&Advance(complete,full,1,1.0/60,error);double difference=0;
    for(std::size_t i=0;ok&&i<adaptive.Positions().size();++i)difference=std::max(difference,glm::length(adaptive.Positions()[i]-complete.Positions()[i]));
    const auto fullIterations=std::uint64_t(still.cloth.settings.substeps)*still.cloth.settings.iterations;
    Expect(ok&&adaptive.Stats().constraintIterations==still.cloth.settings.substeps*4&&adaptive.Stats().constraintIterations<fullIterations&&difference<1e-10,"A settled sheet stops after four iterations per substep without changing its rest shape");
    Expect(ok&&complete.Stats().constraintIterations==fullIterations,"Zero convergence tolerance always executes the complete iteration budget");
    ok=adaptive.Step(still.cloth,still.pose,still.mesh,0,still.cloth.settings,&error);
    Expect(ok&&adaptive.Stats().constraintIterations==0,"Per-Step iteration statistics report zero when no fixed tick executes");
    auto self=still;self.cloth.settings.selfCollision=true;ClothSimulator selfCollision;
    Expect(Advance(selfCollision,self,1,1.0/60,error)&&selfCollision.Stats().constraintIterations==fullIterations,"Self-collision disables early convergence even for an otherwise settled sheet");
    auto thin=Sheet();thin.cloth.settings.thickness=.0001;auto explicitThin=thin;explicitThin.cloth.settings.convergenceTolerance=.00001;ClothSimulator clampedThin,requestedThin;
    ok=Advance(clampedThin,thin,4,1.0/60,error)&&Advance(requestedThin,explicitThin,4,1.0/60,error);difference=0;
    for(std::size_t i=0;ok&&i<clampedThin.Positions().size();++i)difference=std::max(difference,glm::length(clampedThin.Positions()[i]-requestedThin.Positions()[i]));
    Expect(ok&&difference<1e-10&&clampedThin.Stats().constraintIterations==requestedThin.Stats().constraintIterations,"Effective convergence tolerance is capped at one tenth of collision thickness");
    auto moving=Point({0,0,0});moving.cloth.settings.substeps=1;moving.cloth.capsules.push_back({"convergence-swept-thigh",0,0,{0,-.1,0},{0,.1,0},.08});moving.pose.global={glm::translate(glm::dmat4(1),glm::dvec3(-.3,0,0))};
    auto exact=moving;exact.cloth.settings.convergenceTolerance=0;ClothSimulator fastContact,fullContact;
    ok=fastContact.Step(moving.cloth,moving.pose,moving.mesh,0,moving.cloth.settings,&error)&&fullContact.Step(exact.cloth,exact.pose,exact.mesh,0,exact.cloth.settings,&error);
    moving.pose.global[0]=exact.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(.3,0,0));
    ok=ok&&Advance(fastContact,moving,1,1.0/60,error)&&Advance(fullContact,exact,1,1.0/60,error);
    Expect(ok&&fastContact.Stats().contacts==1&&fastContact.Positions()[0].x>.38&&fastContact.Stats().maxPenetration<1e-7&&Near(fastContact.Positions()[0],fullContact.Positions()[0],1e-8),"Adaptive convergence preserves fast swept collision and agrees with the full-budget contact result");
}
void HangingAndWind(){
    std::puts("XPBD suspension, fixed stepping and SI units:");std::string error;const auto f=Sheet();ClothSimulator sim;
    Expect(sim.Reset(f.cloth,f.mesh,&error)&&Advance(sim,f,90,1.0/60,error),"A suspended cloth sheet runs 1.5 seconds without nonfinite positions");
    double minY=1,maxPinError=0,maxLengthError=0;
    for(std::size_t i=0;i<sim.Positions().size();++i){minY=std::min(minY,sim.Positions()[i].y);if(f.cloth.particles[i].inverseMass==0)maxPinError=std::max(maxPinError,glm::length(sim.Positions()[i]-f.cloth.particles[i].restPosition));}
    for(const auto& c:f.cloth.stretch)maxLengthError=std::max(maxLengthError,std::abs(glm::length(sim.Positions()[c.a]-sim.Positions()[c.b])-c.restLength));
    Expect(minY<.85,"Gravity bends a horizontally suspended sheet below its animated rest plane");
    Expect(maxPinError<1e-10&&sim.Stats().pinned==7,"The waist attachment row remains exactly on its animated targets");
    Expect(maxLengthError<.015&&sim.Stats().maxStretchError<.15,"XPBD stretch constraints preserve the sheet edge lengths under gravity");
    Expect(sim.Stats().maxDynamicEdgeError<.015&&sim.Stats().dynamicEdgeError95<=sim.Stats().maxDynamicEdgeError&&sim.Stats().maxKinematicStretchError<1e-10,"Diagnostics separate dynamic edge error in source units from fixed attachment strain");
    Expect(sim.Stats().steps==90&&sim.Stats().resets==1,"Stats count fixed ticks and preserve state instead of resetting every frame");
    bool tetherOk=true;for(const auto& tether:f.cloth.tethers)tetherOk&=glm::length(sim.Positions()[tether.particle]-sim.Positions()[tether.anchor])<=tether.restLength+.001;
    Expect(tetherOk,"Geodesic anchor tethers limit excessive extension");
    ClothSimulator a,b;bool ok=Advance(a,f,60,1.0/60,error)&&Advance(b,f,120,1.0/120,error);double difference=0;
    for(std::size_t i=0;ok&&i<a.Positions().size();++i)difference=std::max(difference,glm::length(a.Positions()[i]-b.Positions()[i]));
    Expect(ok&&a.Stats().steps==b.Stats().steps&&difference<1e-10,"60 Hz and 120 Hz submissions produce the same fixed-step result");
    const auto before=a.Positions();const auto steps=a.Stats().steps;
    Expect(a.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error)&&a.Positions()==before&&a.Stats().steps==steps,"A paused frame does not advance dynamics");
    auto scaled=f;scaled.cloth.id="centimeter-sheet";scaled.cloth.unitsPerMeter=100;
    for(auto& p:scaled.cloth.particles){p.restPosition*=100;p.maxDistance*=100;}
    for(auto* constraints:{&scaled.cloth.stretch,&scaled.cloth.bend})for(auto& c:*constraints)c.restLength*=100;
    for(auto& t:scaled.cloth.tethers)t.restLength*=100;for(auto& vertex:scaled.mesh.meshes[0].vertices)vertex.position*=100;
    ClothSimulator centimeters;ok=Advance(centimeters,scaled,60,1.0/60,error);difference=0;
    for(std::size_t i=0;ok&&i<a.Positions().size();++i)difference=std::max(difference,glm::length(centimeters.Positions()[i]/100.0-a.Positions()[i]));
    Expect(ok&&difference<1e-7,"Source centimeters and meters simulate identically in internal SI units");
    Expect(ok&&std::abs(centimeters.Stats().maxDynamicEdgeError/100.0-a.Stats().maxDynamicEdgeError)<1e-7&&std::abs(centimeters.Stats().dynamicEdgeError95/100.0-a.Stats().dynamicEdgeError95)<1e-7,"Absolute dynamic max and p95 errors scale with source units");
    auto windy=f;windy.cloth.settings.gravity={0,0,0};windy.cloth.settings.wind={0,6,0};ClothSimulator wind,calm;
    auto still=windy;still.cloth.settings.wind={0,0,0};ok=Advance(wind,windy,60,1.0/60,error)&&Advance(calm,still,60,1.0/60,error);
    double windMotion=0,calmMotion=0;for(std::size_t i=0;ok&&i<wind.Positions().size();++i){windMotion=std::max(windMotion,glm::length(wind.Positions()[i]-f.cloth.particles[i].restPosition));calmMotion=std::max(calmMotion,glm::length(calm.Positions()[i]-f.cloth.particles[i].restPosition));}
    Expect(ok&&windMotion>.02&&calmMotion<1e-7,"Wind pressure moves the sheet while a zero-gravity calm sheet stays at rest");
    std::printf("    lowest y %.5f m, max edge error %.6f m, wind deflection %.5f m\n",minY,maxLengthError,windMotion);
}
void Contacts(){
    std::puts("Capsule contacts and spatial self collision:");std::string error;auto f=Point({.05,0,0});f.cloth.capsules.push_back({"sphere",-1,-1,{0,0,0},{0,0,0},.2});ClothSimulator sim;
    bool ok=Advance(sim,f,1,1.0/60,error);Expect(ok&&sim.Positions()[0].x>=.208-1e-8&&sim.Stats().contacts>0&&sim.Stats().maxPenetration<1e-7,"Capsule contact projects particles outside body radius plus cloth thickness");
    Expect(sim.Stats().infeasibleAnimatedTargets==0&&sim.Stats().kinematicColliderOverlaps==0,"A free particle with enough MaxDistance is not reported as an infeasible target");
    auto impossible=f;impossible.cloth.particles[0].maxDistance=.02;ClothSimulator conflict;
    ok=Advance(conflict,impossible,1,1.0/60,error);Expect(ok&&conflict.Stats().infeasibleAnimatedTargets==1&&conflict.Stats().kinematicColliderOverlaps==0,"A target inside a body capsule farther than MaxDistance is reported once");
    impossible.cloth.particles[0].inverseMass=0;impossible.cloth.particles[0].maxDistance=0;ClothSimulator attached;
    ok=attached.Step(impossible.cloth,impossible.pose,impossible.mesh,0,impossible.cloth.settings,&error);
    Expect(ok&&attached.Stats().kinematicColliderOverlaps==1&&attached.Stats().infeasibleAnimatedTargets==0&&Near(attached.Positions()[0],{.05,0,0}),"Paused inspection reports kinematic body overlap without moving authored fixed points");
    f=Point({0,0,0});f.cloth.capsules.push_back({"moving-thigh",0,0,{0,-.1,0},{0,.1,0},.08});f.pose.global={glm::translate(glm::dmat4(1),glm::dvec3(-.3,0,0))};f.cloth.settings.substeps=1;
    ClothSimulator sweep;ok=sweep.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error);f.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(.3,0,0));ok=ok&&sweep.Step(f.cloth,f.pose,f.mesh,1.0/60,f.cloth.settings,&error);
    Expect(ok&&sweep.Positions()[0].x>.38&&sweep.Stats().contacts==1,"Swept capsule contact catches a fast leg passing completely across a particle");
    auto caps=EvaluateClothCapsules(f.cloth,f.pose);Expect(caps.size()==1&&Near(caps[0].a,{.3,-.1,0})&&Near(caps[0].b,{.3,.1,0}),"Capsule endpoints follow their animated skeleton bone transforms");
    f.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(-.3,0,0));ClothSimulator covered;ok=covered.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error);
    f.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(.03,0,0));ok=ok&&covered.Step(f.cloth,f.pose,f.mesh,1.0/60,f.cloth.settings,&error);
    Expect(ok&&covered.Positions()[0].x>=.118-1e-7&&covered.Stats().maxPenetration<1e-7,"Sweep preserves the entry side when a moving capsule ends covering the particle");
    f.pose.global.clear();Expect(!sweep.Step(f.cloth,f.pose,f.mesh,1.0/60,f.cloth.settings,&error),"A missing animated collider bone reports an error before solving");
    auto overlap=Point({-.002,0,0});overlap.cloth.id="two-points";overlap.cloth.particles.push_back({{.002,0,0},1,2,0});auto v=overlap.mesh.meshes[0].vertices[0];v.position={.002,0,0};overlap.mesh.meshes[0].vertices.push_back(v);overlap.cloth.renderBindings.push_back({0,1,1});overlap.cloth.settings.collision=false;overlap.cloth.settings.selfCollision=true;ClothSimulator self;
    ok=Advance(self,overlap,1,1.0/60,error);Expect(ok&&glm::length(self.Positions()[0]-self.Positions()[1])>=.016-1e-9&&self.Stats().selfContacts==1,"Spatial hash self collision separates nonadjacent simulation particles");
    overlap.cloth.stretch.push_back({0,1,.004});ClothSimulator neighbors;ok=Advance(neighbors,overlap,1,1.0/60,error);
    Expect(ok&&neighbors.Stats().selfContacts==0&&glm::length(neighbors.Positions()[0]-neighbors.Positions()[1])<.005,"Topological neighbors are excluded from self collision");
}
void InertiaSurfaceAndBending(){
    std::puts("Root-space inertia, membrane area and continuous surface contact:");std::string error;
    auto f=Sheet();f.cloth.id="pelvis-space-sheet";f.cloth.motionBone=0;f.cloth.motionInverseBind=glm::dmat4(1);f.pose.global={glm::dmat4(1)};
    for(std::size_t i=0;i<f.cloth.particles.size();++i){auto& p=f.cloth.particles[i];p.restPosition={p.restPosition.x,1-p.restPosition.z,0};f.mesh.meshes[0].vertices[i].position=p.restPosition;}
    f.cloth.settings.damping=.4;f.cloth.settings.localDamping=.7;f.cloth.settings.bendCompliance=100;
    ClothSimulator sim,misleadingBones;bool ok=sim.Reset(f.cloth,f.pose,f.mesh,&error)&&misleadingBones.Reset(f.cloth,f.pose,f.mesh,&error);double peakLag=0,ignoredSkinError=0,stoppedY=0,finalY=0,lateOscillation=0;
    const std::size_t hem=f.cloth.particles.size()-4;
    // Move normal to the sheet: an in-plane horizontal pull tests membrane
    // shear stiffness and cannot demonstrate an out-of-plane pendulum swing.
    for(int frame=0;frame<240&&ok;++frame){const double shift=.35*std::min(1.0,frame/18.0);f.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(0,0,shift));auto skinned=f.mesh;
        for(std::size_t i=0;i<skinned.meshes[0].vertices.size();++i)skinned.meshes[0].vertices[i].position=f.cloth.particles[i].restPosition+glm::dvec3(0,0,shift);
        auto distorted=skinned;for(std::size_t i=0;i<f.cloth.particles.size();++i)if(f.cloth.particles[i].inverseMass>0)distorted.meshes[0].vertices[i].position+=glm::dvec3(.2*std::sin(frame*.2),.15*std::cos(frame*.17),.3*std::sin(frame*.3));
        ok=sim.Step(f.cloth,f.pose,skinned,1.0/60,f.cloth.settings,&error)&&misleadingBones.Step(f.cloth,f.pose,distorted,1.0/60,f.cloth.settings,&error);
        if(frame<19)peakLag=std::max(peakLag,f.cloth.particles[hem].restPosition.z+shift-sim.Positions()[hem].z);
        for(std::size_t i=0;i<f.cloth.particles.size();++i)ignoredSkinError=std::max(ignoredSkinError,glm::length(sim.Positions()[i]-misleadingBones.Positions()[i]));
        if(frame==18)stoppedY=sim.Positions()[hem].y;if(frame==239)finalY=sim.Positions()[hem].y;
        if(frame>=210)lateOscillation=std::max(lateOscillation,std::abs(sim.Positions()[hem].z-(f.cloth.particles[hem].restPosition.z+.35)));
    }
    Expect(ok&&peakLag>.03,"A moving waist drags the hem with measurable inertial lag rather than rigid translation");
    Expect(ok&&ignoredSkinError<1e-9,"Animated thigh/skirt skinning cannot move the free cloth reference frame");
    Expect(ok&&finalY<stoppedY-.005&&lateOscillation<peakLag*.5,"After the waist stops, the hem drops and the late oscillation amplitude decays by more than half");
    Expect(ok&&sim.Stats().resets==1&&sim.Stats().collapsedTriangles==0,"Continuous movement preserves solver history and noncollapsed membrane triangles");
    std::printf("    inertial lag %.5fm, stop/drop y %.5f -> %.5f, late oscillation %.5fm, final z error %.5fm, ignored skin error %.3g\n",peakLag,stoppedY,finalY,lateOscillation,std::abs(sim.Positions()[hem].z-(f.cloth.particles[hem].restPosition.z+.35)),ignoredSkinError);
    Fixture face;face.cloth.id="capsule-inside-face";face.mesh.meshes.resize(1);face.cloth.settings.gravity={0,0,0};face.cloth.settings.substeps=1;face.cloth.settings.iterations=16;
    for(const auto p:{glm::dvec3(-.5,-.4,0),glm::dvec3(.5,-.4,0),glm::dvec3(0,.5,0)}){const auto i=std::uint32_t(face.cloth.particles.size());face.cloth.particles.push_back({p,1,2,0});face.cloth.renderBindings.push_back({0,i,i});StaticVertex v;v.position=p;face.mesh.meshes[0].vertices.push_back(v);}
    face.cloth.triangles.push_back({0,1,2});face.mesh.meshes[0].indices={0,1,2};face.cloth.capsules.push_back({"face-interior",-1,-1,{0,0,0},{0,0,0},.15});ClothSimulator interior;
    bool verticesOutside=true;for(const auto& p:face.cloth.particles)verticesOutside&=glm::length(p.restPosition)>.158;
    ok=Advance(interior,face,1,1.0/60,error);
    Expect(ok&&verticesOutside&&interior.Stats().contacts>0&&interior.Stats().maxSurfacePenetration<1e-6,"An entire triangle is kept outside a sphere even when every vertex starts outside it");
    auto corner=face;corner.cloth.id="rounded-aabb-edge-contact";const glm::dvec3 cornerVertices[3]={{.07,.13,-.2},{.13,.07,.2},{.2,.2,0}};
    verticesOutside=true;for(int i=0;i<3;++i){corner.cloth.particles[i].restPosition=cornerVertices[i];corner.mesh.meshes[0].vertices[i].position=cornerVertices[i];verticesOutside&=glm::length(cornerVertices[i])>.158;}
    ClothSimulator cornerContact;ok=Advance(cornerContact,corner,1,1.0/60,error);
    Expect(ok&&verticesOutside&&cornerContact.Stats().contacts>0&&cornerContact.Stats().maxSurfacePenetration<1e-6,"Conservative rounded-AABB rejection retains edge contact near a capsule's diagonal corner");
    auto swept=face;swept.cloth.capsules[0].boneA=0;swept.cloth.capsules[0].boneB=0;swept.pose.global={glm::translate(glm::dmat4(1),glm::dvec3(0,0,-.4))};ClothSimulator moving;
    ok=moving.Step(swept.cloth,swept.pose,swept.mesh,0,swept.cloth.settings,&error);swept.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(0,0,.4));ok=ok&&moving.Step(swept.cloth,swept.pose,swept.mesh,1.0/60,swept.cloth.settings,&error);
    double centroidZ=0;for(const auto& p:moving.Positions())centroidZ+=p.z/3;
    Expect(ok&&moving.Stats().contacts>0&&centroidZ>.2&&moving.Stats().maxSurfacePenetration<1e-5,"Surface CCD catches a fast capsule crossing the middle of a coarse triangle");
    auto startup=Sheet();startup.cloth.id="posed-startup-wrap";startup.pose.global={glm::translate(glm::dmat4(1),glm::dvec3(0,.03,0))};
    ClothCapsule rising{"initial-raised-limb",0,0,{0,1,.35},{0,1,.35},.15};rising.referenceValid=true;rising.restA=rising.restB={0,.6,.35};startup.cloth.capsules.push_back(rising);startup.cloth.settings.collision=true;
    ClothSimulator prepared;ok=prepared.Reset(startup.cloth,startup.pose,startup.mesh,&error)&&prepared.Step(startup.cloth,startup.pose,startup.mesh,0,startup.cloth.settings,&error);
    Expect(ok&&prepared.Stats().steps==0&&prepared.Stats().resets==1&&prepared.Stats().maxSurfacePenetration<.005,"A deeply posed startup wraps the neutral cloth around a rising limb without advancing animation time");
    Expect(ok&&prepared.Stats().maxDynamicEdgeError<.025&&prepared.Stats().collapsedTriangles==0,"Neutral-to-posed warmup avoids long contact needles and collapsed startup triangles");
    ok=ok&&prepared.Step(startup.cloth,startup.pose,startup.mesh,1.0/60,startup.cloth.settings,&error);
    Expect(ok&&prepared.Stats().steps==1&&prepared.Stats().resets==1,"The first real frame counts one fixed tick after hidden initialization");
    auto hinge=Sheet();hinge.cloth.id="signed-hinge-recovery";hinge.cloth.settings.gravity={0,0,0};hinge.cloth.settings.bendCompliance=1e-7;hinge.cloth.settings.damping=2;hinge.cloth.settings.iterations=16;
    auto folded=hinge.mesh;for(std::size_t i=0;i<folded.meshes[0].vertices.size();++i){auto& p=folded.meshes[0].vertices[i].position;if(p.z>.3){const double z=p.z-.3;p.y+=z*std::sin(1.2);p.z=.3+z*std::cos(1.2);}}
    ClothSimulator unfold;ok=unfold.Reset(hinge.cloth,folded,&error)&&unfold.Step(hinge.cloth,hinge.pose,folded,0,hinge.cloth.settings,&error);const double initialBend=unfold.Stats().maxBendDeviation;
    ok=ok&&Advance(unfold,hinge,150,1.0/60,error);
    Expect(ok&&initialBend>.5&&unfold.Stats().maxBendDeviation<initialBend*.4,"Signed dihedral constraints recover a folded rest hinge rather than preserving a false distance fold");
}
void SurfaceManifolds(){
    std::puts("Cached surface manifolds, sliding and newly entering features:");
    auto triangle=[](const std::array<glm::dvec3,3>& points){Fixture f;f.cloth.id="surface-manifold-regression";f.mesh.meshes.resize(1);
        f.cloth.settings.gravity={0,0,0};f.cloth.settings.wind={0,0,0};f.cloth.settings.substeps=1;f.cloth.settings.iterations=16;f.cloth.settings.convergenceTolerance=0;f.cloth.settings.areaCompliance=1e4;
        for(const auto& p:points){const auto i=std::uint32_t(f.cloth.particles.size());f.cloth.particles.push_back({p,1,2,0});f.cloth.renderBindings.push_back({0,i,i});StaticVertex v;v.position=p;f.mesh.meshes[0].vertices.push_back(v);}
        f.cloth.triangles.push_back({0,1,2});f.mesh.meshes[0].indices={0,1,2};return f;};
    auto sliding=triangle({glm::dvec3(-.4,-.3,.085),glm::dvec3(.4,-.3,.085),glm::dvec3(0,.4,.085)});
    sliding.cloth.capsules.push_back({"sliding-axis-endpoints",0,0,{-.12,0,0},{.12,0,0},.08});sliding.pose.global={glm::dmat4(1)};
    for(auto& p:sliding.cloth.particles)p.animationDrive=.5;
    struct Result {bool ok=true;double penetration=0;std::size_t collapsed=0,contactFrames=0;std::vector<glm::dvec3> positions;};
    auto run=[&](Fixture fixture,bool cache,int frames,bool move,bool requestCache=true){ScopedEnvironment enabled("MODEL_CLOTH_CONTACT_CACHE",requestCache?"1":"0"),full("MODEL_CLOTH_FULL_NARROWPHASE",cache?"0":"1");ClothSimulator simulator;std::string error;Result result;
        for(int frame=0;frame<frames&&result.ok;++frame){if(move)fixture.pose.global[0]=glm::translate(glm::dmat4(1),glm::dvec3(.2*std::sin(frame*.06),.15*std::sin(frame*.11),.03*std::sin(frame*.08)));
            result.ok=simulator.Step(fixture.cloth,fixture.pose,fixture.mesh,1.0/60,fixture.cloth.settings,&error);
            result.penetration=std::max(result.penetration,simulator.Stats().maxSurfacePenetration);result.collapsed=std::max(result.collapsed,simulator.Stats().collapsedTriangles);result.contactFrames+=simulator.Stats().contacts>0;}
        result.positions=simulator.Positions();if(!result.ok)std::printf("    %s\n",error.c_str());return result;};
    const auto fullSliding=run(sliding,false,120,true),cachedSliding=run(sliding,true,120,true);
    Expect(fullSliding.ok&&cachedSliding.ok&&fullSliding.contactFrames>20&&cachedSliding.contactFrames>20&&fullSliding.collapsed==0&&cachedSliding.collapsed==0&&fullSliding.penetration<.001&&cachedSliding.penetration<.001&&cachedSliding.penetration<=fullSliding.penetration+.0005,
        "Cached contacts follow a capsule sliding across a face and its endpoints without lost contact or collapse");
    // The first sphere initially sees the A feature outside R=.158. A later
    // sphere pushes the B-C edge inward by less than thickness/2; A stays
    // outside, and every vertex stays outside the first sphere. Reusing A's
    // nonpenetrating gap must therefore trigger a complete feature query.
    auto entering=triangle({glm::dvec3(.1582,0,.010),glm::dvec3(.159,.030,0),glm::dvec3(.159,-.030,0)});
    entering.cloth.capsules.push_back({"first-sphere",-1,-1,{0,0,0},{0,0,0},.15});entering.cloth.capsules.push_back({"edge-pusher",-1,-1,{.169,0,-.02},{.169,0,-.02},.017});
    bool verticesOutside=true;for(const auto& p:entering.cloth.particles)verticesOutside&=glm::length(p.restPosition)>.158&&glm::length(p.restPosition-glm::dvec3(.169,0,-.02))>.025;
    const auto fullEntering=run(entering,false,1,false),cachedEntering=run(entering,true,1,false);
    Expect(verticesOutside&&fullEntering.ok&&cachedEntering.ok&&fullEntering.contactFrames==1&&cachedEntering.contactFrames==1&&fullEntering.collapsed==0&&cachedEntering.collapsed==0&&fullEntering.penetration<.001&&cachedEntering.penetration<.001&&cachedEntering.penetration<=fullEntering.penetration+.0005,
        "A second capsule creates a new edge-interior contact even though the old barypoint and all vertices stay outside");
    // The override is also an exact A/B control, even when CACHE remains on.
    auto reference=sliding;reference.cloth.settings.convergenceTolerance=.00025;const auto repeatedFull=run(reference,false,4,true),againFull=run(reference,false,4,true,false);
    bool identical=repeatedFull.ok&&againFull.ok&&repeatedFull.positions.size()==againFull.positions.size();for(std::size_t i=0;identical&&i<repeatedFull.positions.size();++i)identical&=repeatedFull.positions[i]==againFull.positions[i];
    Expect(identical,"The full narrowphase override remains deterministic with contact caching requested");
    std::printf("    sliding penetration full/cache %.6g / %.6gm, new edge %.6g / %.6gm\n",fullSliding.penetration,cachedSliding.penetration,fullEntering.penetration,cachedEntering.penetration);
}
void BindingAndGuards(){
    std::puts("Render binding, discontinuities and validation:");std::string error;auto f=Sheet();f.cloth.settings.gravity={0,0,0};f.cloth.settings.wind={0,4,0};
    const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(4,2,-1))*glm::rotate(glm::dmat4(1),.4,glm::dvec3(0,1,0))*glm::scale(glm::dmat4(1),glm::dvec3(2,3,1));f.mesh.instances[0].transform=transform;
    for(auto& p:f.cloth.particles)p.restPosition=glm::dvec3(transform*glm::dvec4(p.restPosition,1));
    for(auto* constraints:{&f.cloth.stretch,&f.cloth.bend})for(auto& c:*constraints)c.restLength=glm::length(f.cloth.particles[c.a].restPosition-f.cloth.particles[c.b].restPosition);
    for(auto& t:f.cloth.tethers)t.restLength=glm::length(f.cloth.particles[t.particle].restPosition-f.cloth.particles[t.anchor].restPosition);
    const auto indices=f.mesh.meshes[0].indices;std::vector<glm::vec2> uv;for(const auto& v:f.mesh.meshes[0].vertices)uv.push_back(v.uv);ClothSimulator sim;
    bool ok=Advance(sim,f,20,1.0/60,error);auto rendered=f.mesh;ok=ok&&sim.Apply(f.cloth,rendered,&error);double modelSpaceError=0;bool normalOk=true;
    for(std::size_t i=0;ok&&i<sim.Positions().size();++i){const auto& v=rendered.meshes[0].vertices[i];modelSpaceError=std::max(modelSpaceError,glm::length(glm::dvec3(transform*glm::dvec4(v.position,1))-sim.Positions()[i]));normalOk&=std::abs(glm::length(v.normal)-1)<1e-5&&std::abs(glm::dot(v.normal,glm::vec3(v.tangent)))<1e-5&&v.uv==uv[i];}
    Expect(ok&&modelSpaceError<1e-8,"Welded simulation positions map back through a nonuniform mesh instance transform");
    Expect(ok&&normalOk&&rendered.bounds.valid&&rendered.meshes[0].indices==indices,"Apply rebuilds finite orthonormal shading and bounds while preserving indices and UVs");
    auto seams=Sheet();auto& seamMesh=seams.mesh.meshes[0];const auto copied=std::uint32_t(seamMesh.vertices.size());seamMesh.vertices.push_back(seamMesh.vertices[8]);seams.cloth.renderBindings.push_back({0,copied,8});
    for(std::size_t index=3;index<seamMesh.indices.size();++index)if(seamMesh.indices[index]==8){seamMesh.indices[index]=copied;break;}
    seamMesh.vertices[copied].uv={.9f,.1f};ClothSimulator welded;ok=Advance(welded,seams,30,1.0/60,error)&&welded.Apply(seams.cloth,seams.mesh,&error);
    Expect(ok&&glm::length(seamMesh.vertices[8].normal-seamMesh.vertices[copied].normal)<1e-6&&seamMesh.vertices[copied].uv==glm::vec2(.9f,.1f),"Welded simulation normals remain identical across independent UV seam render vertices");
    auto moved=f.mesh;for(auto& vertex:moved.meshes[0].vertices)vertex.position.x+=.01;
    const auto previous=sim.Stats().steps;ok=sim.Step(f.cloth,f.pose,moved,0,f.cloth.settings,&error);double pinError=0;
    for(std::size_t i=0;i<f.cloth.particles.size();++i)if(f.cloth.particles[i].inverseMass==0)pinError=std::max(pinError,glm::length(sim.Positions()[i]-glm::dvec3(transform*glm::dvec4(moved.meshes[0].vertices[i].position,1))));
    Expect(ok&&pinError<1e-9&&sim.Stats().steps==previous,"Paused edits update kinematic waist anchors without adding simulation time");
    const auto resets=sim.Stats().resets;Expect(sim.Step(f.cloth,f.pose,f.mesh,2,f.cloth.settings,&error)&&sim.Stats().resets==resets+1&&sim.Stats().steps==0,"A long frame resets safely instead of running unbounded catch-up iterations");
    auto teleported=f.mesh;for(auto& vertex:teleported.meshes[0].vertices)vertex.position.x+=5;const auto reset2=sim.Stats().resets;
    Expect(sim.Step(f.cloth,f.pose,teleported,1.0/60,f.cloth.settings,&error)&&sim.Stats().resets==reset2+1,"A large animated teleport resets inertia to the new pose");
    Expect(sim.Reset(f.cloth,f.mesh,&error)&&sim.Stats().steps==0&&Near(sim.Positions()[0],f.cloth.particles[0].restPosition),"Explicit rewind reset restores bind targets and clears velocity");
    Expect(!sim.Step(f.cloth,f.pose,f.mesh,-.1,f.cloth.settings,&error)&&!sim.Step(f.cloth,f.pose,f.mesh,std::numeric_limits<double>::quiet_NaN(),f.cloth.settings,&error),"Negative and nonfinite frame times are rejected");
    auto invalid=f.cloth;invalid.renderBindings[0].vertex=1000000;Expect(!sim.Reset(invalid,f.mesh,&error),"Out-of-range render bindings are rejected");
    invalid=f.cloth;invalid.stretch[0].b=1000000;Expect(!sim.Reset(invalid,f.mesh,&error),"Out-of-range constraint endpoints are rejected");
    auto singular=f.mesh;singular.instances[0].transform=glm::dmat4(0);Expect(!sim.Reset(f.cloth,singular,&error),"Singular garment instance transforms are rejected");
    auto instanced=f.mesh;instanced.instances.push_back({"second-location",0,glm::dmat4(1)});Expect(!sim.Reset(f.cloth,instanced,&error),"Shared geometry at different instance transforms must be expanded before simulation");
    auto badSettings=f.cloth.settings;badSettings.substeps=0;Expect(!sim.Step(f.cloth,f.pose,f.mesh,.016,badSettings,&error),"Invalid solver iteration settings are rejected");
    badSettings=f.cloth.settings;badSettings.convergenceTolerance=-.001;const bool negativeToleranceRejected=!sim.Step(f.cloth,f.pose,f.mesh,.016,badSettings,&error);badSettings.convergenceTolerance=std::numeric_limits<double>::infinity();
    Expect(negativeToleranceRejected&&!sim.Step(f.cloth,f.pose,f.mesh,.016,badSettings,&error),"Negative and nonfinite convergence tolerances are rejected before solving");
    ClothSimulator movedSimulator=std::move(sim);Expect(movedSimulator.Positions().size()==f.cloth.particles.size()&&sim.Positions().empty(),"Solver ownership can move without dangling simulation positions");
}
void RealCharacter(){
    std::puts("Mahiro animated garment integration:");const std::filesystem::path root=MODEL_SOURCE_DIR;
    const auto package=root/"Asset/Generated/MahiroMaid/Package.asset.json",animation=root/"Asset/Generated/Animations/MahiroTaitui.anim.json";
    if(!std::filesystem::exists(package)||!std::filesystem::exists(animation)){std::puts("    Generated local Mahiro fixtures unavailable; portable solver tests completed.");return;}
    std::string error;auto character=LoadSkeletalAssetPackage(package,&error);if(!Expect(bool(character),"The real generated Mahiro skeletal package loads")){std::printf("%s\n",error.c_str());return;}
    auto clip=LoadAnimationSequenceAsset(animation,*character,&error);if(!Expect(bool(clip),"The imported real VMD sequence loads for Mahiro")){std::printf("%s\n",error.c_str());return;}
    auto modular=BuildSkirtGarment(*character,{},&error);if(!Expect(bool(modular),"The real model separates into a compatible body and skirt simulation asset")){std::printf("%s\n",error.c_str());return;}
    ClothSimulator sim,unobstructed;StaticModel skinned,deformed;bool ok=true;double maxPin=0,maxDisplacement=0,maxPenetration=0,maxSurfacePenetration=0,maxResolvableSurfacePenetration=0,maxKinematicColliderPenetration=0,maxFixedMismatch=0;std::size_t maxCollapsed=0;
    auto unconstrainedSettings=modular->cloth.settings;unconstrainedSettings.collision=false;
    struct StrainSummary {double animated=0,solved=0,noCollision=0,peak95=0,time=0,absolute=0,absolute95=0;std::size_t edge=0;double restMeters=0,animatedAtPeak=0,noCollisionAtPeak=0;};
    std::array<StrainSummary,3> strain;std::array<std::size_t,3> counts{};std::size_t maximumInfeasible=0,maximumKinematicOverlap=0;
    auto category=[&](const ClothDistanceConstraint& c){const auto pinned=[&](auto i){return modular->cloth.particles[i].inverseMass==0||modular->cloth.particles[i].maxDistance<=1e-12;};return pinned(c.a)&&pinned(c.b)?0:pinned(c.a)!=pinned(c.b)?1:2;};
    for(const auto& edge:modular->cloth.stretch)++counts[category(edge)];
    const int frameCount=int(std::ceil(clip->duration*60))+1;
    for(int frame=0;frame<frameCount;++frame){const double time=std::min(clip->duration,frame/60.0);auto pose=EvaluateSkeletalPose(*modular->garment,&*clip,time,false);
        if(!SkinSkeletalMesh(*modular->garment,pose,skinned,&error)||!sim.Step(modular->cloth,pose,skinned,1.0/60,modular->cloth.settings,&error)){ok=false;break;}
        if(!unobstructed.Step(modular->cloth,pose,skinned,1.0/60,unconstrainedSettings,&error)){ok=false;break;}
        deformed=skinned;if(!sim.Apply(modular->cloth,deformed,&error)){ok=false;break;}std::vector<glm::dvec3> target(modular->cloth.particles.size(),glm::dvec3(0));std::vector<std::uint32_t> counts(target.size(),0);
        for(const auto& binding:modular->cloth.renderBindings){glm::dmat4 transform(1);for(const auto& instance:skinned.instances)if(instance.meshIndex==binding.mesh){transform=instance.transform;break;}target[binding.particle]+=glm::dvec3(transform*glm::dvec4(skinned.meshes[binding.mesh].vertices[binding.vertex].position,1));++counts[binding.particle];}
        for(std::size_t i=0;i<target.size();++i){if(counts[i])target[i]/=double(counts[i]);if(modular->cloth.particles[i].inverseMass==0)maxPin=std::max(maxPin,glm::length(target[i]-sim.Positions()[i]));ok&=Finite(sim.Positions()[i]);}
        maxDisplacement=std::max(maxDisplacement,sim.Stats().maxDisplacement);maxPenetration=std::max(maxPenetration,sim.Stats().maxPenetration);
        maxSurfacePenetration=std::max(maxSurfacePenetration,sim.Stats().maxSurfacePenetration);maxCollapsed=std::max(maxCollapsed,sim.Stats().collapsedTriangles);
        maxResolvableSurfacePenetration=std::max(maxResolvableSurfacePenetration,sim.Stats().maxResolvableSurfacePenetration);
        maxKinematicColliderPenetration=std::max(maxKinematicColliderPenetration,sim.Stats().maxKinematicColliderPenetration);
        maximumInfeasible=std::max(maximumInfeasible,sim.Stats().infeasibleAnimatedTargets);maximumKinematicOverlap=std::max(maximumKinematicOverlap,sim.Stats().kinematicColliderOverlaps);
        std::array<std::vector<double>,3> strains,absoluteErrors;
        for(std::size_t i=0;i<modular->cloth.stretch.size();++i){const auto& edge=modular->cloth.stretch[i];const int type=category(edge);auto& summary=strain[type];
            const double rest=std::max(edge.restLength,1e-10);const double animated=std::abs(glm::length(target[edge.a]-target[edge.b])/rest-1),solved=std::abs(glm::length(sim.Positions()[edge.a]-sim.Positions()[edge.b])/rest-1),clear=std::abs(glm::length(unobstructed.Positions()[edge.a]-unobstructed.Positions()[edge.b])/rest-1);
            summary.animated=std::max(summary.animated,animated);summary.noCollision=std::max(summary.noCollision,clear);strains[type].push_back(solved);
            const double absolute=solved*rest/modular->cloth.unitsPerMeter;summary.absolute=std::max(summary.absolute,absolute);absoluteErrors[type].push_back(absolute);
            if(solved>summary.solved){summary.solved=solved;summary.time=time;summary.edge=i;summary.restMeters=rest/modular->cloth.unitsPerMeter;summary.animatedAtPeak=animated;summary.noCollisionAtPeak=clear;}
            if(type==0)maxFixedMismatch=std::max(maxFixedMismatch,std::abs(solved-animated));}
        for(int type=0;type<3;++type)if(!strains[type].empty()){auto& values=strains[type];std::sort(values.begin(),values.end());strain[type].peak95=std::max(strain[type].peak95,values[std::min(values.size()-1,std::size_t(values.size()*.95))]);auto& absolutes=absoluteErrors[type];std::sort(absolutes.begin(),absolutes.end());strain[type].absolute95=std::max(strain[type].absolute95,absolutes[std::min(absolutes.size()-1,std::size_t(absolutes.size()*.95))]);}
    }
    if(!ok)std::printf("    %s\n",error.c_str());
    Expect(ok&&sim.Stats().steps>=std::uint64_t(frameCount)&&sim.Stats().resets==1,"The full 9.2-second VMD and cloth simulation stay finite without repeated resets");
    Expect(ok&&sim.Stats().pinned>0&&maxPin<1e-5,"The real garment keeps its welded waist pins attached throughout the motion");
    Expect(ok&&maxDisplacement>1e-3&&maxDisplacement<modular->cloth.unitsPerMeter*3,"The simulated skirt differs from pure skinning while remaining inside useful bounds");
    Expect(ok&&maximumInfeasible==0,"The complete motion has no infeasible free-cloth MaxDistance targets");
    Expect(ok&&maximumKinematicOverlap>0&&maxKinematicColliderPenetration>0,"Authored embedded waist/body conflicts remain explicitly reported rather than hidden");
    Expect(ok&&maxPenetration<modular->cloth.unitsPerMeter*.005,"Real skirt free-particle body penetration remains below five millimeters");
    Expect(ok&&maxResolvableSurfacePenetration<modular->cloth.unitsPerMeter*.008,"Every resolvable skirt surface contact retains the original eight-millimeter error limit");
    Expect(ok&&maxSurfacePenetration<=maxResolvableSurfacePenetration+maxKinematicColliderPenetration+modular->cloth.unitsPerMeter*1e-6,"Raw surface penetration is bounded by the independently reported source seam conflict and solver residual");
    Expect(ok&&maxCollapsed==0,"The complete motion produces no membrane triangles below three percent of rest area");
    Expect(ok&&strain[1].absolute<.05&&strain[2].absolute<.05,"All edges with dynamic endpoints stay below five centimeters absolute length error");
    Expect(ok&&strain[1].absolute95<.02&&strain[2].absolute95<.02,"Mixed and fully dynamic edge error p95 stays below two centimeters throughout the motion");
    Expect(ok&&maxFixedMismatch<1e-5,"All-fixed edge strain exactly equals the source animation rather than solver error");
    std::printf("    %zu particles, %zu pins, %llu fixed ticks, peak displacement %.6g source units, penetration %.6g\n",sim.Stats().particles,sim.Stats().pinned,static_cast<unsigned long long>(sim.Stats().steps),maxDisplacement,maxPenetration);
    std::printf("    peak infeasible animation targets %zu; peak fixed collider overlaps %zu\n",maximumInfeasible,maximumKinematicOverlap);
    std::printf("    peak resolvable/raw full-surface penetration %.6f / %.6fm, source seam conflict %.6fm, collapsed triangles %zu\n",maxResolvableSurfacePenetration/modular->cloth.unitsPerMeter,maxSurfacePenetration/modular->cloth.unitsPerMeter,maxKinematicColliderPenetration/modular->cloth.unitsPerMeter,maxCollapsed);
    for(int type=0;type<3;++type){const auto& summary=strain[type];const auto& edge=modular->cloth.stretch[summary.edge];
        std::printf("    %s: %zu edges; peak strain animated %.6f / solved %.6f / collision-off %.6f; peak p95 %.6f; absolute max %.6fm / peak p95 %.6fm\n",type==0?"fixed-fixed":type==1?"fixed-dynamic":"dynamic-dynamic",counts[type],summary.animated,summary.solved,summary.noCollision,summary.peak95,summary.absolute,summary.absolute95);
        std::printf("      worst at %.6fs edge %u-%u rest %.6fm; simultaneous animation %.6f / collision-off %.6f; maxDistance %.6f / %.6fm\n",summary.time,edge.a,edge.b,summary.restMeters,summary.animatedAtPeak,summary.noCollisionAtPeak,modular->cloth.particles[edge.a].maxDistance/modular->cloth.unitsPerMeter,modular->cloth.particles[edge.b].maxDistance/modular->cloth.unitsPerMeter);}
}
}
int main(){AngularMath();Convergence();HangingAndWind();Contacts();InertiaSurfaceAndBending();SurfaceManifolds();BindingAndGuards();RealCharacter();std::printf("Cloth solver: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;}
