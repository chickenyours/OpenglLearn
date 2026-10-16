#include "Model/Public/cloth_asset.h"
#include "Model/Public/vmd_animation.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace {
using namespace Model;
int failures=0;
bool Expect(bool condition,const char* message){std::printf("  %-88s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;}
bool Finite(glm::dvec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool Fixed(const ClothParticle& p){return p.inverseMass==0||p.maxDistance<=1e-12;}
struct Fixture {ClothAsset cloth;StaticModel mesh;SkeletalPose pose;};
Fixture Tube(){
    Fixture f;f.cloth.id="torso-attached-garment";f.cloth.motionBone=0;f.cloth.unitsPerMeter=1;f.cloth.settings.collision=false;f.cloth.settings.gravity={0,0,0};
    f.pose.global={glm::dmat4(1),glm::dmat4(1)};f.mesh.meshes.resize(1);f.mesh.instances.push_back({"garment",0,glm::dmat4(1)});
    constexpr int ring=12,rows=6;
    for(int y=0;y<rows;++y)for(int i=0;i<ring;++i){const double angle=i*6.2831853071795864769/ring;const glm::dvec3 p(.18*std::cos(angle),1-y*.08,.18*std::sin(angle));const auto index=std::uint32_t(f.cloth.particles.size());
        f.cloth.particles.push_back({p,y?1.:0.,y?1.2:0.,0});StaticVertex vertex;vertex.position=p;f.mesh.meshes[0].vertices.push_back(vertex);f.cloth.renderBindings.push_back({0,index,index});}
    std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
    for(int y=0;y<rows-1;++y)for(int i=0;i<ring;++i){const std::uint32_t a=y*ring+i,b=y*ring+(i+1)%ring,c=a+ring,d=b+ring;f.cloth.triangles.push_back({a,b,c});f.cloth.triangles.push_back({b,d,c});}
    for(const auto& tri:f.cloth.triangles)for(int e=0;e<3;++e){auto a=tri[e],b=tri[(e+1)%3];if(a>b)std::swap(a,b);edges.insert({a,b});}
    for(const auto [a,b]:edges)f.cloth.stretch.push_back({a,b,glm::length(f.cloth.particles[a].restPosition-f.cloth.particles[b].restPosition)});
    return f;
}
void PosePins(Fixture& f,const glm::dmat4& torso,bool nonrigid=false){
    for(std::size_t i=0;i<f.cloth.particles.size();++i){const auto rest=f.cloth.particles[i].restPosition;
        // This represents authored garment skinning: fixed waist follows the
        // torso, while a leg/helper bone may severely move all free vertices.
        auto point=glm::dvec3((Fixed(f.cloth.particles[i])?torso:f.pose.global[1])*glm::dvec4(rest,1));
        if(nonrigid&&Fixed(f.cloth.particles[i]))point.y+=.004*std::cos(std::atan2(rest.z,rest.x)*2);
        f.mesh.meshes[0].vertices[i].position=point;
    }
}
void PortableAttachment(){
    std::puts("Attachment-derived rest frames:");std::string error;
    double maximumRigidError=0;bool ok=true;
    for(double angle:{0.,.7,1.8,3.14159265358979323846,-2.7}){
        auto f=Tube();const auto torso=glm::translate(glm::dmat4(1),glm::dvec3(.4,-.2,.6))*glm::rotate(glm::dmat4(1),angle,glm::normalize(glm::dvec3(1,.3,-.5)));
        f.pose.global[1]=glm::translate(glm::dmat4(1),glm::dvec3(3,2,-4));PosePins(f,torso);ClothSimulator sim;ok&=sim.Reset(f.cloth,f.pose,f.mesh,&error);
        for(std::size_t i=0;ok&&i<sim.Positions().size();++i)maximumRigidError=std::max(maximumRigidError,glm::length(sim.Positions()[i]-glm::dvec3(torso*glm::dvec4(f.cloth.particles[i].restPosition,1))));
    }
    Expect(ok&&maximumRigidError<1e-9,"Large torso rotations, including exactly 180 degrees, carry the complete free rest shape");
    auto a=Tube(),b=a;const auto torso=glm::rotate(glm::dmat4(1),1.3,glm::dvec3(1,0,0));a.pose.global[1]=glm::translate(glm::dmat4(1),glm::dvec3(5,4,3));b.pose.global[1]=glm::rotate(glm::dmat4(1),2.8,glm::dvec3(0,1,0));PosePins(a,torso);PosePins(b,torso);ClothSimulator first,second;
    ok=first.Reset(a.cloth,a.pose,a.mesh,&error)&&second.Reset(b.cloth,b.pose,b.mesh,&error);double legDifference=0;
    for(std::size_t i=0;ok&&i<first.Positions().size();++i)legDifference=std::max(legDifference,glm::length(first.Positions()[i]-second.Positions()[i]));
    Expect(ok&&legDifference<1e-12,"Free reference positions remain independent of thigh and skirt helper skinning");
    auto deformed=Tube();PosePins(deformed,torso,true);ClothSimulator smooth;ok=smooth.Reset(deformed.cloth,deformed.pose,deformed.mesh,&error);double pinError=0,mixedLengthError=0;
    for(std::size_t i=0;ok&&i<deformed.cloth.particles.size();++i)if(Fixed(deformed.cloth.particles[i]))pinError=std::max(pinError,glm::length(smooth.Positions()[i]-deformed.mesh.meshes[0].vertices[i].position));
    for(const auto& edge:deformed.cloth.stretch)if(Fixed(deformed.cloth.particles[edge.a])!=Fixed(deformed.cloth.particles[edge.b]))mixedLengthError=std::max(mixedLengthError,std::abs(glm::length(smooth.Positions()[edge.a]-smooth.Positions()[edge.b])-edge.restLength));
    Expect(ok&&pinError<1e-12&&mixedLengthError<.008,"Nonrigid waist residuals extend continuously into free material while pins stay exact");
    auto scaled=Tube();const auto scale=glm::translate(glm::dmat4(1),glm::dvec3(-.5,.2,.1))*glm::scale(glm::dmat4(1),glm::dvec3(1.2));PosePins(scaled,scale);ClothSimulator scaleSim;ok=scaleSim.Reset(scaled.cloth,scaled.pose,scaled.mesh,&error);double scaleError=0;
    for(std::size_t i=0;ok&&i<scaleSim.Positions().size();++i)scaleError=std::max(scaleError,glm::length(scaleSim.Positions()[i]-glm::dvec3(scale*glm::dvec4(scaled.cloth.particles[i].restPosition,1))));
    Expect(ok&&scaleError<1e-9,"Uniform character scale is preserved by the attachment fit");
    auto legacy=Tube();legacy.cloth.motionBone=-1;PosePins(legacy,torso);ClothSimulator legacySim;ok=legacySim.Reset(legacy.cloth,legacy.pose,legacy.mesh,&error);double legacyError=0;
    for(std::size_t i=0;ok&&i<legacySim.Positions().size();++i)legacyError=std::max(legacyError,glm::length(legacySim.Positions()[i]-legacy.mesh.meshes[0].vertices[i].position));
    Expect(ok&&legacyError<1e-12,"Legacy assets with no rigid reference retain their authored skinned goals");
    std::printf("    rigid fit error %.12gm; waist residual mixed-edge error %.6gm\n",maximumRigidError,mixedLengthError);
    if(!ok)std::printf("    %s\n",error.c_str());
}
std::vector<glm::dvec3> Targets(const ClothAsset& cloth,const StaticModel& mesh){
    std::vector<glm::dvec3> result(cloth.particles.size(),glm::dvec3(0));std::vector<std::size_t> counts(result.size(),0);
    for(const auto& binding:cloth.renderBindings){glm::dmat4 transform(1);for(const auto& instance:mesh.instances)if(instance.meshIndex==binding.mesh){transform=instance.transform;break;}result[binding.particle]+=glm::dvec3(transform*glm::dvec4(mesh.meshes[binding.mesh].vertices[binding.vertex].position,1));++counts[binding.particle];}
    for(std::size_t i=0;i<result.size();++i)if(counts[i])result[i]/=double(counts[i]);return result;
}
void RealResetFrames(bool quick,bool sweep,const std::filesystem::path& overrideManifest){
    std::puts("Mahiro arbitrary-pose reset continuity:");const std::filesystem::path root=MODEL_SOURCE_DIR;
    const auto manifest=overrideManifest.empty()?root/"Asset/Generated/MahiroCloth/Character.asset.json":overrideManifest;
    const auto animation=root/"Asset/Generated/Animations/MahiroTaitui.anim.json";
    if(!std::filesystem::exists(manifest)||!std::filesystem::exists(animation)){std::puts("    Generated local fixtures unavailable; portable attachment tests completed.");return;}
    std::string error;auto character=LoadModularCharacter(manifest,&error);if(!Expect(bool(character),"The canonical separated body, garment and cloth assets load")){std::printf("    %s\n",error.c_str());return;}
    auto clip=LoadAnimationSequenceAsset(animation,*character->garment,&error);if(!Expect(bool(clip),"The complete real VMD-derived sequence loads")){std::printf("    %s\n",error.c_str());return;}
    const auto& cloth=character->cloth;ClothSimulator sim;StaticModel mesh;bool ok=true;double maxPin=0,maxMixedRatio=0,maxMixedError=0,oldMixedRatio=0;int frames=0;
    for(double time=0;time<=clip->duration+1e-8;time=++frames/30.){
        const auto pose=EvaluateSkeletalPose(*character->garment,&*clip,std::min(time,clip->duration),false);
        if(!SkinSkeletalMesh(*character->garment,pose,mesh,&error)||!sim.Reset(cloth,pose,mesh,&error)){ok=false;break;}
        const auto target=Targets(cloth,mesh);const auto fallback=pose.global[cloth.motionBone]*cloth.motionInverseBind;
        for(std::size_t i=0;i<target.size();++i)if(Fixed(cloth.particles[i]))maxPin=std::max(maxPin,glm::length(sim.Positions()[i]-target[i])/cloth.unitsPerMeter);
        for(const auto& edge:cloth.stretch)if(Fixed(cloth.particles[edge.a])!=Fixed(cloth.particles[edge.b])){
            const auto oldPoint=[&](auto i){return Fixed(cloth.particles[i])?target[i]:glm::dvec3(fallback*glm::dvec4(cloth.particles[i].restPosition,1));};
            oldMixedRatio=std::max(oldMixedRatio,glm::length(oldPoint(edge.a)-oldPoint(edge.b))/edge.restLength);
            const double length=glm::length(sim.Positions()[edge.a]-sim.Positions()[edge.b]);maxMixedRatio=std::max(maxMixedRatio,length/edge.restLength);maxMixedError=std::max(maxMixedError,std::abs(length-edge.restLength)/cloth.unitsPerMeter);
        }
    }
    Expect(ok&&frames>=277&&maxPin<1e-9,"Resetting at all 277 animation keyframes keeps the full authored waist exactly attached");
    Expect(ok&&maxMixedRatio<1.5&&maxMixedError<.01,"Every arbitrary-pose initial seam has bounded edge strain without a pelvis/torso discontinuity");
    Expect(ok&&oldMixedRatio>10&&maxMixedRatio<oldMixedRatio*.1,"The real regression reproduces the old large reset strain and verifies its removal");
    std::printf("    %d reset poses; old mixed length/rest %.6f -> fitted %.6f, maximum edge error %.6fm, pin error %.12gm\n",frames,oldMixedRatio,maxMixedRatio,maxMixedError,maxPin);
    if(quick){if(!ok)std::printf("    %s\n",error.c_str());return;}
    const std::vector<double> poses=sweep?std::vector<double>{0,.3,2./3,1,1.7,2.3,2.9,3.4,3.5,4.1,4.9,5.6,6.3,7,7.4,8,8.7,9.2}:std::vector<double>{2.9,3.5,7.4};
    for(double time:poses){
        const auto pose=EvaluateSkeletalPose(*character->garment,&*clip,time,false);ClothSimulator resetA,resetB;ok=SkinSkeletalMesh(*character->garment,pose,mesh,&error)&&resetA.Reset(cloth,pose,mesh,&error)&&resetB.Reset(cloth,pose,mesh,&error);
        double penetration=0,rawPenetration=0,sourceConflict=0,edgeError=0,pinError=0,difference=0;std::size_t collapsed=0;const auto target=Targets(cloth,mesh);
        for(int tick=0;ok&&tick<30;++tick){ok=resetA.Step(cloth,pose,mesh,1./60,cloth.settings,&error)&&resetB.Step(cloth,pose,mesh,1./60,cloth.settings,&error);
            penetration=std::max(penetration,resetA.Stats().maxResolvableSurfacePenetration/cloth.unitsPerMeter);
            rawPenetration=std::max(rawPenetration,resetA.Stats().maxSurfacePenetration/cloth.unitsPerMeter);
            sourceConflict=std::max(sourceConflict,resetA.Stats().maxKinematicColliderPenetration/cloth.unitsPerMeter);edgeError=std::max(edgeError,resetA.Stats().maxDynamicEdgeError/cloth.unitsPerMeter);collapsed=std::max(collapsed,resetA.Stats().collapsedTriangles);
            for(std::size_t i=0;ok&&i<target.size();++i){ok&=Finite(resetA.Positions()[i]);difference=std::max(difference,glm::length(resetA.Positions()[i]-resetB.Positions()[i])/cloth.unitsPerMeter);if(Fixed(cloth.particles[i]))pinError=std::max(pinError,glm::length(resetA.Positions()[i]-target[i])/cloth.unitsPerMeter);}
        }
        Expect(ok&&pinError<1e-9&&difference<1e-10,"Repeated focused-pose resets settle deterministically with an exact fixed attachment");
        Expect(ok&&collapsed==0&&penetration<.008&&edgeError<.05,"Focused-pose preparation and settling stay finite without collapsed surfaces or large edge errors");
        Expect(ok&&rawPenetration<=penetration+sourceConflict+1e-6,"Raw surface errors remain bounded by explicit authored attachment conflicts and resolvable errors");
        std::printf("    reset %.2fs: pin %.12gm, resolvable/raw penetration %.6f / %.6fm, source conflict %.6fm, dynamic edge %.6fm, collapsed %zu, repeated-run delta %.12gm\n",time,pinError,penetration,rawPenetration,sourceConflict,edgeError,collapsed,difference);
    }
    if(!ok)std::printf("    %s\n",error.c_str());
}
}
int main(int argc,char** argv){
    bool quick=false,sweep=false;std::filesystem::path manifest;
    for(int i=1;i<argc;++i){const std::string option=argv[i];
        if(option=="--quick")quick=true;
        else if(option=="--sweep-resets")sweep=true;
        else if(option=="--character"&&i+1<argc)manifest=std::filesystem::u8path(argv[++i]);
        else {std::fprintf(stderr,"Unknown or incomplete attachment test option: %s\n",option.c_str());return 2;}
    }
    if(!manifest.empty()&&!std::filesystem::exists(manifest)){std::fprintf(stderr,"Requested character fixture does not exist\n");return 2;}
    PortableAttachment();RealResetFrames(quick,sweep,manifest);
    std::printf("Cloth attachment: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;
}
