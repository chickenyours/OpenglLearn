#include "Model/Public/hair_asset.h"
#include "Model/Private/hair_body_collision.h"
#include "Model/Public/vmd_animation.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>

namespace {
using namespace Model;
int failures=0;
bool Expect(bool condition,const char* message){std::printf("  %-96s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;}
bool Near(glm::dmat4 a,glm::dmat4 b,double tolerance=1e-11){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>tolerance)return false;return true;}
bool Finite(glm::dvec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
struct ScopedHairCache {
    std::string previous,name;bool existed=false;
    explicit ScopedHairCache(const char* value,const char* variable="MODEL_HAIR_CONTACT_CACHE"):name(variable){
#ifdef _WIN32
        char* stored=nullptr;std::size_t length=0;if(_dupenv_s(&stored,&length,name.c_str())==0&&stored){existed=true;previous=stored;}std::free(stored);_putenv_s(name.c_str(),value);
#else
        if(const auto* stored=std::getenv(name.c_str())){existed=true;previous=stored;}setenv(name.c_str(),value,1);
#endif
    }
    ~ScopedHairCache(){
#ifdef _WIN32
        _putenv_s(name.c_str(),existed?previous.c_str():"");
#else
        if(existed)setenv(name.c_str(),previous.c_str(),1);else unsetenv(name.c_str());
#endif
    }
};
struct Fixture {SkeletalAssetPackage model;HairAsset hair;};
Fixture Make(double units=1,bool collision=true){
    Fixture f;f.model.id="hair-test-model";f.model.skeleton.id="hair-test-skeleton";
    auto bone=[&](const char* name,int parent,glm::dvec3 t){SkeletonBone b;b.name=name;b.parent=parent;b.restLocal.translation=t*units;f.model.skeleton.bones.push_back(b);};
    bone("Root",-1,{0,0,0});bone("Head",0,{0,1.1,0});bone("Hair1",1,{.04,0,.025});
    bone("Hair2",2,{0,-.2,0});bone("Hair3",3,{0,-.2,0});bone("Other",0,{.5,.7,.2});
    f.model.skeleton.bones[2].restLocal.rotation=glm::angleAxis(.3,glm::dvec3(0,-1,0));
    const auto globals=GlobalBoneTransforms(f.model.skeleton,RestPose(f.model.skeleton));
    for(std::size_t i=0;i<globals.size();++i)f.model.skeleton.bones[i].inverseBind=glm::inverse(globals[i]);
    f.model.geometry=std::make_shared<StaticModel>();f.model.geometry->meshes.resize(2);f.model.skins.resize(2);
    auto vertex=[&](std::uint32_t mesh,glm::dvec3 p,std::uint32_t joint){StaticVertex v;v.position=p*units;v.normal={0,0,1};
        f.model.geometry->meshes[mesh].vertices.push_back(v);VertexSkin skin;skin.joints={joint,0,0,0};skin.weights={1,0,0,0};f.model.skins[mesh].vertices.push_back(skin);};
    // A closed body, rather than a plane's unbounded back half-space, is
    // required to make the authored 10 mm reset embedding physically real.
    f.model.geometry->meshes[0].name="Body";
    for(const auto p:{glm::dvec3(-1,0,0),glm::dvec3(1,0,0),glm::dvec3(1,1.5,0),glm::dvec3(-1,1.5,0),
                     glm::dvec3(-1,0,-.3),glm::dvec3(1,0,-.3),glm::dvec3(1,1.5,-.3),glm::dvec3(-1,1.5,-.3)})vertex(0,p,0);
    auto& body=f.model.geometry->meshes[0];
    body.indices={0,1,2,0,2,3, 4,6,5,4,7,6, 0,3,7,0,7,4, 1,5,6,1,6,2, 0,4,5,0,5,1, 3,2,6,3,6,7};
    for(auto& v:body.vertices)v.normal=glm::vec3(glm::normalize(v.position/units-glm::dvec3(0,.75,-.15)));
    f.model.geometry->meshes[1].name="Hair";
    vertex(1,{.04,.95,-.01},2);vertex(1,{.04,.75,-.01},3);vertex(1,{.04,.55,-.01},4);
    f.model.geometry->meshes[1].indices={0,1,2};
    f.model.geometry->instances={{"Body",0,glm::dmat4(1)},{"Hair",1,glm::dmat4(1)}};
    f.hair.id="test-hair";f.hair.skeletonId=f.model.skeleton.id;f.hair.unitsPerMeter=units;f.hair.hairMeshes={1};f.hair.settings.collision=collision;
    HairChain chain;chain.name="strand";chain.anchorBone=1;
    for(std::uint32_t b=2;b<=4;++b){HairBoneLink l;l.bone=b;l.restTip={0,-.2*units,0};l.maxAngle=1.1;l.stiffness=10;l.damping=7;chain.links.push_back(l);}
    f.hair.chains.push_back(chain);
    for(std::uint32_t v=0;v<3;++v){const auto b=v+2;HairSurfaceProbe p;p.chain=0;p.mesh=1;p.vertex=v;
        p.influences.push_back({b,1,glm::dvec3(f.model.skeleton.bones[b].inverseBind*glm::dvec4(f.model.geometry->meshes[1].vertices[v].position,1))});f.hair.probes.push_back(p);}
    MorphTarget morph;morph.name="unchanged expression";f.model.morphs.push_back(morph);
    return f;
}
glm::dvec3 Probe(const HairSurfaceProbe& p,const SkeletalPose& pose,double units){glm::dvec3 result(0);for(const auto& i:p.influences)
    result+=glm::dvec3(pose.global[i.bone]*glm::dvec4(i.weightedPosition,i.weight));return result/units;}
void InertiaAndDeterminism(){
    std::puts("Fixed clock, inertia, exact roots and pose coupling:");auto f=Make(1,false);
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);pose.morphWeights={.37f};HairSimulator a,b;std::string error;
    bool okay=a.Reset(f.hair,f.model,pose,&error)&&b.Reset(f.hair,f.model,pose,&error);
    SkeletalPose outA,outB;double maxLag=0,lateVelocity=0;bool finite=true,exactOther=true,exactScale=true,consistent=true,deterministic=true;
    glm::dvec3 oldTip(0);
    for(int tick=0;okay&&tick<600;++tick){auto animated=pose;
        const double x=tick<120?.10*std::sin(tick*.06):0;animated.local[1].translation.x=x;
        animated.local[1].rotation=glm::angleAxis(tick<120?.22*std::sin(tick*.04):0.,glm::dvec3(0,0,1));
        animated.global=GlobalBoneTransforms(f.model.skeleton,animated.local);
        okay=a.Step(f.hair,f.model,animated,1./60.,f.hair.settings,outA,&error)&&b.Step(f.hair,f.model,animated,1./60.,f.hair.settings,outB,&error);
        for(const auto& p:a.Positions())finite&=Finite(p);
        for(const auto bone:{0u,1u,5u})exactOther&=outA.global[bone]==animated.global[bone]&&outA.local[bone].translation==animated.local[bone].translation&&outA.local[bone].rotation==animated.local[bone].rotation;
        exactOther&=outA.morphWeights==animated.morphWeights;
        for(std::uint32_t bone=2;bone<=4;++bone){exactScale&=outA.local[bone].scale==animated.local[bone].scale&&outA.local[bone].translation==animated.local[bone].translation;
            consistent&=Near(outA.global[bone],outA.global[f.model.skeleton.bones[bone].parent]*TransformMatrix(outA.local[bone]));}
        deterministic&=a.Positions()==b.Positions();for(std::size_t i=0;i<outA.global.size();++i)deterministic&=outA.global[i]==outB.global[i];
        if(tick<120)maxLag=std::max(maxLag,a.Stats().maxTipDisplacement);
        const auto tip=a.Positions().back();if(tick>=590)lateVelocity=std::max(lateVelocity,glm::length(tip-oldTip)*60);oldTip=tip;
        okay&=a.Stats().maxRootError<1e-11;
    }
    Expect(okay,"600 fixed steps remain finite with strictly attached hair roots");
    if(!okay)std::printf("    %s\n",error.c_str());
    Expect(finite&&consistent,"Physical nodes and written local/global hair poses agree");
    Expect(exactOther&&exactScale,"Every unrelated animated bone, morph, local translation and scale remains exact");
    Expect(deterministic,"Independent simulators produce identical nodes and rendered bone transforms");
    std::printf("    Moving-head maximum lag %.3f mm; final settling speed %.6f m/s\n",maxLag*1000,lateVelocity);
    Expect(maxLag>.003&&maxLag<.65,"Head movement produces bounded visible inertia");
    Expect(lateVelocity<.002,"A held animation settles without numerical energy growth");
    const auto previous=a.Positions();const auto previousGlobal=outA.global;
    okay=a.Step(f.hair,f.model,pose,0,f.hair.settings,outA,&error);
    Expect(okay&&a.Positions()==previous&&outA.global==previousGlobal,"Zero delta preserves the same completed physical pose");
    okay=a.Reset(f.hair,f.model,pose,&error)&&a.Step(f.hair,f.model,pose,1./240.,f.hair.settings,outA,&error);
    Expect(okay&&a.Stats().steps==0,"Fractional deltas accumulate without fabricating a whole physical tick");
    for(int i=0;i<3&&okay;++i)okay=a.Step(f.hair,f.model,pose,1./240.,f.hair.settings,outA,&error);
    Expect(okay&&a.Stats().steps==1,"Four quarter ticks produce exactly one 60 Hz step");
    Expect(!a.Step(f.hair,f.model,pose,std::numeric_limits<double>::quiet_NaN(),f.hair.settings,outA,&error),"Nonfinite time is rejected before it can corrupt physical state");
}
void SurfaceCollision(){
    std::puts("Actual rendered skin contact and arbitrary-pose resets:");auto f=Make();auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator simulator;SkeletalPose result;std::string error;
    bool okay=simulator.Reset(f.hair,f.model,pose,&error)&&simulator.Step(f.hair,f.model,pose,0,f.hair.settings,result,&error);
    double minimum=1;for(const auto& p:f.hair.probes)minimum=std::min(minimum,Probe(p,result,1).z);
    std::printf("    Authored overlap %.3f mm, after reset nearest z %.3f mm, measured residual %.3f mm\n",simulator.Stats().sourcePenetration*1000,minimum*1000,simulator.Stats().maxPenetration*1000);
    Expect(okay&&simulator.Stats().sourcePenetration>.009,"Authored source overlap is retained as an independent diagnostic");
    Expect(okay&&minimum>-.001,"Surface contact rotates actual skin vertices out of the body, rather than only debug nodes");
    for(int tick=0;okay&&tick<180;++tick){pose.local[0].translation.z=.025*std::sin(tick*.05);pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
        okay=simulator.Step(f.hair,f.model,pose,1./60.,f.hair.settings,result,&error);okay&=simulator.Stats().maxRootError<1e-11&&simulator.Stats().maxPenetration<.006;}
    Expect(okay,"Body motion and repeated surface contacts preserve a bounded rendered-surface residual");
    if(!okay)std::printf("    %s; residual %.3f mm\n",error.c_str(),simulator.Stats().maxPenetration*1000);
    for(const double angle:{0.,.6,-.6,1.2}){pose.local[1].rotation=glm::angleAxis(angle,glm::dvec3(0,0,1));pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
        HairSimulator other;SkeletalPose out;const bool reset=simulator.Reset(f.hair,f.model,pose,&error)&&other.Reset(f.hair,f.model,pose,&error)
            &&simulator.Step(f.hair,f.model,pose,0,f.hair.settings,result,&error)&&other.Step(f.hair,f.model,pose,0,f.hair.settings,out,&error);
        Expect(reset&&simulator.Positions()==other.Positions()&&simulator.Stats().maxRootError<1e-11,"Arbitrary animation pose resets are deterministic and keep the root attached");}
}
void MovingBodyContact(){
    std::puts("Independent arm surface crossing the rendered strand:");auto f=Make();
    f.model.geometry->meshes.resize(3);f.model.skins.resize(3);auto& mesh=f.model.geometry->meshes[2];mesh.name="Moving arm";
    for(const auto p:{glm::dvec3(-.4,.35,-.12),glm::dvec3(.4,.35,-.12),glm::dvec3(.4,1.02,-.12),glm::dvec3(-.4,1.02,-.12)}){
        StaticVertex v;v.position=p;v.normal={0,0,1};mesh.vertices.push_back(v);VertexSkin skin;skin.joints={5,0,0,0};skin.weights={1,0,0,0};f.model.skins[2].vertices.push_back(skin);}
    mesh.indices={0,1,2,0,2,3};f.model.geometry->instances.push_back({"Moving arm",2,glm::dmat4(1)});
    auto animated=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator solver;SkeletalPose out;std::string error;
    bool okay=solver.Reset(f.hair,f.model,animated,&error),preserved=true;double maximumDepth=0,root=0;
    for(int frame=0;okay&&frame<240;++frame){const double displacement=.18*std::pow(std::sin(glm::pi<double>()*double(frame)/239),2);
        animated.local[5].translation.z=.2+displacement;animated.global=GlobalBoneTransforms(f.model.skeleton,animated.local);
        okay=solver.Step(f.hair,f.model,animated,1./60.,f.hair.settings,out,&error);
        preserved&=out.global[5]==animated.global[5]&&out.local[5].rotation==animated.local[5].rotation;
        root=std::max(root,solver.Stats().maxRootError);
        const double plane=-.12+displacement;
        for(const auto& p:f.hair.probes){const auto q=Probe(p,out,1);
            if(q.y>.35&&q.y<1.02&&std::abs(q.x)<.4)maximumDepth=std::max(maximumDepth,std::max(0.,plane-q.z));}
    }
    std::printf("    Independent arm intersection depth %.3f mm, root error %.9g meters\n",maximumDepth*1000,root);
    Expect(okay&&root<1e-11&&preserved,"The moving non-hair collision bone remains exact while every hair root stays attached");
    Expect(okay&&maximumDepth<.006,"Actual weighted hair surface responds to another animated body part crossing its path");
}
void Units(){
    std::puts("Unit conversion and preserved local scale:");auto a=Make(1,false),b=Make(13.3405118467,false);
    auto pa=EvaluateSkeletalPose(a.model,nullptr,0,false),pb=EvaluateSkeletalPose(b.model,nullptr,0,false);HairSimulator sa,sb;SkeletalPose oa,ob;std::string error;
    bool okay=sa.Reset(a.hair,a.model,pa,&error)&&sb.Reset(b.hair,b.model,pb,&error);double errorMeters=0;
    for(int tick=0;okay&&tick<180;++tick){const double movement=.05*std::sin(tick*.07);pa.local[1].translation.x=movement;pb.local[1].translation.x=movement*b.hair.unitsPerMeter;
        pa.global=GlobalBoneTransforms(a.model.skeleton,pa.local);pb.global=GlobalBoneTransforms(b.model.skeleton,pb.local);
        okay=sa.Step(a.hair,a.model,pa,1./60.,a.hair.settings,oa,&error)&&sb.Step(b.hair,b.model,pb,1./60.,b.hair.settings,ob,&error);
        for(std::size_t i=0;i<sa.Positions().size();++i)errorMeters=std::max(errorMeters,glm::length(sa.Positions()[i]-sb.Positions()[i]/b.hair.unitsPerMeter));}
    std::printf("    Maximum SI difference %.12g meters\n",errorMeters);Expect(okay&&errorMeters<1e-9,"Source-unit scale changes do not alter SI inertia or constraints");
    pa.local[2].scale={1.1,.9,1.03};pa.global=GlobalBoneTransforms(a.model.skeleton,pa.local);okay=sa.Reset(a.hair,a.model,pa,&error)&&sa.Step(a.hair,a.model,pa,1./60.,a.hair.settings,oa,&error);
    Expect(okay&&oa.local[2].scale==pa.local[2].scale&&oa.local[2].translation==pa.local[2].translation,"Nonuniform authored bone scale is preserved exactly");
}
void SubtickPresentation(){
    std::puts("Current animated body and attached hair between physical ticks:");auto f=Make();
    auto initial=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator shown,direct;SkeletalPose base,out,reference;std::string error;
    bool okay=shown.Reset(f.hair,f.model,initial,&error)&&direct.Reset(f.hair,f.model,initial,&error)
        &&shown.Step(f.hair,f.model,initial,0,f.hair.settings,base,&error);
    if(!Expect(okay,"The presentation fixture resets with its authored surface overlap resolved")){std::printf("    %s\n",error.c_str());return;}
    const double initialRotationDelta=2*std::acos(std::clamp(std::abs(glm::dot(glm::normalize(base.local[2].rotation),glm::normalize(initial.local[2].rotation))),0.,1.));
    if(!Expect(shown.Stats().sourcePenetration>.009&&initialRotationDelta>.01,
        "The subtick fixture holds a real nonzero simulated rotation from closed-body contact"))return;
    auto current=initial;current.local[1].translation+=glm::dvec3(.13,.04,-.02);current.local[1].rotation=glm::angleAxis(.35,glm::dvec3(0,1,0));
    current.local[5].translation.x+=.17;current.local[2].rotation=glm::angleAxis(.12,glm::dvec3(0,0,1))*current.local[2].rotation;
    current.local[3].scale={1.04,.97,1.01};current.morphWeights={.9f};current.global=GlobalBoneTransforms(f.model.skeleton,current.local);
    okay=shown.Step(f.hair,f.model,current,1./240.,f.hair.settings,out,&error);
    bool exact=true,shape=true,consistent=true;
    for(const auto b:{0u,1u,5u})exact&=out.global[b]==current.global[b]&&out.local[b].translation==current.local[b].translation&&out.local[b].rotation==current.local[b].rotation;
    exact&=out.morphWeights==current.morphWeights;
    for(std::uint32_t b=2;b<=4;++b){const auto before=glm::normalize(base.local[b].rotation*glm::inverse(initial.local[b].rotation));
        const auto after=glm::normalize(out.local[b].rotation*glm::inverse(current.local[b].rotation));
        shape&=std::abs(glm::dot(before,after))>1-1e-12;
        shape&=out.local[b].translation==current.local[b].translation&&out.local[b].scale==current.local[b].scale;
        consistent&=Near(out.global[b],out.global[f.model.skeleton.bones[b].parent]*TransformMatrix(out.local[b]));}
    Expect(okay&&shown.Stats().steps==0&&exact,"A quarter tick presents the current head, unrelated bones and morphs exactly");
    Expect(okay&&shape&&consistent,"The held simulated rotation offset follows new animation keys with consistent local/global hair transforms");
    Expect(okay&&glm::length(shown.Positions().front()-glm::dvec3(current.global[2][3]))<1e-11&&shown.Stats().maxRootError<1e-11,
        "Subtick debug nodes and rendered roots follow the current head without moving a physical tick");
    auto paused=current;paused.local[1].translation.x+=.025;paused.morphWeights={.23f};paused.global=GlobalBoneTransforms(f.model.skeleton,paused.local);
    okay=shown.Step(f.hair,f.model,paused,0,f.hair.settings,out,&error);
    Expect(okay&&shown.Stats().steps==0&&out.global[1]==paused.global[1]&&out.global[5]==paused.global[5]&&out.morphWeights==paused.morphWeights
        &&glm::length(shown.Positions().front()-glm::dvec3(paused.global[2][3]))<1e-11,
        "Zero delta freezes only the simulated hair offset while current body animation remains exact");
    okay=shown.Step(f.hair,f.model,current,3./240.,f.hair.settings,out,&error)
        &&direct.Step(f.hair,f.model,current,1./60.,f.hair.settings,reference,&error);
    Expect(okay&&shown.Stats().steps==1&&out.global==reference.global&&shown.Positions()==direct.Positions(),
        "Intermediate presentation never feeds rendered hair or changed head transforms back into physical state");
}
void ScalpAttachment(){
    std::puts("Authored scalp attachment and independent free root-only strands:");auto f=Make();
    for(auto& bodySkin:f.model.skins[0].vertices){bodySkin.joints={1,0,0,0};bodySkin.weights={1,0,0,0};}
    // One deliberately embedded root/Head blend is authored as attachment.
    // The remaining probes keep their original strict free-surface contact.
    auto& weights=f.model.skins[1].vertices[0];weights.joints={2,1,0,0};weights.weights={.4f,.6f,0,0};
    f.hair.attachments={{1,0,0}};f.hair.probes.erase(f.hair.probes.begin());
    const auto rest=EvaluateSkeletalPose(f.model,nullptr,0,false);
    const auto vertex=f.model.geometry->meshes[1].vertices[0].position;
    const auto radius=glm::length(vertex-glm::dvec3(rest.global[2][3]));
    // The asset builder derives this same conservative angular displacement
    // bound from every member of the fixed attachment region.
    const double lever=double(weights.weights[0])/double(weights.weights[0]+weights.weights[1])*radius;
    f.hair.chains[0].links[0].maxAngle=2*std::asin(.0015/(2*lever));
    SkeletonBone shortBone;shortBone.name="FreeShortStrand";shortBone.parent=1;shortBone.restLocal.translation={.15,0,.025};
    f.model.skeleton.bones.push_back(shortBone);
    const auto bind=GlobalBoneTransforms(f.model.skeleton,RestPose(f.model.skeleton));f.model.skeleton.bones[6].inverseBind=glm::inverse(bind[6]);
    HairChain shortChain;shortChain.name="free short root-only";shortChain.anchorBone=1;HairBoneLink shortLink;
    shortLink.bone=6;shortLink.restTip={0,-.18,0};shortLink.maxAngle=1.1;shortChain.links.push_back(shortLink);f.hair.chains.push_back(shortChain);
    StaticVertex freeVertex;freeVertex.position={.15,.98,-.005};freeVertex.normal={0,0,1};f.model.geometry->meshes[1].vertices.push_back(freeVertex);
    VertexSkin freeSkin;freeSkin.joints={6,0,0,0};freeSkin.weights={1,0,0,0};f.model.skins[1].vertices.push_back(freeSkin);
    f.model.geometry->meshes[1].indices.insert(f.model.geometry->meshes[1].indices.end(),{0,2,3});
    HairSurfaceProbe freeProbe;freeProbe.mesh=1;freeProbe.vertex=3;freeProbe.chain=1;
    freeProbe.influences.push_back({6,1,glm::dvec3(f.model.skeleton.bones[6].inverseBind*glm::dvec4(freeVertex.position,1))});f.hair.probes.push_back(freeProbe);
    HairSurfaceProbe attachment;attachment.mesh=1;attachment.vertex=0;attachment.chain=0;
    const auto& attachedSkin=f.model.skins[1].vertices[0];double sum=0;for(int k=0;k<4;++k)sum+=attachedSkin.weights[k];
    for(int k=0;k<4;++k)if(attachedSkin.weights[k]>0){const auto b=attachedSkin.joints[k];const double w=attachedSkin.weights[k]/sum;
        attachment.influences.push_back({b,w,glm::dvec3(f.model.skeleton.bones[b].inverseBind*glm::dvec4(vertex,1))*w});}
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);pose.morphWeights={.35f};HairSimulator simulator;SkeletalPose out;std::string error;
    bool okay=simulator.Reset(f.hair,f.model,pose,&error)&&simulator.Step(f.hair,f.model,pose,0,f.hair.settings,out,&error);
    if(!Expect(okay,"The mixed-head attachment asset initializes without treating its buried root as free cloth")){std::printf("    %s\n",error.c_str());return;}
    StaticModel rendered;okay=SkinSkeletalMesh(f.model,out,rendered,&error)&&ApplyHairSurfaceOffsets(f.model,out,simulator.SurfaceOffsets(),rendered,&error);
    const auto shortPoint=okay?rendered.meshes[1].vertices[3].position:Probe(freeProbe,out,1);
    Expect(okay&&shortPoint.z>-.001&&glm::length(shortPoint-freeVertex.position)>.004,
        "A separate root-only strand with no static Head weight still receives real surface collision");
    double actualMaximum=0,statError=0;bool exact=true;
    for(int frame=0;okay&&frame<600;++frame){pose.local[1].translation.x=.03*std::sin(frame*.05);
        pose.local[1].rotation=glm::angleAxis(.12*std::sin(frame*.04),glm::dvec3(0,1,0));pose.morphWeights={float(.5+.4*std::sin(frame*.02))};
        pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
        okay=simulator.Step(f.hair,f.model,pose,frame%3?1./60.:0.,f.hair.settings,out,&error);
        for(const auto b:{0u,1u,5u})exact&=out.global[b]==pose.global[b]&&out.local[b].translation==pose.local[b].translation&&out.local[b].rotation==pose.local[b].rotation;
        exact&=out.morphWeights==pose.morphWeights;
        const auto delta=glm::dmat3(glm::inverse(pose.global[1]))*(Probe(attachment,out,1)-Probe(attachment,pose,1));
        const auto drift=glm::length(delta);actualMaximum=std::max(actualMaximum,drift);
        statError=std::max(statError,std::abs(drift-simulator.Stats().maxAttachmentDrift));
    }
    std::printf("    Actual head-local attachment drift %.6f mm, statistic difference %.12g meters\n",actualMaximum*1000,statError);
    Expect(okay&&actualMaximum<=.0015+1e-10,"Six hundred moving-head and zero-tick presentations keep the authored scalp attachment within 1.5 mm");
    Expect(okay&&statError<1e-10&&exact,"Attachment statistics use actual rendered LBS while all other animated bones and morphs remain exact");
}
void CatchUpSweep(){
    std::puts("Large-delta catch-up consumes source-body motion only once:");
    auto f=Make();const glm::dvec3 center(.04,.76,-.08),extent(.12,.24,.035);
    SkeletonBone obstacleBone;obstacleBone.name="Independent closed obstacle";obstacleBone.parent=0;obstacleBone.restLocal.translation=center;
    f.model.skeleton.bones.push_back(obstacleBone);const auto bind=GlobalBoneTransforms(f.model.skeleton,RestPose(f.model.skeleton));
    f.model.skeleton.bones[6].inverseBind=glm::inverse(bind[6]);f.model.geometry->meshes.resize(3);f.model.skins.resize(3);
    auto& mesh=f.model.geometry->meshes[2];mesh.name="Independent moving body";mesh.indices=f.model.geometry->meshes[0].indices;
    const glm::dvec3 corners[]={{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}};
    for(const auto& corner:corners){StaticVertex v;v.position=center+corner*extent;v.normal=glm::vec3(glm::normalize(corner));mesh.vertices.push_back(v);
        VertexSkin skin;skin.joints={6,0,0,0};skin.weights={1,0,0,0};f.model.skins[2].vertices.push_back(skin);}
    f.model.geometry->instances.push_back({"Independent moving body",2,glm::dmat4(1)});
    const auto initial=EvaluateSkeletalPose(f.model,nullptr,0,false);std::string error;bool okay=true,exactBody=true;double maximum=0;
    for(int fixture=0;fixture<12&&okay;++fixture){HairSimulator batched,serial;SkeletalPose batchResult,serialResult;
        okay=batched.Reset(f.hair,f.model,initial,&error)&&serial.Reset(f.hair,f.model,initial,&error);
        auto endpoint=initial;const double angle=(fixture%4-1.5)*.36;
        endpoint.local[6].translation=center+glm::dvec3(.025*std::sin(fixture),.015*std::cos(fixture),.145);
        endpoint.local[6].rotation=glm::angleAxis(angle,fixture/4==0?glm::dvec3(1,0,0):fixture/4==1?glm::dvec3(0,1,0):glm::dvec3(0,0,1));
        endpoint.local[1].translation.x+=.01*std::sin(fixture*.7);endpoint.local[1].rotation=glm::angleAxis(.08*std::cos(fixture),glm::dvec3(0,0,1));
        endpoint.global=GlobalBoneTransforms(f.model.skeleton,endpoint.local);const int ticks=fixture%3+2;
        okay=okay&&batched.Step(f.hair,f.model,endpoint,double(ticks)/60.,f.hair.settings,batchResult,&error);
        for(int tick=0;tick<ticks&&okay;++tick)okay=serial.Step(f.hair,f.model,endpoint,1./60.,f.hair.settings,serialResult,&error);
        if(!okay)break;
        for(std::size_t p=0;p<batched.Positions().size();++p)maximum=std::max(maximum,glm::length(batched.Positions()[p]-serial.Positions()[p]));
        for(std::size_t b=0;b<batchResult.global.size();++b)for(int column=0;column<4;++column)for(int row=0;row<4;++row)
            maximum=std::max(maximum,std::abs(batchResult.global[b][column][row]-serialResult.global[b][column][row]));
        exactBody&=batchResult.global[6]==endpoint.global[6]&&batchResult.global[1]==endpoint.global[1]
            &&batched.Stats().steps==std::uint64_t(ticks)&&serial.Stats().steps==std::uint64_t(ticks);
    }
    std::printf("    Maximum batched/serial pose or node difference %.12g\n",maximum);
    Expect(okay&&exactBody,"Two to four catch-up ticks preserve the completed animated body and exact physical tick count");
    Expect(okay&&maximum<1e-9,"Moving and rotating finite body crossings match one motion tick followed by held-body ticks");
    HairSimulator backlog;SkeletalPose out;okay=backlog.Reset(f.hair,f.model,initial,&error)
        &&backlog.Step(f.hair,f.model,initial,.2,f.hair.settings,out,&error);const auto steps=backlog.Stats().steps;
    const auto held=out.global;okay=okay&&backlog.Step(f.hair,f.model,initial,0,f.hair.settings,out,&error);
    Expect(okay&&steps==4&&backlog.Stats().steps==steps&&out.global==held,
        "An explicit zero delta freezes physical state even when the catch-up cap retained older time");
}
void SaturatedRootContact(){
    std::puts("A saturated scalp root redistributes contact to its free child:");auto f=Make();
    // The root has a long normal lever but only 0.0001 rad of freedom. The
    // child has a short lever with enough angular range to solve the 15 mm
    // actual-surface embedding. Counting the blocked root in the denominator
    // starves that child even after repeated ordinary contact passes.
    f.model.skeleton.bones[3].restLocal.translation={0,-.7,0};f.hair.chains[0].links[0].restTip={0,-.7,0};
    f.hair.chains[0].links[0].maxAngle=.0001;f.hair.settings.gravity={0,0,0};
    const auto globals=GlobalBoneTransforms(f.model.skeleton,RestPose(f.model.skeleton));
    for(std::size_t b=0;b<globals.size();++b)f.model.skeleton.bones[b].inverseBind=glm::inverse(globals[b]);
    auto& vertices=f.model.geometry->meshes[1].vertices;vertices[0].position={.04,1.0,.035};vertices[1].position={.04,.37,-.015};vertices[2].position={.04,.15,.035};
    HairSurfaceProbe surface;surface.mesh=1;surface.vertex=1;surface.chain=0;
    surface.influences.push_back({3,1,glm::dvec3(f.model.skeleton.bones[3].inverseBind*glm::dvec4(vertices[1].position,1))});f.hair.probes={surface};
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);const auto initial=pose;HairSimulator solver;SkeletalPose out;std::string error;
    bool okay=solver.Reset(f.hair,f.model,pose,&error)&&solver.Step(f.hair,f.model,pose,0,f.hair.settings,out,&error);
    if(!Expect(okay&&solver.Stats().sourcePenetration>.014,"The contact fixture starts with a real 15 mm embedded rendered skin point")){std::printf("    %s\n",error.c_str());return;}
    double minimumZ=Probe(surface,out,1).z,maximumRootAngle=0,maximumChildAngle=0;bool exact=true;
    for(int frame=0;okay&&frame<120;++frame){pose.local[1].translation.x=.01*std::sin(frame*.05);
        pose.local[1].rotation=glm::angleAxis(.03*std::sin(frame*.04),glm::dvec3(0,1,0));pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
        okay=solver.Step(f.hair,f.model,pose,1./60.,f.hair.settings,out,&error);minimumZ=std::min(minimumZ,Probe(surface,out,1).z);
        const auto angle=[&](std::uint32_t b){return 2*std::acos(std::clamp(std::abs(glm::dot(glm::normalize(out.local[b].rotation),glm::normalize(initial.local[b].rotation))),0.,1.));};
        maximumRootAngle=std::max(maximumRootAngle,angle(2));maximumChildAngle=std::max(maximumChildAngle,angle(3));
        for(const auto b:{0u,1u,5u})exact&=out.global[b]==pose.global[b]&&out.local[b].rotation==pose.local[b].rotation;
    }
    std::printf("    Lowest actual skin z %.6f mm, root swing %.9f rad, free child swing %.6f rad\n",minimumZ*1000,maximumRootAngle,maximumChildAngle);
    Expect(okay&&minimumZ>-.0005,"Bound-aware local contact relinearization removes the actual skin embedding without extra global query passes");
    Expect(okay&&maximumRootAngle<=.0001+1e-8&&maximumChildAngle>.2&&exact,
        "The root keeps its strict angular cap while the available child performs the correction and unrelated animation stays exact");
}
void ClearanceCertificates(){
    std::puts("Exact same-pose outside-clearance certificates versus uncached queries:");
    // Exercise the repeated endpoint passes that the certificate eliminates;
    // default single-pass contact is covered independently below.
    ScopedHairCache coarseReference("0","MODEL_HAIR_COARSE_CONTACTS"),freshMeasurements("0","MODEL_HAIR_MEASUREMENT_REUSE");
    bool okay=true,matchingStats=true;double maximum=0;
    std::size_t cachedQueries=0,referenceQueries=0,skips=0,contacts=0,staticQueries=0,staticReferenceQueries=0;
    for(int fixture=0;fixture<3&&okay;++fixture){auto f=Make();
        if(fixture!=2){for(auto& v:f.model.geometry->meshes[1].vertices)v.position.z=.02;
            for(auto& p:f.hair.probes){p.influences.clear();const auto b=p.vertex+2;
                p.influences.push_back({b,1,glm::dvec3(f.model.skeleton.bones[b].inverseBind*glm::dvec4(f.model.geometry->meshes[1].vertices[p.vertex].position,1))});}}
        auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator cached,reference;SkeletalPose a,b;std::string error;
        {ScopedHairCache option("1");okay=cached.Reset(f.hair,f.model,pose,&error);}
        {ScopedHairCache option("0");okay=okay&&reference.Reset(f.hair,f.model,pose,&error);}
        if(!okay)break;contacts+=cached.Stats().contacts;
        for(int frame=0;frame<120&&okay;++frame){
            if(fixture){pose.local[0].translation.z=.06*std::sin(frame*.09);pose.local[1].translation.x=.025*std::sin(frame*.05);
                pose.local[1].rotation=glm::angleAxis(.12*std::sin(frame*.07),glm::dvec3(0,0,1));}
            pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);const double delta=frame%19?1./60.:2./60.;
            okay=cached.Step(f.hair,f.model,pose,delta,f.hair.settings,a,&error)&&reference.Step(f.hair,f.model,pose,delta,f.hair.settings,b,&error);
            if(!okay)break;
            for(std::size_t p=0;p<cached.Positions().size();++p)maximum=std::max(maximum,glm::length(cached.Positions()[p]-reference.Positions()[p]));
            for(std::size_t bone=0;bone<a.global.size();++bone)for(int column=0;column<4;++column)for(int row=0;row<4;++row)
                maximum=std::max(maximum,std::abs(a.global[bone][column][row]-b.global[bone][column][row]));
            const auto& cs=cached.Stats();const auto& rs=reference.Stats();
            maximum=std::max(maximum,std::abs(cs.maxPenetration-rs.maxPenetration));
            matchingStats&=cs.steps==rs.steps&&cs.contacts==rs.contacts&&cs.worstPenetrationProbe==rs.worstPenetrationProbe
                &&std::abs(cs.maxRootError-rs.maxRootError)<1e-12&&std::abs(cs.maxAttachmentDrift-rs.maxAttachmentDrift)<1e-12;
            cachedQueries+=cs.collisionQueries;referenceQueries+=rs.collisionQueries;skips+=cs.clearanceSkips;contacts+=cs.contacts;
            if(fixture==0){staticQueries+=cs.collisionQueries;staticReferenceQueries+=rs.collisionQueries;}
            okay&=rs.clearanceSkips==0;
        }
        if(!okay)std::printf("    Cache comparison fixture %d: %s\n",fixture,error.c_str());
    }
    std::printf("    Query work cached/reference %zu/%zu, proven skips %zu, real contacts %zu; maximum pose/residual difference %.12g\n",
        cachedQueries,referenceQueries,skips,contacts,maximum);
    Expect(okay&&matchingStats&&maximum<1e-9,
        "Static, moving-body, true-contact and catch-up poses/nodes/penetration agree with MODEL_HAIR_CONTACT_CACHE=0");
    Expect(okay&&contacts>0&&skips>0&&cachedQueries<referenceQueries&&staticQueries*2<staticReferenceQueries,
        "Certificates reduce real query work while actual contacts remain active and moving-body epochs cannot reuse stale clearances");
}
void AdaptiveContacts(){
    std::puts("Optional dirty-chain contact refinement and unchanged low-contact work:");
    ScopedHairCache enableBoneContacts("1","MODEL_HAIR_BONE_SURFACE_CONTACTS");
    auto quiet=Make();quiet.hair.settings.gravity={0,0,0};
    for(auto& v:quiet.model.geometry->meshes[1].vertices)v.position.z=.02;
    for(auto& p:quiet.hair.probes){p.influences.clear();const auto bone=p.vertex+2;
        p.influences.push_back({bone,1,glm::dvec3(quiet.model.skeleton.bones[bone].inverseBind*glm::dvec4(quiet.model.geometry->meshes[1].vertices[p.vertex].position,1))});}
    auto pose=EvaluateSkeletalPose(quiet.model,nullptr,0,false);HairSimulator enabled,disabled,defaultPath;SkeletalPose on,off,standard;std::string error;bool okay=true;
    {ScopedHairCache option("1","MODEL_HAIR_ADAPTIVE_CONTACTS");okay=enabled.Reset(quiet.hair,quiet.model,pose,&error);}
    {ScopedHairCache option("0","MODEL_HAIR_ADAPTIVE_CONTACTS");okay=okay&&disabled.Reset(quiet.hair,quiet.model,pose,&error);}
    {ScopedHairCache option("","MODEL_HAIR_ADAPTIVE_CONTACTS");okay=okay&&defaultPath.Reset(quiet.hair,quiet.model,pose,&error);}
    bool exact=true,sameWork=true;
    for(int frame=0;okay&&frame<60;++frame){okay=enabled.Step(quiet.hair,quiet.model,pose,1./60.,quiet.hair.settings,on,&error)
        &&disabled.Step(quiet.hair,quiet.model,pose,1./60.,quiet.hair.settings,off,&error)
        &&defaultPath.Step(quiet.hair,quiet.model,pose,1./60.,quiet.hair.settings,standard,&error);
        exact&=on.global==off.global&&off.global==standard.global&&enabled.Positions()==disabled.Positions()&&disabled.Positions()==defaultPath.Positions();
        sameWork&=enabled.Stats().collisionQueries==disabled.Stats().collisionQueries&&enabled.Stats().adaptiveQueries==0&&enabled.Stats().adaptiveRounds==0;}
    Expect(okay&&exact,"The disabled path stays bitwise equal to the default, and an unneeded adaptive stage changes no pose");
    Expect(okay&&sameWork,"Static separated hair does not acquire extra probe queries or contact rounds");
    double enabledResidual=0,disabledResidual=0,bestImprovement=0,attachmentDrift=0;std::size_t extraQueries=0;bool guarded=true,bodyExact=true,resetInvariant=true;
    for(const double tilt:{.12,.20,.28}){auto f=Make();f.hair.settings.gravity={0,0,0};f.hair.chains[0].links[0].maxAngle=.0001;
        auto& vertices=f.model.geometry->meshes[1].vertices;vertices[0].position={-.02,.8,-.002};vertices[1].position={.10,.8,.022};vertices[2].position={.04,1.02,-.01};
        for(int v=0;v<2;++v){auto& skin=f.model.skins[1].vertices[v];skin.joints={3,0,0,0};skin.weights={1,0,0,0};}
        auto& rootSkin=f.model.skins[1].vertices[2];rootSkin.joints={2,1,0,0};rootSkin.weights={.4f,.6f,0,0};f.hair.attachments={{1,2,0}};f.hair.probes.clear();
        for(std::uint32_t v=0;v<2;++v){HairSurfaceProbe p;p.mesh=1;p.vertex=v;p.chain=0;
            p.influences.push_back({3,1,glm::dvec3(f.model.skeleton.bones[3].inverseBind*glm::dvec4(vertices[v].position,1))});f.hair.probes.push_back(p);}
        // Two finite closed bodies give the same free child nearly opposing
        // X/Z swing constraints. Both can be satisfied; alternating one-point
        // corrections converge slowly and can invalidate an earlier point.
        const auto rotation=glm::angleAxis(-tilt,glm::dvec3(0,1,0));const auto normal=rotation*glm::dvec3(0,0,-1);
        const auto center=vertices[1].position-normal*.038;const glm::dvec3 extent(.16,.18,.04);
        f.model.geometry->meshes.resize(3);f.model.skins.resize(3);auto& box=f.model.geometry->meshes[2];box.name="Finite coupled contact";
        box.indices=f.model.geometry->meshes[0].indices;
        const glm::dvec3 corners[]={{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}};
        for(const auto corner:corners){StaticVertex v;v.position=center+rotation*(corner*extent);v.normal=glm::vec3(rotation*glm::normalize(corner));box.vertices.push_back(v);
            VertexSkin s;s.joints={0,0,0,0};s.weights={1,0,0,0};f.model.skins[2].vertices.push_back(s);}
        f.model.geometry->instances.push_back({"Finite coupled contact",2,glm::dmat4(1)});
        auto animated=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator refined,old;
        {ScopedHairCache option("1","MODEL_HAIR_ADAPTIVE_CONTACTS");okay=okay&&refined.Reset(f.hair,f.model,animated,&error);}
        {ScopedHairCache option("0","MODEL_HAIR_ADAPTIVE_CONTACTS");okay=okay&&old.Reset(f.hair,f.model,animated,&error);}
        extraQueries+=refined.Stats().adaptiveQueries;
        resetInvariant&=refined.Stats().steps==0&&old.Stats().steps==0&&refined.Stats().sourcePenetration==old.Stats().sourcePenetration
            &&refined.Stats().adaptiveQueries<=256&&refined.Stats().adaptiveRounds<=3;
        const auto resetNodes=refined.Positions();const auto resetSource=refined.Stats().sourcePenetration;
        okay=okay&&refined.Step(f.hair,f.model,animated,0,f.hair.settings,on,&error);
        resetInvariant&=refined.Stats().steps==0&&refined.Stats().sourcePenetration==resetSource&&refined.Positions()==resetNodes;
        for(int frame=0;okay&&frame<30;++frame){okay=refined.Step(f.hair,f.model,animated,1./60.,f.hair.settings,on,&error)
            &&old.Step(f.hair,f.model,animated,1./60.,f.hair.settings,off,&error);
            const auto& rs=refined.Stats();const auto& os=old.Stats();enabledResidual+=rs.maxPenetration;disabledResidual+=os.maxPenetration;
            bestImprovement=std::max(bestImprovement,os.maxPenetration-rs.maxPenetration);extraQueries+=rs.adaptiveQueries;
            attachmentDrift=std::max(attachmentDrift,rs.maxAttachmentDrift);guarded&=rs.adaptiveQueries<=256&&rs.adaptiveRounds<=3&&rs.maxRootError<1e-11;
            for(const auto b:{0u,1u,5u})bodyExact&=on.global[b]==animated.global[b]&&on.local[b].rotation==animated.local[b].rotation;
        }
    }
    std::printf("    Coupled residual sums enabled/disabled %.6f/%.6f mm, best improvement %.6f mm, extra queries %zu, attachment drift %.9f mm\n",
        enabledResidual*1000,disabledResidual*1000,bestImprovement*1000,extraQueries,attachmentDrift*1000);
    Expect(okay&&extraQueries>0&&bestImprovement>.00005&&enabledResidual<disabledResidual,
        "Targeted dependent-probe refinement improves actual coupled contact residuals");
    Expect(okay&&guarded&&bodyExact&&attachmentDrift<=.0015+1e-10,
        "Refinement stays within three rounds and 256 extra queries with strict attachment and unchanged body animation");
    Expect(okay&&resetInvariant,"Reset refinement preserves zero physical steps, authored-source statistics and a stationary zero-delta initial pose");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void PostSkinContacts(){
    std::puts("Post-skin contact, limited bone freedom and frozen anchor-local presentation:");
    auto f=Make();f.hair.settings.gravity={0,0,0};
    // This finite, nondegenerate surface is initially 10 mm inside the closed
    // body. Rotation caps deliberately leave too little skeletal freedom to
    // resolve it: a successful result must correct the actual skinned mesh.
    auto& vertices=f.model.geometry->meshes[1].vertices;
    vertices[0].position={.02,.95,-.01};vertices[1].position={.06,.75,-.01};vertices[2].position={.04,.55,-.01};
    for(auto& link:f.hair.chains[0].links)link.maxAngle=.0001;
    for(auto& probe:f.hair.probes){const auto bone=probe.vertex+2;probe.influences.clear();
        probe.influences.push_back({bone,1,glm::dvec3(f.model.skeleton.bones[bone].inverseBind*glm::dvec4(vertices[probe.vertex].position,1))});}
    const std::vector<StaticVertex> authored=vertices;auto animated=EvaluateSkeletalPose(f.model,nullptr,0,false);
    animated.morphWeights={.37f};HairSimulator simulator,duplicate;SkeletalPose out,other;StaticModel raw,corrected;detail::HairBodyCollision independent;std::string error;
    bool okay=simulator.Reset(f.hair,f.model,animated,&error)&&duplicate.Reset(f.hair,f.model,animated,&error)
        &&simulator.Step(f.hair,f.model,animated,0,f.hair.settings,out,&error)
        &&duplicate.Step(f.hair,f.model,animated,0,f.hair.settings,other,&error)
        &&independent.Initialize(f.model,f.hair.hairMeshes,1,&error)&&independent.Update(animated,&error)
        &&SkinSkeletalMesh(f.model,out,corrected,&error)
        &&ApplyHairSurfaceOffsets(f.model,out,simulator.SurfaceOffsets(),corrected,&error);
    if(!Expect(okay,"The mesh contact layer repairs an embedded reset without advancing a physical step")){std::printf("    %s\n",error.c_str());return;}
    double residual=0;for(const auto& vertex:corrected.meshes[1].vertices){const auto nearest=independent.Nearest(vertex.position,.08);
        if(nearest.found)residual=std::max(residual,std::max(0.,f.hair.settings.thickness-nearest.signedDistance));}
    std::printf("    Bone residual %.3f mm, independently measured corrected mesh residual %.3f mm, correction %.3f mm\n",
        simulator.Stats().maxPenetration*1000,residual*1000,simulator.Stats().maxMeshCorrection*1000);
    Expect(simulator.Stats().steps==0&&simulator.Stats().maxPenetration>.009&&residual<.005
        &&simulator.Stats().maxSurfacePenetration<.005&&!simulator.SurfaceOffsets().empty(),
        "Actual corrected free vertices meet 5 mm while the independent bone-contact diagnostic remains unmasked");
    Expect(simulator.SurfaceOffsets()==duplicate.SurfaceOffsets()&&out.global==other.global,
        "Identical reset inputs produce deterministic mesh offsets and unchanged skeletal results");
    const auto offsets=simulator.SurfaceOffsets();auto presented=animated;
    presented.local[1].translation+=glm::dvec3(.03,.02,.01);presented.local[1].rotation=glm::angleAxis(.15,glm::dvec3(0,0,1));
    presented.local[5].translation.x+=.05;presented.morphWeights={.83f};presented.global=GlobalBoneTransforms(f.model.skeleton,presented.local);
    okay=simulator.Step(f.hair,f.model,presented,0,f.hair.settings,out,&error)&&SkinSkeletalMesh(f.model,out,raw,&error);
    corrected=raw;okay=okay&&ApplyHairSurfaceOffsets(f.model,out,simulator.SurfaceOffsets(),corrected,&error);
    bool attached=true,exact=true;for(const auto& offset:offsets){const auto expected=raw.meshes[offset.mesh].vertices[offset.vertex].position
        +glm::dmat3(out.global[offset.anchorBone])*offset.displacement;
        attached&=glm::length(corrected.meshes[offset.mesh].vertices[offset.vertex].position-expected)<1e-11;}
    for(const auto bone:{0u,1u,5u})exact&=out.global[bone]==presented.global[bone]&&out.local[bone].rotation==presented.local[bone].rotation;
    exact&=out.morphWeights==presented.morphWeights;
    Expect(okay&&attached&&exact&&simulator.Stats().steps==0&&simulator.SurfaceOffsets()==offsets,
        "Zero time keeps physical offsets frozen, follows the current head and preserves every unrelated bone and morph");
    bool stable=true;for(int frame=0;okay&&frame<24;++frame){animated.local[0].translation.z=.02*std::sin(frame*.1);
        animated.global=GlobalBoneTransforms(f.model.skeleton,animated.local);
        okay=simulator.Step(f.hair,f.model,animated,1./60.,f.hair.settings,out,&error)&&independent.Update(animated,&error)
            &&SkinSkeletalMesh(f.model,out,corrected,&error)&&ApplyHairSurfaceOffsets(f.model,out,simulator.SurfaceOffsets(),corrected,&error);
        stable&=simulator.Stats().maxRootError<1e-11;for(const auto& vertex:corrected.meshes[1].vertices){const auto nearest=independent.Nearest(vertex.position,.08);
            stable&=Finite(vertex.position)&&(!nearest.found||f.hair.settings.thickness-nearest.signedDistance<.005);}}
    bool sourceExact=true;for(std::size_t v=0;v<authored.size();++v)sourceExact&=vertices[v].position==authored[v].position;
    Expect(okay&&stable&&sourceExact,"Fixed ticks retain mesh contact without detaching roots or modifying source geometry");
    auto empty=Make(1,false);empty.hair.hairMeshes.clear();HairSimulator noSurface;const auto emptyPose=EvaluateSkeletalPose(empty.model,nullptr,0,false);
    okay=noSurface.Reset(empty.hair,empty.model,emptyPose,&error)&&noSurface.Step(empty.hair,empty.model,emptyPose,1./60.,empty.hair.settings,out,&error);
    Expect(okay&&noSurface.SurfaceOffsets().empty()&&noSurface.Stats().surfaceVertices==0,
        "A legacy asset without a hair surface table keeps the mesh contact layer a no-op");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void CoarseContactOption(){
    std::puts("Default coarse skeletal contacts versus the four-pass reference:");
    ScopedHairCache adaptiveOff("0","MODEL_HAIR_ADAPTIVE_CONTACTS"),enableBoneContacts("1","MODEL_HAIR_BONE_SURFACE_CONTACTS");auto f=Make();
    auto& skin=f.model.skins[1].vertices[0];skin.joints={2,1,0,0};skin.weights={.4f,.6f,0,0};
    f.hair.attachments={{1,0,0}};f.hair.probes.erase(f.hair.probes.begin());
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);const auto lever=double(skin.weights[0])/double(skin.weights[0]+skin.weights[1])
        *glm::length(f.model.geometry->meshes[1].vertices[0].position-glm::dvec3(pose.global[2][3]));
    f.hair.chains[0].links[0].maxAngle=2*std::asin(.0015/(2*lever));
    HairSimulator standard,explicitOff,coarse;SkeletalPose a,b,c;std::string error;bool okay=true;
    {ScopedHairCache option("","MODEL_HAIR_COARSE_CONTACTS");okay=standard.Reset(f.hair,f.model,pose,&error);}
    {ScopedHairCache option("0","MODEL_HAIR_COARSE_CONTACTS");okay=okay&&explicitOff.Reset(f.hair,f.model,pose,&error);}
    {ScopedHairCache option("1","MODEL_HAIR_COARSE_CONTACTS");okay=okay&&coarse.Reset(f.hair,f.model,pose,&error);}
    bool resetExact=okay&&standard.Positions()==coarse.Positions()&&standard.SurfaceOffsets()==coarse.SurfaceOffsets()
        &&standard.Positions()==explicitOff.Positions()&&standard.SurfaceOffsets()==explicitOff.SurfaceOffsets()
        &&standard.Stats().sourcePenetration==coarse.Stats().sourcePenetration&&standard.Stats().maxPenetration==coarse.Stats().maxPenetration
        &&standard.Stats().sourcePenetration==explicitOff.Stats().sourcePenetration&&standard.Stats().maxPenetration==explicitOff.Stats().maxPenetration;
    bool bitwise=true,stable=true,exactOther=true;std::size_t referenceQueries=0,coarseQueries=0;
    for(int frame=0;okay&&frame<60;++frame){pose.local[0].translation.z=.025*std::sin(frame*.08);
        pose.local[1].translation.x=.03*std::sin(frame*.05);pose.local[1].rotation=glm::angleAxis(.12*std::sin(frame*.04),glm::dvec3(0,1,0));
        pose.morphWeights={float(.5+.4*std::sin(frame*.02))};pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
        const auto delta=frame%9?1./60.:2./60.;
        okay=standard.Step(f.hair,f.model,pose,delta,f.hair.settings,a,&error)
            &&explicitOff.Step(f.hair,f.model,pose,delta,f.hair.settings,b,&error)
            &&coarse.Step(f.hair,f.model,pose,delta,f.hair.settings,c,&error);
        bitwise&=a.global==c.global&&standard.Positions()==coarse.Positions()&&standard.SurfaceOffsets()==coarse.SurfaceOffsets()
            &&standard.Stats().maxPenetration==coarse.Stats().maxPenetration&&standard.Stats().collisionQueries==coarse.Stats().collisionQueries;
        for(const auto& point:coarse.Positions())stable&=Finite(point);
        for(const auto& point:explicitOff.Positions())stable&=Finite(point);
        for(const auto& offset:coarse.SurfaceOffsets())stable&=Finite(offset.displacement);
        for(const auto& offset:explicitOff.SurfaceOffsets())stable&=Finite(offset.displacement);
        stable&=coarse.Stats().maxRootError<1e-11&&coarse.Stats().maxAttachmentDrift<=.0015+1e-10&&std::isfinite(coarse.Stats().maxPenetration);
        stable&=explicitOff.Stats().maxRootError<1e-11&&explicitOff.Stats().maxAttachmentDrift<=.0015+1e-10&&std::isfinite(explicitOff.Stats().maxPenetration);
        for(const auto bone:{0u,1u,5u})exactOther&=c.global[bone]==pose.global[bone]&&c.local[bone].rotation==pose.local[bone].rotation
            &&b.global[bone]==pose.global[bone]&&b.local[bone].rotation==pose.local[bone].rotation;
        exactOther&=c.morphWeights==pose.morphWeights&&b.morphWeights==pose.morphWeights;
        referenceQueries+=explicitOff.Stats().collisionQueries;coarseQueries+=coarse.Stats().collisionQueries;
    }
    std::printf("    Skeletal query work four-pass/default %zu/%zu\n",referenceQueries,coarseQueries);
    Expect(okay&&resetExact,"Both contact modes retain the same eight-pass reset warmup and authored-source diagnostics");
    Expect(okay&&bitwise,"An unset coarse option and MODEL_HAIR_COARSE_CONTACTS=1 produce bitwise identical poses, offsets and diagnostics");
    Expect(okay&&stable&&exactOther,"Default single-pass and explicit four-pass reference remain finite with strict roots, attachments and exact unrelated animation");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void BoneSurfaceContactOption(){
    std::puts("Attachment-aware mesh contact without conflicting skeletal probe-driven rotations:");
    auto f=Make();f.hair.settings.gravity={0,0,0};auto& vertices=f.model.geometry->meshes[1].vertices;
    vertices[0].position.x=.02;vertices[1].position.x=.06;
    auto& skin=f.model.skins[1].vertices[0];skin.joints={2,1,0,0};skin.weights={.4f,.6f,0,0};
    f.hair.attachments={{1,0,0}};f.hair.probes.erase(f.hair.probes.begin());
    for(auto& probe:f.hair.probes){const auto bone=probe.vertex+2;probe.influences.clear();
        probe.influences.push_back({bone,1,glm::dvec3(f.model.skeleton.bones[bone].inverseBind*glm::dvec4(vertices[probe.vertex].position,1))});}
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);const auto lever=double(skin.weights[0])/double(skin.weights[0]+skin.weights[1])
        *glm::length(vertices[0].position-glm::dvec3(pose.global[2][3]));f.hair.chains[0].links[0].maxAngle=2*std::asin(.0015/(2*lever));
    HairSimulator standard,explicitOn,meshOnly,meshOnlyAdaptive;SkeletalPose a,b,c,d;std::string error;
    const auto reset=[&](HairSimulator& simulator,const char* contacts,const char* adaptive){
        ScopedHairCache option(contacts,"MODEL_HAIR_BONE_SURFACE_CONTACTS"),refine(adaptive,"MODEL_HAIR_ADAPTIVE_CONTACTS");
        return simulator.Reset(f.hair,f.model,pose,&error);};
    bool okay=reset(standard,"","0")&&reset(explicitOn,"1","0")&&reset(meshOnly,"0","0")&&reset(meshOnlyAdaptive,"0","1")
        &&meshOnly.Step(f.hair,f.model,pose,0,f.hair.settings,c,&error)&&meshOnlyAdaptive.Step(f.hair,f.model,pose,0,f.hair.settings,d,&error);
    if(!Expect(okay,"Both experimental modes initialize with the actual mesh contact layer active")){std::printf("    %s\n",error.c_str());return;}
    Expect(standard.Stats().contacts==0&&explicitOn.Stats().contacts>0&&standard.Stats().maxPenetration>.009
        &&standard.Stats().sourcePenetration>.009&&!standard.SurfaceOffsets().empty(),
        "Automatic mesh-only contact leaves raw skeletal/source overlap visible while explicit 1 restores the old bone-contact warmup");
    Expect(c.global==pose.global&&d.global==pose.global&&meshOnly.Stats().contacts==0&&meshOnlyAdaptive.Stats().adaptiveRounds==0
        &&meshOnly.Stats().maxPenetration>.009&&!meshOnly.SurfaceOffsets().empty(),
        "Disabling bone contacts preserves authored reset bones while mesh offsets repair the independently reported overlap");
    detail::HairBodyCollision independent;okay=independent.Initialize(f.model,f.hair.hairMeshes,1,&error)&&independent.Update(pose,&error);
    StaticModel corrected;double maximumMeshResidual=0;bool defaultExact=true,adaptiveBlocked=true,stable=true,exactOther=true,heldRest=true;
    const auto inspect=[&](){if(!SkinSkeletalMesh(f.model,c,corrected,&error)||!ApplyHairSurfaceOffsets(f.model,c,meshOnly.SurfaceOffsets(),corrected,&error))return false;
        for(std::size_t vertex=1;vertex<corrected.meshes[1].vertices.size();++vertex){const auto nearest=independent.Nearest(corrected.meshes[1].vertices[vertex].position,.08);
            if(nearest.found)maximumMeshResidual=std::max(maximumMeshResidual,std::max(0.,f.hair.settings.thickness-nearest.signedDistance));}return true;};
    okay=okay&&inspect();
    for(int frame=0;okay&&frame<120;++frame){const double motion=std::clamp(double(frame)-20.,0.,40.);
        pose.local[0].translation.z=.005*std::sin(motion*.08);pose.local[1].translation.x=.02*std::sin(motion*.1);
        pose.local[1].rotation=glm::angleAxis(.03*std::sin(motion*.09),glm::dvec3(0,0,1));pose.morphWeights={float(.4+.2*std::sin(motion*.06))};
        pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
        okay=standard.Step(f.hair,f.model,pose,1./60.,f.hair.settings,a,&error)&&explicitOn.Step(f.hair,f.model,pose,1./60.,f.hair.settings,b,&error)
            &&meshOnly.Step(f.hair,f.model,pose,1./60.,f.hair.settings,c,&error)&&meshOnlyAdaptive.Step(f.hair,f.model,pose,1./60.,f.hair.settings,d,&error)
            &&independent.Update(pose,&error)&&inspect();
        defaultExact&=a.global==c.global&&standard.Positions()==meshOnly.Positions()&&standard.SurfaceOffsets()==meshOnly.SurfaceOffsets()
            &&standard.Stats().maxPenetration==meshOnly.Stats().maxPenetration&&standard.Stats().contacts==0;
        adaptiveBlocked&=c.global==d.global&&meshOnly.Positions()==meshOnlyAdaptive.Positions()&&meshOnly.SurfaceOffsets()==meshOnlyAdaptive.SurfaceOffsets()
            &&meshOnly.Stats().contacts==0&&meshOnlyAdaptive.Stats().contacts==0&&meshOnlyAdaptive.Stats().adaptiveRounds==0&&meshOnlyAdaptive.Stats().adaptiveQueries==0;
        for(const auto& point:meshOnly.Positions())stable&=Finite(point);for(const auto& offset:meshOnly.SurfaceOffsets())stable&=Finite(offset.displacement);
        stable&=meshOnly.Stats().maxRootError<1e-11&&meshOnly.Stats().maxAttachmentDrift<=.0015+1e-10&&std::isfinite(meshOnly.Stats().maxPenetration);
        if(frame<20)heldRest&=meshOnly.Stats().maxTipDisplacement<1e-10;
        for(const auto bone:{0u,1u,5u})exactOther&=c.global[bone]==pose.global[bone]&&c.local[bone].rotation==pose.local[bone].rotation;
        exactOther&=c.morphWeights==pose.morphWeights;
    }
    std::printf("    Independently measured mesh-only free-surface residual %.6f mm\n",maximumMeshResidual*1000);
    Expect(okay&&defaultExact,"Authored attachments and usable hair geometry select the exact explicit-0 mesh-contact result by default");
    Expect(okay&&adaptiveBlocked,"Explicitly enabled adaptive correction cannot bypass disabled skeletal contacts or rotate the hair");
    Expect(okay&&stable&&exactOther&&heldRest,"Mesh-only contact remains finite, preserves resting strands, roots, scalp caps and unrelated animation");
    Expect(okay&&maximumMeshResidual<.005,"Actual skinned free vertices retain fine mesh collision below 5 mm without bone-driven surface pushes");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void BoneContactFallbackPolicy(){
    std::puts("Skeletal-contact compatibility when authored mesh contact is unavailable:");
    ScopedHairCache adaptiveOff("0","MODEL_HAIR_ADAPTIVE_CONTACTS");bool okay=true,exact=true,legacyActive=true,fallback=true;std::string error;
    for(const bool missingSurface:{false,true}){auto f=Make();f.hair.settings.gravity={0,0,0};
        if(missingSurface){auto& scalp=f.model.skins[1].vertices[0];scalp.joints={2,1,0,0};scalp.weights={.4f,.6f,0,0};
            f.hair.attachments={{1,0,0}};f.hair.probes.erase(f.hair.probes.begin());f.hair.hairMeshes.clear();
            auto rest=EvaluateSkeletalPose(f.model,nullptr,0,false);const double lever=double(scalp.weights[0])/double(scalp.weights[0]+scalp.weights[1])
                *glm::length(f.model.geometry->meshes[1].vertices[0].position-glm::dvec3(rest.global[2][3]));
            f.hair.chains[0].links[0].maxAngle=2*std::asin(.0015/(2*lever));}
        auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator automatic,explicitOn,explicitOff;SkeletalPose a,b,c;
        const auto reset=[&](HairSimulator& simulator,const char* value){ScopedHairCache option(value,"MODEL_HAIR_BONE_SURFACE_CONTACTS");return simulator.Reset(f.hair,f.model,pose,&error);};
        okay=okay&&reset(automatic,"")&&reset(explicitOn,"1")&&reset(explicitOff,"0");
        const auto warmContacts=automatic.Stats().contacts;legacyActive&=warmContacts>0&&explicitOff.Stats().contacts==0;
        for(int frame=0;okay&&frame<12;++frame){const double delta=frame?1./60.:0.;
            okay=automatic.Step(f.hair,f.model,pose,delta,f.hair.settings,a,&error)&&explicitOn.Step(f.hair,f.model,pose,delta,f.hair.settings,b,&error)
                &&explicitOff.Step(f.hair,f.model,pose,delta,f.hair.settings,c,&error);
            exact&=a.global==b.global&&automatic.Positions()==explicitOn.Positions()&&automatic.SurfaceOffsets()==explicitOn.SurfaceOffsets()
                &&automatic.Stats().maxPenetration==explicitOn.Stats().maxPenetration;
            if(missingSurface)fallback&=automatic.SurfaceOffsets().empty()&&automatic.Stats().surfaceVertices==0;
            fallback&=automatic.Stats().maxRootError<1e-11&&automatic.Stats().maxAttachmentDrift<=.0015+1e-10;
        }
    }
    Expect(okay&&exact&&legacyActive,"Unattached legacy assets and attached assets without a hair mesh table retain the exact explicit-1 skeletal solver");
    Expect(okay&&fallback,"Missing fine geometry stays a no-op surface layer while skeletal contact, roots and attachment bounds remain valid");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void BoneAnchorContactOption(){
    std::puts("Experimental anchor-surface filtering retains coarse torso steering and fine head contact:");
    ScopedHairCache enableBoneContacts("1","MODEL_HAIR_BONE_SURFACE_CONTACTS");
    bool okay=true,defaultExact=true,filteredHead=true,retainedTorso=true,fineContact=true;std::string error;
    for(const float headWeight:{0.f,.5f,1.f}){auto f=Make();f.hair.settings.gravity={0,0,0};
        for(auto& weights:f.model.skins[0].vertices){weights.joints={1,0,0,0};weights.weights={headWeight,1-headWeight,0,0};}
        const auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator standard,explicitOn,filtered,adaptiveFiltered;SkeletalPose a,b,c,d;
        const auto reset=[&](HairSimulator& simulator,const char* anchor,const char* adaptive){
            ScopedHairCache option(anchor,"MODEL_HAIR_BONE_ANCHOR_CONTACTS"),refine(adaptive,"MODEL_HAIR_ADAPTIVE_CONTACTS");
            return simulator.Reset(f.hair,f.model,pose,&error);};
        okay=okay&&reset(standard,"","0")&&reset(explicitOn,"1","0")&&reset(filtered,"0","0")&&reset(adaptiveFiltered,"0","1");
        // Step clears per-call contact counters even at zero delta. Inspect
        // reset steering before its presentation call clears that statistic.
        const auto resetContacts=filtered.Stats().contacts;
        okay=okay&&filtered.Step(f.hair,f.model,pose,0,f.hair.settings,c,&error)&&adaptiveFiltered.Step(f.hair,f.model,pose,0,f.hair.settings,d,&error);
        detail::HairBodyCollision independent;okay=okay&&independent.Initialize(f.model,f.hair.hairMeshes,1,&error)&&independent.Update(pose,&error);
        if(!okay)break;
        if(headWeight>=.5f)filteredHead&=c.global==pose.global&&d.global==pose.global&&filtered.Stats().contacts==0
            &&adaptiveFiltered.Stats().contacts==0&&filtered.Stats().maxPenetration>.009;
        else retainedTorso&=resetContacts>0&&c.global!=pose.global;
        for(int frame=0;okay&&frame<30;++frame){okay=standard.Step(f.hair,f.model,pose,1./60.,f.hair.settings,a,&error)
                &&explicitOn.Step(f.hair,f.model,pose,1./60.,f.hair.settings,b,&error)
                &&filtered.Step(f.hair,f.model,pose,1./60.,f.hair.settings,c,&error)
                &&adaptiveFiltered.Step(f.hair,f.model,pose,1./60.,f.hair.settings,d,&error);
            defaultExact&=a.global==b.global&&standard.Positions()==explicitOn.Positions()&&standard.SurfaceOffsets()==explicitOn.SurfaceOffsets();
            if(headWeight>=.5f)filteredHead&=filtered.Stats().contacts==0&&adaptiveFiltered.Stats().contacts==0
                &&filtered.Stats().maxTipDisplacement<1e-10&&adaptiveFiltered.Stats().maxTipDisplacement<1e-10
                &&c.global==d.global&&filtered.Stats().maxRootError<1e-11&&filtered.Stats().maxPenetration>.009;
            else retainedTorso&=a.global==c.global&&standard.Positions()==filtered.Positions()&&standard.SurfaceOffsets()==filtered.SurfaceOffsets();
            StaticModel corrected;okay=okay&&SkinSkeletalMesh(f.model,c,corrected,&error)&&ApplyHairSurfaceOffsets(f.model,c,filtered.SurfaceOffsets(),corrected,&error);
            if(okay)for(const auto& vertex:corrected.meshes[1].vertices){const auto nearest=independent.Nearest(vertex.position,.08);
                fineContact&=Finite(vertex.position)&&(!nearest.found||f.hair.settings.thickness-nearest.signedDistance<.005);}
        }
    }
    Expect(okay&&defaultExact,"Unset anchor filtering and explicit 1 preserve identical default poses and mesh corrections");
    Expect(okay&&filteredHead,"Triangles with at least half anchor weight cannot drive skeletal rotation, including adaptive correction; raw overlap remains reported");
    Expect(okay&&retainedTorso,"Filtering anchor surfaces preserves torso-driven skeletal steering and its exact unfiltered result");
    Expect(okay&&fineContact,"Fine rendered-mesh contact still repairs Head and torso surfaces without excluding anchor geometry");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void BoneFreeProbeOption(){
    std::puts("Experimental distal-majority skeletal contact probes retain complete mesh diagnostics:");
    ScopedHairCache enableBone("1","MODEL_HAIR_BONE_SURFACE_CONTACTS"),enableAnchor("1","MODEL_HAIR_BONE_ANCHOR_CONTACTS");
    bool okay=true,defaultExact=true,rejectedRoot=true,retainedFree=true,fineContact=true;std::string error;
    for(const float distal:{0.f,.49f,.5f,1.f}){auto f=Make();f.hair.settings.gravity={0,0,0};f.hair.probes.clear();
        for(std::uint32_t vertex=0;vertex<3;++vertex){auto& skin=f.model.skins[1].vertices[vertex];skin.joints={3,2,0,0};skin.weights={distal,1-distal,0,0};
            const double sum=double(skin.weights[0])+double(skin.weights[1]);HairSurfaceProbe probe;probe.mesh=1;probe.vertex=vertex;probe.chain=0;
            for(int k=0;k<2;++k)if(skin.weights[k]>0){const auto bone=skin.joints[k];const double weight=double(skin.weights[k])/sum;
                probe.influences.push_back({bone,weight,glm::dvec3(f.model.skeleton.bones[bone].inverseBind*glm::dvec4(f.model.geometry->meshes[1].vertices[vertex].position,1))*weight});}
            f.hair.probes.push_back(std::move(probe));}
        const auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator standard,explicitOff,filtered,adaptiveFiltered;SkeletalPose a,b,c,d;
        const auto reset=[&](HairSimulator& simulator,const char* free,const char* adaptive){ScopedHairCache option(free,"MODEL_HAIR_BONE_FREE_PROBES"),refine(adaptive,"MODEL_HAIR_ADAPTIVE_CONTACTS");
            return simulator.Reset(f.hair,f.model,pose,&error);};
        okay=okay&&reset(standard,"","0")&&reset(explicitOff,"0","0")&&reset(filtered,"1","0")&&reset(adaptiveFiltered,"1","1");
        const auto warmContacts=filtered.Stats().contacts;
        okay=okay&&filtered.Step(f.hair,f.model,pose,0,f.hair.settings,c,&error)&&adaptiveFiltered.Step(f.hair,f.model,pose,0,f.hair.settings,d,&error);
        detail::HairBodyCollision independent;okay=okay&&independent.Initialize(f.model,f.hair.hairMeshes,1,&error)&&independent.Update(pose,&error);if(!okay)break;
        if(distal<.5f)rejectedRoot&=warmContacts==0&&c.global==pose.global&&d.global==pose.global&&filtered.Stats().maxPenetration>.009;
        else retainedFree&=warmContacts>0&&c.global!=pose.global;
        for(int frame=0;okay&&frame<12;++frame){okay=standard.Step(f.hair,f.model,pose,1./60.,f.hair.settings,a,&error)
                &&explicitOff.Step(f.hair,f.model,pose,1./60.,f.hair.settings,b,&error)&&filtered.Step(f.hair,f.model,pose,1./60.,f.hair.settings,c,&error)
                &&adaptiveFiltered.Step(f.hair,f.model,pose,1./60.,f.hair.settings,d,&error);
            defaultExact&=a.global==b.global&&standard.Positions()==explicitOff.Positions()&&standard.SurfaceOffsets()==explicitOff.SurfaceOffsets();
            if(distal<.5f)rejectedRoot&=filtered.Stats().contacts==0&&adaptiveFiltered.Stats().contacts==0&&c.global==d.global
                &&filtered.Stats().maxTipDisplacement<1e-10&&adaptiveFiltered.Stats().maxTipDisplacement<1e-10
                &&filtered.Stats().probes==3&&filtered.Stats().maxPenetration>.009&&filtered.Stats().maxRootError<1e-11;
            else retainedFree&=a.global==c.global&&standard.Positions()==filtered.Positions()&&standard.SurfaceOffsets()==filtered.SurfaceOffsets();
            StaticModel corrected;okay=okay&&SkinSkeletalMesh(f.model,c,corrected,&error)&&ApplyHairSurfaceOffsets(f.model,c,filtered.SurfaceOffsets(),corrected,&error);
            if(okay)for(const auto& vertex:corrected.meshes[1].vertices){const auto nearest=independent.Nearest(vertex.position,.08);
                fineContact&=Finite(vertex.position)&&(!nearest.found||f.hair.settings.thickness-nearest.signedDistance<.005);}
        }
    }
    Expect(okay&&defaultExact,"Unset distal filtering and explicit 0 preserve identical existing poses and mesh corrections");
    Expect(okay&&rejectedRoot,"Root-only and 49 percent distal probes cannot rotate strands through either contact path; all raw probes remain measured");
    Expect(okay&&retainedFree,"Exactly 50 percent and fully distal probes retain torso steering and the exact unfiltered skeletal result");
    Expect(okay&&fineContact,"Fine contact still repairs every actual skinned free vertex, including regions excluded from coarse steering");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void AttachmentContactPolicy(){
    std::puts("Attachment-aware default coarse steering and complete rendered-surface collision:");
    ScopedHairCache enableBone("1","MODEL_HAIR_BONE_SURFACE_CONTACTS"),adaptiveOff("0","MODEL_HAIR_ADAPTIVE_CONTACTS");
    bool okay=true,matching=true,policy=true,rawMeasured=true,fineContact=true,attached=true;std::string error;
    for(int fixture=0;fixture<4&&okay;++fixture){auto f=Make();f.hair.settings.gravity={0,0,0};const bool hasAttachments=fixture!=0,headBody=fixture<2,rootOnly=fixture==2;
        if(headBody)for(auto& weights:f.model.skins[0].vertices){weights.joints={1,0,0,0};weights.weights={1,0,0,0};}
        if(rootOnly)for(auto& weights:f.model.skins[1].vertices){weights.joints={2,0,0,0};weights.weights={1,0,0,0};}
        f.hair.probes.clear();
        for(std::uint32_t vertex=hasAttachments?1u:0u;vertex<3;++vertex){const auto bone=rootOnly?2u:vertex+2;HairSurfaceProbe probe;probe.mesh=1;probe.vertex=vertex;probe.chain=0;
            probe.influences.push_back({bone,1,glm::dvec3(f.model.skeleton.bones[bone].inverseBind*glm::dvec4(f.model.geometry->meshes[1].vertices[vertex].position,1))});f.hair.probes.push_back(std::move(probe));}
        auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);
        if(hasAttachments){auto& skin=f.model.skins[1].vertices[0];skin.joints={2,1,0,0};skin.weights={.4f,.6f,0,0};f.hair.attachments={{1,0,0}};
            const double lever=double(skin.weights[0])/double(skin.weights[0]+skin.weights[1])*glm::length(f.model.geometry->meshes[1].vertices[0].position-glm::dvec3(pose.global[2][3]));
            f.hair.chains[0].links[0].maxAngle=2*std::asin(.0015/(2*lever));}
        HairSimulator automatic,explicitPolicy,legacy;SkeletalPose a,b,c;
        const auto reset=[&](HairSimulator& simulator,const char* anchor,const char* free){ScopedHairCache anchorOption(anchor,"MODEL_HAIR_BONE_ANCHOR_CONTACTS"),freeOption(free,"MODEL_HAIR_BONE_FREE_PROBES");
            return simulator.Reset(f.hair,f.model,pose,&error);};
        okay=reset(automatic,"","")&&reset(explicitPolicy,hasAttachments?"0":"1",hasAttachments?"1":"0")&&reset(legacy,"1","0");
        const auto automaticWarm=automatic.Stats().contacts,legacyWarm=legacy.Stats().contacts;
        const bool suppressesCoarse=hasAttachments&&(headBody||rootOnly);
        policy&=suppressesCoarse?(automaticWarm==0&&legacyWarm>0):automaticWarm>0;
        detail::HairBodyCollision independent;okay=okay&&independent.Initialize(f.model,f.hair.hairMeshes,1,&error)&&independent.Update(pose,&error);
        for(int frame=0;okay&&frame<18;++frame){const double dt=frame?1./60.:0.;
            okay=automatic.Step(f.hair,f.model,pose,dt,f.hair.settings,a,&error)&&explicitPolicy.Step(f.hair,f.model,pose,dt,f.hair.settings,b,&error)
                &&legacy.Step(f.hair,f.model,pose,dt,f.hair.settings,c,&error);
            matching&=a.global==b.global&&automatic.Positions()==explicitPolicy.Positions()&&automatic.SurfaceOffsets()==explicitPolicy.SurfaceOffsets()
                &&automatic.Stats().maxPenetration==explicitPolicy.Stats().maxPenetration;
            if(suppressesCoarse)rawMeasured&=automatic.Stats().probes==f.hair.probes.size()&&automatic.Stats().maxPenetration>.009;
            attached&=automatic.Stats().maxRootError<1e-11&&automatic.Stats().maxAttachmentDrift<=.0015+1e-10;
            StaticModel corrected;okay=okay&&SkinSkeletalMesh(f.model,a,corrected,&error)&&ApplyHairSurfaceOffsets(f.model,a,automatic.SurfaceOffsets(),corrected,&error);
            if(okay)for(std::uint32_t vertex=hasAttachments?1u:0u;vertex<3;++vertex){const auto point=corrected.meshes[1].vertices[vertex].position;const auto nearest=independent.Nearest(point,.08);
                fineContact&=Finite(point)&&(!nearest.found||f.hair.settings.thickness-nearest.signedDistance<.005);}
        }
    }
    Expect(okay&&matching,"Default policies match explicit 0/1 settings bitwise for assets with and without authored attachments");
    Expect(okay&&policy,"Attachments restrain Head/root-driven steering while unattached assets and free torso probes retain coarse collision; overrides restore legacy steering");
    Expect(okay&&rawMeasured&&attached,"Coarse filtering keeps every raw probe measurable with exact roots and bounded scalp attachment drift");
    Expect(okay&&fineContact,"Every free rendered vertex still receives complete Head and torso contact under the automatic policy");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void ExactProbeMeasurements(){
    std::puts("Exact probe measurements across unchanged geometry and complete contact keys:");
    ScopedHairCache noSleep("0","MODEL_HAIR_SLEEP"),meshOnly("0","MODEL_HAIR_BONE_SURFACE_CONTACTS");
    auto f=Make();f.hair.settings.gravity={0,0,0};
    for(auto& skin:f.model.skins[0].vertices){skin.joints={5,0,0,0};skin.weights={1,0,0,0};}
    for(std::uint32_t v=0;v<f.model.geometry->meshes[0].vertices.size();++v)f.model.morphs[0].deltas.push_back({0,v,{0,0,.002}});
    MorphTarget hairMorph;hairMorph.name="independent hair query motion";
    for(std::uint32_t v=0;v<3;++v)hairMorph.deltas.push_back({1,v,{.001,0,0}});f.model.morphs.push_back(std::move(hairMorph));
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator cached,reference;SkeletalPose a,b;std::string error;bool okay=true;
    {ScopedHairCache option("1","MODEL_HAIR_MEASUREMENT_REUSE");okay=cached.Reset(f.hair,f.model,pose,&error);}
    {ScopedHairCache option("0","MODEL_HAIR_MEASUREMENT_REUSE");okay=okay&&reference.Reset(f.hair,f.model,pose,&error);}
    bool exact=true,raw=true,invalidated=true,zeroTick=true;std::size_t queries=0,referenceQueries=0,reuses=0;
    for(int frame=0;okay&&frame<18;++frame){auto settings=f.hair.settings;
        if(frame==4)settings.thickness=.002;
        if(frame==6)settings.collision=false;
        pose.local[5].translation.z=.2+(frame==8?.002:0.);pose.morphWeights={frame==10?1.f:0.f,frame==12?1.f:0.f};
        pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
        okay=cached.Step(f.hair,f.model,pose,1./60.,settings,a,&error)&&reference.Step(f.hair,f.model,pose,1./60.,settings,b,&error);
        exact&=a.global==b.global&&a.morphWeights==b.morphWeights&&cached.Positions()==reference.Positions()&&cached.SurfaceOffsets()==reference.SurfaceOffsets()
            &&cached.Stats().maxPenetration==reference.Stats().maxPenetration&&cached.Stats().worstPenetrationProbe==reference.Stats().worstPenetrationProbe;
        raw&=settings.collision?cached.Stats().maxPenetration>.009:cached.Stats().maxPenetration==0;
        if(frame>=4&&frame<=13)invalidated&=cached.Stats().measurementReuses==0;
        queries+=cached.Stats().collisionQueries;referenceQueries+=reference.Stats().collisionQueries;reuses+=cached.Stats().measurementReuses;
        const auto nodes=cached.Positions();const auto offsets=cached.SurfaceOffsets();const auto steps=cached.Stats().steps;
        okay=okay&&cached.Step(f.hair,f.model,pose,0,settings,a,&error);
        zeroTick&=cached.Stats().measurementReuses==0&&cached.Stats().collisionQueries==0&&cached.Stats().steps==steps
            &&cached.Positions()==nodes&&cached.SurfaceOffsets()==offsets;
    }
    std::printf("    Identical measurement query work cached/reference %zu/%zu; exact reused observations %zu\n",queries,referenceQueries,reuses);
    okay=okay&&cached.Reset(f.hair,f.model,pose,&error);
    const bool resetFresh=cached.Stats().measurementReuses==0&&cached.Stats().collisionQueries>=f.hair.probes.size()&&cached.Stats().steps==0;
    Expect(okay&&exact&&raw,"Cached inside depths, worst-probe IDs, output bones and actual mesh offsets remain exactly equal to fresh measurement");
    Expect(okay&&invalidated,"Thickness/collision changes, actual body motion, body morphs and moving hair query points invalidate the appropriate key");
    Expect(okay&&reuses>0&&queries<referenceQueries&&zeroTick&&resetFresh,"Unchanged geometry avoids redundant queries; zero-time stays idle and explicit Reset invalidates all measurement history");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void SettledMeshSleep(){
    std::puts("Convergence-gated mesh-only sleep, truthful clocks and complete input wakeup:");
    auto f=Make();f.hair.settings.gravity={0,0,0};
    auto& scalp=f.model.skins[1].vertices[0];scalp.joints={2,1,0,0};scalp.weights={.4f,.6f,0,0};
    f.hair.attachments={{1,0,0}};f.hair.probes.erase(f.hair.probes.begin());
    for(auto& skin:f.model.skins[0].vertices){skin.joints={5,0,0,0};skin.weights={1,0,0,0};}
    for(std::uint32_t v=0;v<f.model.geometry->meshes[0].vertices.size();++v)f.model.morphs[0].deltas.push_back({0,v,{0,0,.002}});
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);const double lever=double(scalp.weights[0])/double(scalp.weights[0]+scalp.weights[1])
        *glm::length(f.model.geometry->meshes[1].vertices[0].position-glm::dvec3(pose.global[2][3]));f.hair.chains[0].links[0].maxAngle=2*std::asin(.0015/(2*lever));
    auto settings=f.hair.settings;HairSimulator sleeping,reference;SkeletalPose a,b;std::string error;bool okay=true;
    {ScopedHairCache option("1","MODEL_HAIR_SLEEP");okay=sleeping.Reset(f.hair,f.model,pose,&error);}
    {ScopedHairCache option("0","MODEL_HAIR_SLEEP");okay=okay&&reference.Reset(f.hair,f.model,pose,&error);}
    detail::HairBodyCollision body;okay=okay&&body.Initialize(f.model,f.hair.hairMeshes,1,&error);
    bool equal=true,raw=true,wakes=true,collision=true,stableGate=true;std::size_t sleepWork=0;
    auto tick=[&](){if(!okay)return;const auto oldSteps=sleeping.Stats().steps;
        okay=sleeping.Step(f.hair,f.model,pose,1./60.,settings,a,&error)&&reference.Step(f.hair,f.model,pose,1./60.,settings,b,&error);
        equal&=sleeping.Stats().steps==oldSteps+1&&sleeping.Stats().steps==reference.Stats().steps;
        if(sleeping.Positions().size()!=reference.Positions().size())equal=false;
        else for(std::size_t i=0;i<sleeping.Positions().size();++i)equal&=glm::length(sleeping.Positions()[i]-reference.Positions()[i])<1e-6;
        if(sleeping.SurfaceOffsets().size()!=reference.SurfaceOffsets().size())equal=false;
        else for(std::size_t i=0;i<sleeping.SurfaceOffsets().size();++i){const auto& x=sleeping.SurfaceOffsets()[i];const auto& y=reference.SurfaceOffsets()[i];
            equal&=x.mesh==y.mesh&&x.vertex==y.vertex&&x.anchorBone==y.anchorBone&&glm::length(x.displacement-y.displacement)<1e-6;}
        raw&=!settings.collision||sleeping.Stats().maxPenetration>.009;
        if(sleeping.Stats().sleeping&&sleeping.Stats().sleepTicks>0){sleepWork+=sleeping.Stats().collisionQueries+sleeping.Stats().surfaceSweeps;
            stableGate&=sleeping.Stats().maxSurfacePenetration<=1e-6;}
    };
    auto settle=[&](){for(int i=0;okay&&!sleeping.Stats().sleeping&&i<900;++i)tick();return okay&&sleeping.Stats().sleeping;};
    for(int i=0;okay&&i<180;++i){tick();if(i<59)stableGate&=!sleeping.Stats().sleeping;}
    const bool slept=okay&&sleeping.Stats().sleeping&&sleeping.Stats().sleepTicks>30&&sleepWork==0;
    const auto heldSteps=sleeping.Stats().steps,heldSleeps=sleeping.Stats().sleepTicks;const auto heldOffsets=sleeping.SurfaceOffsets();
    okay=okay&&sleeping.Step(f.hair,f.model,pose,0,settings,a,&error);
    const bool zeroHeld=sleeping.Stats().sleeping&&sleeping.Stats().steps==heldSteps&&sleeping.Stats().sleepTicks==heldSleeps&&sleeping.SurfaceOffsets()==heldOffsets;
    bool movingMeshAwake=true,meshMoved=false;auto previousOffsets=sleeping.SurfaceOffsets();
    for(int frame=0;okay&&frame<24;++frame){pose.local[5].translation.z=.2+.002*std::sin((frame+1)*.13);pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);tick();
        movingMeshAwake&=!sleeping.Stats().sleeping&&sleeping.Stats().sleepTicks==heldSleeps;
        meshMoved|=sleeping.SurfaceOffsets()!=previousOffsets;previousOffsets=sleeping.SurfaceOffsets();}
    for(int change=0;okay&&change<6;++change){wakes&=settle();const auto oldSleeps=sleeping.Stats().sleepTicks;
        if(change==0)pose.local[5].translation.z+=.002;
        if(change==1)pose.morphWeights={.5f};
        if(change==2)pose.local[1].translation.x+=.001;
        if(change==3)settings.friction+=.01;
        if(change==4)settings.wind.x=.0001;
        if(change==5){auto transient=pose;transient.local[1].translation.x+=.001;transient.global=GlobalBoneTransforms(f.model.skeleton,transient.local);
            okay=sleeping.Step(f.hair,f.model,transient,0,settings,a,&error)&&sleeping.Step(f.hair,f.model,pose,0,settings,a,&error);
            wakes&=!sleeping.Stats().sleeping;}
        pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);tick();wakes&=!sleeping.Stats().sleeping&&sleeping.Stats().sleepTicks==oldSleeps;
        if(change==4)settings.wind.x=0;
        // The following physical frame must be checked even after waking;
        // sleep never excuses actual corrected render-surface penetration.
        StaticModel mesh;okay=okay&&body.Update(pose,&error)&&SkinSkeletalMesh(f.model,a,mesh,&error)&&ApplyHairSurfaceOffsets(f.model,a,sleeping.SurfaceOffsets(),mesh,&error);
        if(okay)for(std::uint32_t v=1;v<3;++v){const auto near=body.Nearest(mesh.meshes[1].vertices[v].position,.08);collision&=!near.found||settings.thickness-near.signedDistance<.005;}
    }
    bool legacyAwake=true;
    for(const bool missingSurface:{false,true}){auto legacy=Make();legacy.hair.settings.gravity={0,0,0};
        if(missingSurface){legacy.hair.hairMeshes.clear();legacy.hair.attachments={{1,0,0}};legacy.hair.chains[0].links[0].maxAngle=.0001;}
        HairSimulator fallback;auto legacyPose=EvaluateSkeletalPose(legacy.model,nullptr,0,false);okay=okay&&fallback.Reset(legacy.hair,legacy.model,legacyPose,&error);
        for(int i=0;okay&&i<90;++i)okay=fallback.Step(legacy.hair,legacy.model,legacyPose,1./60.,legacy.hair.settings,b,&error);
        legacyAwake&=!fallback.Stats().sleeping&&fallback.Stats().sleepTicks==0;
    }
    wakes&=settle();const auto resets=sleeping.Stats().resets;okay=okay&&sleeping.Reset(f.hair,f.model,pose,&error);
    const bool resetAwake=!sleeping.Stats().sleeping&&sleeping.Stats().steps==0&&sleeping.Stats().sleepTicks==0&&sleeping.Stats().resets==resets+1;
    Expect(okay&&slept&&stableGate&&zeroHeld,"Only a continuously converged real mesh-contact state sleeps after at least one second; sleep performs no collision queries");
    Expect(okay&&equal&&raw&&collision,"Logical ticks remain coherent with the active reference within one micrometer and preserve raw overlap plus actual 5 mm contact quality");
    Expect(okay&&wakes&&resetAwake,"Body, morph, anchor, configuration, wind and transient Step(0) edits wake without resetting the clock; explicit Reset alone clears it");
    Expect(okay&&movingMeshAwake&&meshMoved,"A continuously moving fine-contact displacement never sleeps even while the hair bones remain stationary");
    Expect(okay&&legacyAwake,"Legacy skeletal-contact assets remain active and cannot use sleep to hide unresolved coarse dynamics");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void LocalJointConstraintStability(){
    std::puts("One parent-local angular cone with cumulative bend and a held endpoint:");
    auto f=Make(1,false);for(auto& link:f.hair.chains[0].links){link.maxAngle=glm::radians(20.);link.stiffness=0;link.damping=7;}
    auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);pose.local[1].rotation=glm::angleAxis(glm::radians(80.),glm::dvec3(0,0,1));
    pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
    HairSimulator automatic,explicitLocal,legacy;SkeletalPose a,b,c;std::string error;bool okay=true;
    {ScopedHairCache local("","MODEL_HAIR_LOCAL_JOINT_LIMITS");okay=automatic.Reset(f.hair,f.model,pose,&error);}
    {ScopedHairCache local("1","MODEL_HAIR_LOCAL_JOINT_LIMITS");okay=okay&&explicitLocal.Reset(f.hair,f.model,pose,&error);}
    {ScopedHairCache local("0","MODEL_HAIR_LOCAL_JOINT_LIMITS");okay=okay&&legacy.Reset(f.hair,f.model,pose,&error);}
    bool exact=true,bounded=true,bodyExact=true;double cumulative=0,legacyCumulative=0,lateSpeed=0;
    auto previous=automatic.Positions();std::vector<glm::dvec3> lo(previous.size(),glm::dvec3(std::numeric_limits<double>::infinity())),hi(previous.size(),glm::dvec3(-std::numeric_limits<double>::infinity()));
    for(int frame=0;okay&&frame<720;++frame){
        okay=automatic.Step(f.hair,f.model,pose,1./60.,f.hair.settings,a,&error)&&explicitLocal.Step(f.hair,f.model,pose,1./60.,f.hair.settings,b,&error)
            &&legacy.Step(f.hair,f.model,pose,1./60.,f.hair.settings,c,&error);
        exact&=a.global==b.global&&automatic.Positions()==explicitLocal.Positions();
        for(const auto& link:f.hair.chains[0].links){auto delta=glm::normalize(a.local[link.bone].rotation*glm::inverse(pose.local[link.bone].rotation));
            bounded&=2*std::acos(std::clamp(std::abs(delta.w),0.,1.))<=link.maxAngle+1e-10;}
        for(const auto bone:{0u,1u,5u})bodyExact&=a.global[bone]==pose.global[bone];
        const auto& current=automatic.Positions();
        if(frame>=600)for(std::size_t node=1;node<current.size();++node){lo[node]=glm::min(lo[node],current[node]);hi[node]=glm::max(hi[node],current[node]);
            lateSpeed=std::max(lateSpeed,glm::length(current[node]-previous[node])*60);}
        previous=current;
    }
    if(okay){const auto& link=f.hair.chains[0].links.back();const auto source=glm::normalize(glm::dvec3(pose.global[link.bone]*glm::dvec4(link.restTip,0)));
        cumulative=std::acos(std::clamp(glm::dot(source,glm::normalize(glm::dvec3(a.global[link.bone]*glm::dvec4(link.restTip,0)))),-1.,1.));
        legacyCumulative=std::acos(std::clamp(glm::dot(source,glm::normalize(glm::dvec3(c.global[link.bone]*glm::dvec4(link.restTip,0)))),-1.,1.));}
    double range=0;for(std::size_t node=1;node<lo.size();++node)if(Finite(lo[node])&&Finite(hi[node]))range=std::max(range,glm::length(hi[node]-lo[node]));
    std::printf("    Distal world deviation local/reference %.3f/%.3f deg; held range %.6f mm, speed %.9f m/s\n",glm::degrees(cumulative),glm::degrees(legacyCumulative),range*1000,lateSpeed);
    Expect(okay&&exact,"Default and explicit local joint projection produce identical physical trajectories");
    Expect(okay&&bounded&&bodyExact&&automatic.Stats().maxRootError<1e-11,"Cumulative bending preserves every individual local angle limit and all animated non-hair bones");
    Expect(okay&&cumulative>glm::radians(25.),"Successive valid local bends are not pulled back into a second animated world-space cone");
    Expect(okay&&range<.0005&&lateSpeed<.005,"The same joint constraints settle under a constant force without introducing kinetic energy");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void ContactVelocityStability(){
    std::puts("Zero-restitution contact velocity and long held-body settling at 30/60 Hz:");
    ScopedHairCache adaptiveOff("0","MODEL_HAIR_ADAPTIVE_CONTACTS"),enableBoneContacts("1","MODEL_HAIR_BONE_SURFACE_CONTACTS");std::string error;bool okay=true,unchangedFree=true;
    {auto f=Make(1,false);auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);HairSimulator corrected,reference;SkeletalPose a,b;
        {ScopedHairCache option("1","MODEL_HAIR_STABLE_CONTACT_VELOCITY");okay=corrected.Reset(f.hair,f.model,pose,&error);}
        {ScopedHairCache option("0","MODEL_HAIR_STABLE_CONTACT_VELOCITY");okay=okay&&reference.Reset(f.hair,f.model,pose,&error);}
        for(int frame=0;okay&&frame<30;++frame){pose.local[1].translation.x=.05*std::sin(frame*.1);pose.local[1].rotation=glm::angleAxis(.1*std::sin(frame*.07),glm::dvec3(0,0,1));
            pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);okay=corrected.Step(f.hair,f.model,pose,1./60.,f.hair.settings,a,&error)
                &&reference.Step(f.hair,f.model,pose,1./60.,f.hair.settings,b,&error);unchangedFree&=a.global==b.global&&corrected.Positions()==reference.Positions();}}
    Expect(okay&&unchangedFree,"Separating geometric contact correction changes no non-contact animated inertia or node trajectory");
    bool settled=true,decayed=true,collision=true,attachment=true,bodyExact=true,pausedExact=true;std::size_t contacts=0;
    for(const double fixed:{1./60.,1./30.}){auto f=Make();f.hair.settings.fixedTimeStep=fixed;f.hair.settings.substeps=fixed>.02?4:2;
        for(auto& link:f.hair.chains[0].links){link.maxAngle=glm::radians(20.);link.stiffness=12;link.damping=7;}
        auto& vertices=f.model.geometry->meshes[1].vertices;vertices[0].position={.02,.95,.02};vertices[1].position={.06,.75,.02};vertices[2].position={.04,.55,.02};
        for(auto& skin:f.model.skins[0].vertices){skin.joints={5,0,0,0};skin.weights={1,0,0,0};}
        auto& scalp=f.model.skins[1].vertices[0];scalp.joints={2,1,0,0};scalp.weights={.4f,.6f,0,0};f.hair.attachments={{1,0,0}};f.hair.probes.clear();
        for(std::uint32_t vertex=1;vertex<3;++vertex){const auto bone=vertex+2;HairSurfaceProbe probe;probe.mesh=1;probe.vertex=vertex;probe.chain=0;
            probe.influences.push_back({bone,1,glm::dvec3(f.model.skeleton.bones[bone].inverseBind*glm::dvec4(vertices[vertex].position,1))});f.hair.probes.push_back(std::move(probe));}
        auto pose=EvaluateSkeletalPose(f.model,nullptr,0,false);const double lever=double(scalp.weights[0])/double(scalp.weights[0]+scalp.weights[1])
            *glm::length(vertices[0].position-glm::dvec3(pose.global[2][3]));f.hair.chains[0].links[0].maxAngle=std::min(f.hair.chains[0].links[0].maxAngle,2*std::asin(.0015/(2*lever)));
        HairSimulator corrected,reference;SkeletalPose a,b;
        {ScopedHairCache option("1","MODEL_HAIR_STABLE_CONTACT_VELOCITY");okay=okay&&corrected.Reset(f.hair,f.model,pose,&error);}
        {ScopedHairCache option("0","MODEL_HAIR_STABLE_CONTACT_VELOCITY");okay=okay&&reference.Reset(f.hair,f.model,pose,&error);}
        detail::HairBodyCollision independent;okay=okay&&independent.Initialize(f.model,f.hair.hairMeshes,1,&error);
        auto previous=corrected.Positions(),previousReference=reference.Positions();
        std::vector<glm::dvec3> lo(previous.size(),glm::dvec3(std::numeric_limits<double>::infinity())),hi(previous.size(),glm::dvec3(-std::numeric_limits<double>::infinity()));
        double earlyEnergy=0,lateEnergy=0,referenceLateEnergy=0,peakMotion=0;std::size_t earlySamples=0,lateSamples=0;StaticModel rendered;
        const auto ticks=std::size_t(std::llround(12./fixed));
        for(std::size_t frame=1;okay&&frame<=ticks;++frame){const double time=double(frame)*fixed;
            // A separate body part enters stationary hair once, then remains
            // fixed for nearly twelve seconds. A contact projection may move
            // nodes, but cannot launch a new rebound on every held tick.
            pose.local[5].translation.z=.2+(time>=.2?.04:0.);pose.global=GlobalBoneTransforms(f.model.skeleton,pose.local);
            okay=corrected.Step(f.hair,f.model,pose,fixed,f.hair.settings,a,&error)&&reference.Step(f.hair,f.model,pose,fixed,f.hair.settings,b,&error)
                &&independent.Update(pose,&error)&&SkinSkeletalMesh(f.model,a,rendered,&error)&&ApplyHairSurfaceOffsets(f.model,a,corrected.SurfaceOffsets(),rendered,&error);
            if(!okay)break;const auto& current=corrected.Positions();const auto& old=reference.Positions();
            for(std::size_t node=1;node<current.size();++node){const auto velocity=(current[node]-previous[node])/fixed,referenceVelocity=(old[node]-previousReference[node])/fixed;
                if(time>=.25&&time<2.25){earlyEnergy+=glm::dot(velocity,velocity);++earlySamples;}
                if(time>=10.){lateEnergy+=glm::dot(velocity,velocity);referenceLateEnergy+=glm::dot(referenceVelocity,referenceVelocity);++lateSamples;
                    lo[node]=glm::min(lo[node],current[node]);hi[node]=glm::max(hi[node],current[node]);}}
            previous=current;previousReference=old;contacts+=corrected.Stats().contacts;peakMotion=std::max(peakMotion,corrected.Stats().maxTipDisplacement);
            attachment&=corrected.Stats().maxRootError<1e-11&&corrected.Stats().maxAttachmentDrift<=.0015+1e-10;
            for(const auto bone:{0u,1u,5u})bodyExact&=a.global[bone]==pose.global[bone]&&a.local[bone].rotation==pose.local[bone].rotation;
            for(std::size_t vertex=1;vertex<3;++vertex){const auto nearest=independent.Nearest(rendered.meshes[1].vertices[vertex].position,.08);
                collision&=!nearest.found||f.hair.settings.thickness-nearest.signedDistance<.005;}}
        earlyEnergy/=double(std::max<std::size_t>(1,earlySamples));lateEnergy/=double(std::max<std::size_t>(1,lateSamples));referenceLateEnergy/=double(std::max<std::size_t>(1,lateSamples));
        double range=0;for(std::size_t node=1;node<lo.size();++node)if(Finite(lo[node])&&Finite(hi[node]))range=std::max(range,glm::length(hi[node]-lo[node]));
        std::printf("    %.0f Hz held tail range %.6f mm; early/late RMS %.6f/%.6f m/s; legacy late %.6f m/s\n",1/fixed,range*1000,std::sqrt(earlyEnergy),std::sqrt(lateEnergy),std::sqrt(referenceLateEnergy));
        settled&=lateSamples>0&&range<.0005&&std::sqrt(lateEnergy)<.005&&peakMotion>.005;
        decayed&=lateEnergy<=earlyEnergy*.05+1e-8;
        const auto held=corrected.Positions();const auto offsets=corrected.SurfaceOffsets();const auto steps=corrected.Stats().steps;
        okay=okay&&corrected.Step(f.hair,f.model,pose,0,f.hair.settings,a,&error);
        pausedExact&=corrected.Positions()==held&&corrected.SurfaceOffsets()==offsets&&corrected.Stats().steps==steps;
    }
    Expect(okay&&settled&&decayed,"A held collision settles below 0.5 mm tail range with decaying late kinetic energy at both 30 and 60 Hz");
    Expect(okay&&contacts>0&&collision&&attachment&&bodyExact,"Real moving-body contacts remain active with strict roots, scalp limits, exact animation and the unchanged 5 mm rendered-surface bound");
    Expect(okay&&pausedExact,"Zero-time presentation after contact settling preserves the exact physical and mesh state");
    if(!okay)std::printf("    %s\n",error.c_str());
}

int RealMotion(const std::filesystem::path& character,const std::filesystem::path& hairPath,const std::filesystem::path& animation){
    std::string error;auto target=LoadSkeletalAssetPackage(character,&error);if(!target){std::printf("Load model: %s\n",error.c_str());return 1;}
    auto hair=LoadHairAsset(hairPath,*target,&error);if(!hair){std::printf("Load hair: %s\n",error.c_str());return 1;}
    auto clip=LoadAnimationSequenceAsset(animation,*target,&error);if(!clip){std::printf("Load animation: %s\n",error.c_str());return 1;}
    HairSimulator simulator;detail::HairBodyCollision independent;auto pose=EvaluateSkeletalPose(*target,&*clip,0,false);SkeletalPose result;
    if(!simulator.Reset(*hair,*target,pose,&error)||!independent.Initialize(*target,hair->hairMeshes,hair->unitsPerMeter,&error)){std::printf("Initialize: %s\n",error.c_str());return 1;}
    std::vector<bool> simulated(target->skeleton.bones.size(),false);for(const auto& chain:hair->chains)for(const auto& link:chain.links)simulated[link.bone]=true;
    struct Sample {std::uint32_t mesh=0,vertex=0;glm::dmat4 instance{1};};std::vector<Sample> samples;
    for(const auto mesh:hair->hairMeshes)if(mesh<target->geometry->meshes.size()){
        auto instance=target->skins[mesh].bindTransform;for(const auto& i:target->geometry->instances)if(i.meshIndex==mesh){instance=i.transform;break;}
        for(std::uint32_t v=0;v<target->geometry->meshes[mesh].vertices.size();++v){const auto& skin=target->skins[mesh].vertices[v];bool dynamic=false;
            for(int k=0;k<4;++k)dynamic|=skin.weights[k]>0&&skin.joints[k]<simulated.size()&&simulated[skin.joints[k]];
            if(dynamic)samples.push_back({mesh,v,instance});}}
    double maximum=0,maximumProbe=0,totalMs=0,maxMs=0,maximumSource=0,maximumIncrease=0;
    std::size_t observations=0,inside=0;bool okay=true;StaticModel skinned,sourceSkinned;
    for(int frame=0;okay&&frame<=552;++frame){pose=EvaluateSkeletalPose(*target,&*clip,double(frame)/60,false);
        const auto start=std::chrono::steady_clock::now();okay=simulator.Step(*hair,*target,pose,1./60.,hair->settings,result,&error);
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();totalMs+=elapsed;maxMs=std::max(maxMs,elapsed);
        okay&=simulator.Stats().maxRootError<1e-8;maximumProbe=std::max(maximumProbe,simulator.Stats().maxPenetration);
        if(!okay||!independent.Update(result,&error)||!SkinSkeletalMesh(*target,result,skinned,&error)
            ||!ApplyHairSurfaceOffsets(*target,result,simulator.SurfaceOffsets(),skinned,&error)
            ||!SkinSkeletalMesh(*target,pose,sourceSkinned,&error)){okay=false;break;}
        // An independent full weighted-vertex scan intentionally belongs only
        // to this offline validation mode, never the interactive preview loop.
        for(const auto& sample:samples){const auto& vertex=skinned.meshes[sample.mesh].vertices[sample.vertex].position;
            const auto& sourceVertex=sourceSkinned.meshes[sample.mesh].vertices[sample.vertex].position;
            const auto point=glm::dvec3(sample.instance*glm::dvec4(vertex,1))/hair->unitsPerMeter;
            const auto sourcePoint=glm::dvec3(sample.instance*glm::dvec4(sourceVertex,1))/hair->unitsPerMeter;
            const auto near=independent.Nearest(point,.08),sourceNear=independent.Nearest(sourcePoint,.08);++observations;
            const double sourceDepth=sourceNear.found?std::max(0.,hair->settings.thickness-sourceNear.signedDistance):0.;maximumSource=std::max(maximumSource,sourceDepth);
            if(near.found){const double depth=std::max(0.,hair->settings.thickness-near.signedDistance);maximum=std::max(maximum,depth);inside+=near.signedDistance<0;
                maximumIncrease=std::max(maximumIncrease,depth-sourceDepth);}}
        if(frame%60==0)std::printf("frame %d probe %.3f mm all-vertex %.3f mm hair %.3f ms\n",frame,maximumProbe*1000,maximum*1000,elapsed);
    }
    std::printf("Real motion: %s, observations %zu, inside %zu, probe max %.3f mm, full weighted vertex max %.3f mm, source max %.3f mm, maximum per-vertex increase %.3f mm, mean/max solver %.3f/%.3f ms\n",okay?"complete":"FAILED",observations,inside,maximumProbe*1000,maximum*1000,maximumSource*1000,maximumIncrease*1000,totalMs/553,maxMs);
    if(!okay)std::printf("  %s\n",error.c_str());return okay?0:1;
}
}
int main(int argc,char** argv){
    if(argc==5&&std::string(argv[1])=="--real-motion")return RealMotion(argv[2],argv[3],argv[4]);
    InertiaAndDeterminism();SurfaceCollision();MovingBodyContact();Units();SubtickPresentation();ScalpAttachment();CatchUpSweep();SaturatedRootContact();ClearanceCertificates();AdaptiveContacts();PostSkinContacts();CoarseContactOption();BoneSurfaceContactOption();BoneContactFallbackPolicy();BoneAnchorContactOption();BoneFreeProbeOption();AttachmentContactPolicy();ExactProbeMeasurements();SettledMeshSleep();LocalJointConstraintStability();ContactVelocityStability();std::printf("Hair solver: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;
}
