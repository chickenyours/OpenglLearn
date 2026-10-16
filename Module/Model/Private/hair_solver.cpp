#include "Model/Public/hair_asset.h"
#include "Model/Private/hair_body_collision.h"
#include "Model/Private/hair_surface_contact.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Model {
namespace {
using Clock=std::chrono::steady_clock;
constexpr double Tiny=1e-18;
bool Finite(glm::dvec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
glm::dquat Unit(glm::dquat q){const auto n=glm::dot(q,q);return std::isfinite(n)&&n>Tiny?q/std::sqrt(n):glm::dquat(1,0,0,0);}
glm::dvec3 Direction(glm::dvec3 v,glm::dvec3 fallback={0,-1,0}){
    const auto n=glm::dot(v,v);return std::isfinite(n)&&n>Tiny?v/std::sqrt(n):fallback;
}
glm::dquat Between(glm::dvec3 from,glm::dvec3 to){
    from=Direction(from);to=Direction(to);const auto d=std::clamp(glm::dot(from,to),-1.,1.);
    if(d>1-1e-14)return {1,0,0,0};
    if(d<-1+1e-12){auto axis=glm::cross(from,std::abs(from.x)<.8?glm::dvec3(1,0,0):glm::dvec3(0,1,0));return glm::angleAxis(glm::pi<double>(),Direction(axis));}
    const auto c=glm::cross(from,to);return Unit(glm::dquat(1+d,c.x,c.y,c.z));
}
glm::dquat LimitSwing(glm::dquat candidate,glm::dquat animated,double limit){
    auto delta=Unit(candidate*glm::inverse(Unit(animated)));if(delta.w<0)delta=-delta;
    const auto angle=2*std::acos(std::clamp(delta.w,0.,1.));
    if(angle>limit&&angle>1e-12)delta=glm::slerp(glm::dquat(1,0,0,0),delta,std::max(0.,limit)/angle);
    return Unit(delta*animated);
}
double Milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
bool SameSettings(const HairSolverSettings& a,const HairSolverSettings& b){
    return a.fixedTimeStep==b.fixedTimeStep&&a.substeps==b.substeps&&a.iterations==b.iterations&&a.thickness==b.thickness
        &&a.friction==b.friction&&a.inertia==b.inertia&&a.gravity==b.gravity&&a.wind==b.wind&&a.collision==b.collision;
}
bool SamePose(const SkeletalPose& a,const SkeletalPose& b){
    if(a.global!=b.global||a.morphWeights!=b.morphWeights||a.local.size()!=b.local.size())return false;
    for(std::size_t i=0;i<a.local.size();++i)if(a.local[i].translation!=b.local[i].translation||a.local[i].rotation!=b.local[i].rotation||a.local[i].scale!=b.local[i].scale)return false;
    return true;
}
}

struct HairSimulator::Impl {
    struct ChainState {
        std::vector<glm::dvec3> position,velocity,old,goal,contactDelta;
        std::vector<double> length;
        glm::dmat4 previousAnchor{1};
    };
    struct MorphContribution {std::uint32_t morph=0,bone=0;glm::dvec3 position{0};};
    struct ProbeState {
        std::vector<MorphContribution> morph;glm::dvec3 previous{0};std::uint32_t bodyHint=UINT32_MAX;
        glm::dvec3 clearPoint{0};double clearance=0;std::uint64_t clearEpoch=0;
        glm::dvec3 measuredPoint{0};double measuredDepth=0,measuredThickness=0;std::uint64_t measuredEpoch=0;bool measuredCollision=false;
        detail::HairBodyContact measuredContact;
    };
    struct AttachmentState {HairSurfaceProbe probe;ProbeState cached;};
    struct MeshMorphDelta {std::uint32_t morph=0,vertex=0;glm::dvec3 delta{0};};
    const HairAsset* asset=nullptr;
    const SkeletalAssetPackage* target=nullptr;
    detail::HairBodyCollision body;
    detail::HairSurfaceContact surface;
    std::vector<ChainState> chains;
    std::vector<ProbeState> probes;
    std::vector<AttachmentState> attachments;
    std::vector<glm::dmat4> inverseAnchors;
    std::vector<std::vector<MeshMorphDelta>> meshMorphs;
    std::vector<std::vector<std::uint32_t>> chainAffectedProbes;
    std::vector<std::uint32_t> adaptiveCandidates;
    std::vector<bool> adaptiveAffected;
    std::vector<int> boneChain,boneLink;
    std::vector<glm::dquat> simulatedAnimatedRotations;
    SkeletalPose solved;
    HairSimulationStats stats;
    std::vector<glm::dvec3> debugPositions;
    double accumulator=0,units=1;
    std::uint64_t bodyEpoch=0,bodyGeometryRevision=0;
    bool initialized=false,contactCache=true,adaptiveContacts=false,coarseContacts=true,boneSurfaceContacts=true,boneAnchorContacts=true,boneFreeProbes=false;
    bool stableContactVelocity=true,localJointLimits=true,measurementReuse=true;
    bool sleepEnabled=true,sleepEligible=false,wakePending=false;
    SkeletalPose physicalInput;HairSolverSettings physicalSettings;
    std::vector<glm::dvec3> settledNodes;
    std::vector<HairSurfaceOffset> settledOffsets;
    double settledSeconds=0;std::uint32_t settledTicks=0;

    void WakeForInput(const SkeletalPose& animated,const HairSolverSettings& settings){
        if(!SamePose(animated,physicalInput)||!SameSettings(settings,physicalSettings)){
            stats.sleeping=false;wakePending=true;settledSeconds=0;settledTicks=0;
        }
    }
    void CaptureSettledState(){
        settledNodes.clear();for(const auto& chain:chains)settledNodes.insert(settledNodes.end(),chain.position.begin(),chain.position.end());
        settledOffsets=surface.Offsets();
    }
    void ObserveSettling(const SkeletalPose& animated,const HairSolverSettings& settings,double dt){
        if(sleepEnabled&&sleepEligible&&settings.collision&&stats.surfaceVertices>0){
            constexpr double Motion=1e-9;
            bool stable=!wakePending&&SamePose(animated,physicalInput)&&SameSettings(settings,physicalSettings)
                &&std::isfinite(stats.maxSurfacePenetration)&&stats.maxSurfacePenetration<=1e-6;
            std::size_t node=0;
            for(const auto& chain:chains)for(std::size_t i=0;i<chain.position.size();++i,++node){
                stable&=node<settledNodes.size()&&Finite(chain.position[i])&&Finite(chain.velocity[i])
                    &&(node>=settledNodes.size()||glm::length(chain.position[i]-settledNodes[node])<=Motion)&&glm::length(chain.velocity[i])<=Motion;
            }
            stable&=node==settledNodes.size();const auto& offsets=surface.Offsets();stable&=offsets.size()==settledOffsets.size();
            if(offsets.size()==settledOffsets.size())for(std::size_t i=0;i<offsets.size();++i){const auto& a=offsets[i];const auto& b=settledOffsets[i];
                stable&=a.mesh==b.mesh&&a.vertex==b.vertex&&a.anchorBone==b.anchorBone&&Finite(a.displacement)
                    &&a.anchorBone<animated.global.size();
                if(a.anchorBone<animated.global.size())stable&=glm::length(glm::dmat3(animated.global[a.anchorBone])*(a.displacement-b.displacement)/units)<=Motion;
            }
            if(stable){settledSeconds+=dt;++settledTicks;}else {settledSeconds=0;settledTicks=0;}
            // Sleeping follows demonstrated convergence. It only removes the
            // remaining nanometer round-trip noise; it cannot arrest a moving
            // strand or an unresolved surface to hide an unstable solve.
            stats.sleeping=settledSeconds+1e-12>=1&&settledTicks>=30;
            CaptureSettledState();
        }else {stats.sleeping=false;settledSeconds=0;settledTicks=0;}
        physicalInput=animated;physicalSettings=settings;wakePending=false;
    }

    void NewBodyPose(bool reset=false){
        const auto revision=body.GeometryRevision();
        if(reset||revision!=bodyGeometryRevision){if(++bodyEpoch==0)++bodyEpoch;bodyGeometryRevision=revision;}
    }
    bool MeasurementMatches(const ProbeState& p,glm::dvec3 point,const HairSolverSettings& settings)const{
        return p.measuredEpoch==bodyEpoch&&p.measuredPoint==point&&p.measuredCollision==settings.collision&&p.measuredThickness==settings.thickness;
    }
    void MeasurementKey(ProbeState& p,glm::dvec3 point,const HairSolverSettings& settings){
        p.measuredPoint=point;p.measuredEpoch=bodyEpoch;p.measuredCollision=settings.collision;p.measuredThickness=settings.thickness;
    }
    bool ClearOfBody(const ProbeState& p,glm::dvec3 current,double margin)const{
        return contactCache&&p.clearEpoch==bodyEpoch&&p.clearance>glm::length(current-p.clearPoint)+margin+1e-9;
    }
    void Certificate(ProbeState& p,glm::dvec3 point,double distance){
        if(distance>0&&std::isfinite(distance)){p.clearPoint=point;p.clearance=distance;p.clearEpoch=bodyEpoch;}
        else {p.clearance=0;p.clearEpoch=0;}
    }
    void ObserveProbe(std::size_t p,glm::dvec3 point,const HairSolverSettings& settings,bool adaptive=false){
        auto& cached=probes[p];cached.previous=point;
        // An unchanged finite surface and exact query position have the same
        // nearest/inside result. Preserve that result, including an unresolved
        // inside depth; this is measurement reuse, not a contact exemption.
        if(measurementReuse&&MeasurementMatches(cached,point,settings)){++stats.measurementReuses;return;}
        MeasurementKey(cached,point,settings);cached.measuredDepth=0;cached.measuredContact={};
        if(!settings.collision)return;
        if(ClearOfBody(cached,point,settings.thickness)){++stats.clearanceSkips;return;}
        ++stats.collisionQueries;if(adaptive)++stats.adaptiveQueries;
        const auto near=body.Nearest(point,.08,&cached.bodyHint);
        Certificate(cached,point,near.found&&!near.inside?near.distance:0.);
        if(!near.found)return;
        cached.measuredDepth=std::max(0.,settings.thickness-near.signedDistance);
        auto& contact=cached.measuredContact;contact.signedGap=near.signedDistance;contact.normal=near.normal;contact.closestPoint=near.point;
        contact.triangle=near.triangle;contact.outsideClearance=near.inside?0:near.distance;
        contact.hit=cached.measuredDepth>0&&(near.inside||!near.backfacing);
        if(contact.hit)contact.correction=near.normal*cached.measuredDepth;
    }

    glm::dvec3 Point(const HairSurfaceProbe& probe,const ProbeState& cached,const SkeletalPose& pose)const{
        glm::dvec3 p(0);
        for(const auto& i:probe.influences)p+=glm::dvec3(pose.global[i.bone]*glm::dvec4(i.weightedPosition,i.weight));
        for(const auto& m:cached.morph)if(m.morph<pose.morphWeights.size())
            p+=glm::dvec3(pose.global[m.bone]*glm::dvec4(m.position,0))*double(pose.morphWeights[m.morph]);
        return p/units;
    }
    void CacheMorphs(const HairSurfaceProbe& probe,ProbeState& cached){
        if(probe.mesh>=target->skins.size())return;const auto& skin=target->skins[probe.mesh];const auto count=target->skeleton.bones.size();
        for(const auto& delta:meshMorphs[probe.mesh]){
            double bary=delta.vertex==probe.vertex&&!probe.triangleSample?1.:0.;
            if(probe.triangleSample)for(int k=0;k<3;++k)if(delta.vertex==probe.triangle[k])bary+=probe.barycentric[k];
            if(bary==0||delta.vertex>=skin.vertices.size())continue;const auto& weights=skin.vertices[delta.vertex];double sum=0;
            for(int k=0;k<4;++k)if(weights.weights[k]>0&&weights.joints[k]<count)sum+=weights.weights[k];
            if(sum<=0)continue;
            for(int k=0;k<4;++k){const auto bone=weights.joints[k];if(weights.weights[k]<=0||bone>=count)continue;
                const auto offset=bone<skin.boneFromMesh.size()?skin.boneFromMesh[bone]:target->skeleton.bones[bone].inverseBind*skin.bindTransform;
                cached.morph.push_back({delta.morph,bone,glm::dvec3(offset*glm::dvec4(delta.delta,0))*(bary*weights.weights[k]/sum)});}
        }
    }
    void MeasureAttachments(const SkeletalPose& animated,const SkeletalPose& presented){
        stats.maxAttachmentDrift=0;
        for(std::size_t c=0;c<asset->chains.size();++c){const auto anchor=asset->chains[c].anchorBone;
            inverseAnchors[c]=anchor>=0?glm::inverse(animated.global[anchor]):glm::dmat4(1);}
        for(const auto& attachment:attachments){
            const auto simulated=Point(attachment.probe,attachment.cached,presented);
            const auto source=Point(attachment.probe,attachment.cached,animated);
            const auto localDelta=glm::dmat3(inverseAnchors[attachment.probe.chain])*(simulated-source);
            stats.maxAttachmentDrift=std::max(stats.maxAttachmentDrift,glm::length(localDelta));
        }
    }
    bool Descendant(std::uint32_t bone,std::uint32_t ancestor)const{
        // Skeleton order is unrestricted; chain lengths, rather than bone IDs,
        // bound this walk. This also supports helper bones between weighted links.
        for(std::size_t n=0;n<target->skeleton.bones.size();++n){
            if(bone==ancestor)return true;
            const auto parent=target->skeleton.bones[bone].parent;
            if(parent<0)return false;bone=std::uint32_t(parent);
        }return false;
    }
    void ComposeChain(std::size_t c,std::size_t first=0){
        const auto& chain=asset->chains[c];
        for(std::size_t i=first;i<chain.links.size();++i){
            const auto b=chain.links[i].bone;const auto parent=target->skeleton.bones[b].parent;
            solved.global[b]=(parent>=0?solved.global[parent]:glm::dmat4(1))*TransformMatrix(solved.local[b]);
        }
    }
    void PoseFromNodes(std::size_t c,const SkeletalPose& animated){
        const auto& chain=asset->chains[c];auto& state=chains[c];
        for(std::size_t i=0;i<chain.links.size();++i){
            const auto& link=chain.links[i];const auto b=link.bone;const auto parent=target->skeleton.bones[b].parent;
            const auto parentGlobal=parent>=0?solved.global[parent]:glm::dmat4(1);
            auto& local=solved.local[b];local=animated.local[b];
            const auto pivot=glm::dvec3(parentGlobal*glm::dvec4(local.translation,1))/units;
            const auto desired=state.position[i+1]-pivot;
            const auto inverseLinear=glm::inverse(glm::dmat3(parentGlobal));
            const auto localDesired=inverseLinear*desired;
            const auto localRest=local.rotation*(local.scale*link.restTip);
            local.rotation=LimitSwing(Between(localRest,localDesired)*local.rotation,animated.local[b].rotation,link.maxAngle);
            solved.global[b]=parentGlobal*TransformMatrix(local);
        }
    }
    void NodesFromPose(std::size_t c){
        const auto& chain=asset->chains[c];auto& state=chains[c];
        state.position[0]=glm::dvec3(solved.global[chain.links.front().bone][3])/units;
        for(std::size_t i=0;i<chain.links.size();++i)
            state.position[i+1]=glm::dvec3(solved.global[chain.links[i].bone]*glm::dvec4(chain.links[i].restTip,1))/units;
    }
    void Debug(){
        std::size_t n=0;for(const auto& chain:chains)for(const auto& p:chain.position)debugPositions[n++]=p*units;
    }
    void Present(const SkeletalPose& animated,SkeletalPose& output){
        // Presentation may run more frequently than physics. Only the hair's
        // simulated local rotation offset is held; the character animation and
        // morphs must always belong to this caller's current completed pose.
        output=animated;std::size_t node=0;stats.maxRootError=0;
        for(const auto& chain:asset->chains){
            for(const auto& link:chain.links){const auto b=link.bone;
                const auto reference=simulatedAnimatedRotations[b];
                auto& local=output.local[b];
                local.rotation=animated.local[b].rotation==reference?solved.local[b].rotation
                    :Unit(solved.local[b].rotation*glm::inverse(Unit(reference))*animated.local[b].rotation);
                const auto parent=target->skeleton.bones[b].parent;
                output.global[b]=(parent>=0?output.global[parent]:glm::dmat4(1))*TransformMatrix(local);
            }
            const auto root=chain.links.front().bone;
            debugPositions[node++]=glm::dvec3(output.global[root][3]);
            for(const auto& link:chain.links)
                debugPositions[node++]=glm::dvec3(output.global[link.bone]*glm::dvec4(link.restTip,1));
            stats.maxRootError=std::max(stats.maxRootError,glm::length(glm::dvec3(output.global[root][3]-animated.global[root][3]))/units);
        }
        // No physical positions, velocities, anchor history, probe trajectory,
        // body BVH or fixed-step accumulator is changed by presentation.
        MeasureAttachments(animated,output);
    }
    bool BoneProbeAllowed(std::size_t p)const{
        if(!boneFreeProbes)return true;
        const auto& probe=asset->probes[p];double distalWeight=0;
        for(const auto& influence:probe.influences)if(influence.weight>0&&boneChain[influence.bone]==int(probe.chain)&&boneLink[influence.bone]>0)
            distalWeight+=influence.weight;
        return distalWeight>=.5;
    }
    // Contact acts on the actual LBS surface. The rotation gradient is the
    // weighted skin Jacobian, so the rendered strand moves with the debug chain.
    bool CorrectProbe(std::size_t p,const detail::HairBodyContact& contact,const SkeletalPose& animated){
        if(!BoneProbeAllowed(p))return false;
        const auto& probe=asset->probes[p];const auto& cached=probes[p];const auto c=probe.chain;
        const auto& chain=asset->chains[c];
        const auto n=Direction(contact.normal);const auto depth=std::max(0.,glm::dot(contact.correction,n));
        if(!(depth>1e-8))return false;
        std::array<glm::dvec3,64> gradient{};
        std::array<glm::dquat,64> previousRotation{};
        const auto count=std::min(chain.links.size(),gradient.size());
        const auto initial=Point(probe,cached,solved);const double contactPlane=glm::dot(initial,n)+depth;
        bool changed=false;
        // Re-linearize this one already queried finite contact, rather than
        // adding global BVH passes. A scalp-limited ancestor must stop taking
        // the correction denominator once its outward angular freedom is gone.
        for(int linearization=0;linearization<4;++linearization){
            const auto before=Point(probe,cached,solved);const auto remaining=contactPlane-glm::dot(before,n);
            if(remaining<=1e-6)break;double denominator=0;
            for(std::size_t j=0;j<count;++j){
                const auto& link=chain.links[j];const auto bone=link.bone;gradient[j]={0,0,0};previousRotation[j]=solved.local[bone].rotation;
                if(link.maxAngle<=1e-8)continue;
                const auto pivot=glm::dvec3(solved.global[bone][3])/units;glm::dvec3 radius(0);
                for(const auto& i:probe.influences)if(Descendant(i.bone,bone))
                    radius+=glm::dvec3(solved.global[i.bone]*glm::dvec4(i.weightedPosition,i.weight))/units-pivot*i.weight;
                for(const auto& m:cached.morph)if(m.morph<solved.morphWeights.size()&&Descendant(m.bone,bone))
                    radius+=glm::dvec3(solved.global[m.bone]*glm::dvec4(m.position,0))*(double(solved.morphWeights[m.morph])/units);
                auto g=glm::cross(radius,n);
                const auto strandAxis=Direction(glm::dvec3(solved.global[bone]*glm::dvec4(link.restTip,0)));
                g-=strandAxis*glm::dot(g,strandAxis);
                auto relative=Unit(solved.local[bone].rotation*glm::inverse(Unit(animated.local[bone].rotation)));if(relative.w<0)relative=-relative;
                const auto angle=2*std::acos(std::clamp(relative.w,0.,1.));
                if(angle>=link.maxAngle-1e-7&&angle>1e-10){
                    const auto parent=target->skeleton.bones[bone].parent;
                    const auto parentLinear=parent>=0?glm::dmat3(solved.global[parent]):glm::dmat3(1);
                    const auto radial=Direction(parentLinear*glm::dvec3(relative.x,relative.y,relative.z));
                    const auto outward=glm::dot(g,radial);
                    if(outward>0){
                        // Project into the angular-bound tangent while keeping
                        // the no-twist swing plane. Inward motion stays free.
                        const auto swingRadial=radial-strandAxis*glm::dot(radial,strandAxis);
                        const auto length2=glm::dot(swingRadial,swingRadial);
                        g=length2>1e-14?g-swingRadial*(outward/length2):glm::dvec3(0);
                    }
                }
                gradient[j]=g;denominator+=glm::dot(g,g);
            }
            if(denominator<1e-16)break;
            bool accepted=false;
            for(int trial=0;trial<3&&!accepted;++trial){
                if(trial){for(std::size_t j=0;j<count;++j)solved.local[chain.links[j].bone].rotation=previousRotation[j];ComposeChain(c);}
                const double gain=std::ldexp(1.,-trial);
                for(std::size_t j=0;j<count;++j){
                    const auto rotationVector=gradient[j]*(remaining*gain/denominator);
                    const auto angle=glm::length(rotationVector);if(angle<1e-10)continue;
                    const auto b=chain.links[j].bone;const auto parent=target->skeleton.bones[b].parent;
                    const auto parentGlobal=parent>=0?solved.global[parent]:glm::dmat4(1);
                    const auto axis=Direction(glm::inverse(glm::dmat3(parentGlobal))*Direction(rotationVector));
                    const auto delta=glm::angleAxis(std::min(angle,.12),axis);auto& local=solved.local[b];
                    local.rotation=LimitSwing(delta*local.rotation,animated.local[b].rotation,chain.links[j].maxAngle);ComposeChain(c,j);
                }
                accepted=glm::dot(Point(probe,cached,solved)-before,n)>std::max(1e-10,remaining*1e-6);
            }
            if(!accepted){for(std::size_t j=0;j<count;++j)solved.local[chain.links[j].bone].rotation=previousRotation[j];ComposeChain(c);break;}
            changed=true;
        }
        if(changed){auto& state=chains[c];std::array<glm::dvec3,65> before;
            if(stableContactVelocity)std::copy(state.position.begin(),state.position.end(),before.begin());
            NodesFromPose(c);
            if(stableContactVelocity)for(std::size_t i=1;i<state.position.size();++i)state.contactDelta[i]+=state.position[i]-before[i];
            ++stats.contacts;stats.maxSurfaceCorrection=std::max(stats.maxSurfaceCorrection,depth);}
        return changed;
    }
    bool BoneContactAllowed(std::size_t p,const detail::HairBodyContact& contact)const{
        if(boneAnchorContacts||contact.triangle>=body.TriangleCount())return true;
        const auto anchor=asset->chains[asset->probes[p].chain].anchorBone;
        return anchor<0||body.TriangleBoneWeight(contact.triangle,std::uint32_t(anchor))<.5;
    }
    void SurfacePass(const SkeletalPose& animated,const HairSolverSettings& settings,bool swept){
        const auto start=Clock::now();
        for(std::size_t p=0;p<asset->probes.size();++p){
            if(!BoneProbeAllowed(p))continue;
            const auto current=Point(asset->probes[p],probes[p],solved);
            // Only an endpoint proof in this exact body pose may be reused.
            // The first swept pass always checks source-body and point motion.
            if(!swept&&ClearOfBody(probes[p],current,settings.thickness)){++stats.clearanceSkips;continue;}
            ++stats.collisionQueries;
            const auto contact=body.Contact(swept?probes[p].previous:current,current,settings.thickness,.06,swept,&probes[p].bodyHint);
            Certificate(probes[p],current,contact.outsideClearance);
            if(contact.hit&&BoneContactAllowed(p,contact))CorrectProbe(p,contact,animated);
        }
        stats.collisionMilliseconds+=Milliseconds(start);
    }
    void Measure(const SkeletalPose& animated,const HairSolverSettings& settings){
        stats.maxPenetration=0;stats.maxTipDisplacement=0;stats.maxRootError=0;stats.worstPenetrationProbe=UINT32_MAX;
        const auto start=Clock::now();
        for(std::size_t p=0;p<asset->probes.size();++p){
            ObserveProbe(p,Point(asset->probes[p],probes[p],solved),settings);
            if(probes[p].measuredDepth>stats.maxPenetration){stats.maxPenetration=probes[p].measuredDepth;stats.worstPenetrationProbe=std::uint32_t(p);}
        }
        stats.collisionMilliseconds+=Milliseconds(start);MeasureShape(animated);
    }
    void MeasureShape(const SkeletalPose& animated){
        stats.maxTipDisplacement=0;stats.maxRootError=0;
        for(std::size_t c=0;c<chains.size();++c){const auto& links=asset->chains[c].links;const auto& state=chains[c];
            stats.maxRootError=std::max(stats.maxRootError,glm::length(state.position[0]-glm::dvec3(animated.global[links.front().bone][3])/units));
            for(std::size_t i=0;i<links.size();++i){
                const auto goal=glm::dvec3(animated.global[links[i].bone]*glm::dvec4(links[i].restTip,1))/units;
                stats.maxTipDisplacement=std::max(stats.maxTipDisplacement,glm::length(state.position[i+1]-goal));
            }
        }
        MeasureAttachments(animated,solved);Debug();
    }
    void RefineContacts(const SkeletalPose& animated,const HairSolverSettings& settings){
        constexpr double Tolerance=.002;constexpr std::size_t QueryBudget=256;
        if(!boneSurfaceContacts||!settings.collision||stats.maxPenetration<=Tolerance)return;
        const auto start=Clock::now();
        for(std::size_t round=0;round<3&&stats.maxPenetration>Tolerance;++round){
            adaptiveCandidates.clear();std::fill(adaptiveAffected.begin(),adaptiveAffected.end(),false);std::size_t pendingVerification=0;
            for(std::uint32_t p=0;p<probes.size();++p)if(probes[p].measuredDepth>Tolerance&&BoneProbeAllowed(p))adaptiveCandidates.push_back(p);
            std::sort(adaptiveCandidates.begin(),adaptiveCandidates.end(),[&](auto a,auto b){return probes[a].measuredDepth==probes[b].measuredDepth?a<b:probes[a].measuredDepth>probes[b].measuredDepth;});
            bool changed=false;++stats.adaptiveRounds;
            for(const auto p:adaptiveCandidates){const auto c=asset->probes[p].chain;auto& cached=probes[p];
                const auto current=Point(asset->probes[p],cached,solved);const bool exact=MeasurementMatches(cached,current,settings);
                const bool clear=!exact&&ClearOfBody(cached,current,settings.thickness);
                std::size_t newVerification=0;for(const auto other:chainAffectedProbes[c])newVerification+=!adaptiveAffected[other];
                const std::size_t freshQuery=!exact&&!clear?1:0;
                // Reserve a final true observation for every changed dependent
                // probe before changing any rotation. Budget exhaustion cannot
                // leave the reported final residual at an older pose.
                if(stats.adaptiveQueries+freshQuery+pendingVerification+newVerification>QueryBudget){stats.adaptiveBudgetExhausted=true;continue;}
                if(clear){MeasurementKey(cached,current,settings);cached.measuredDepth=0;cached.measuredContact={};++stats.clearanceSkips;continue;}
                if(!exact){++stats.collisionQueries;++stats.adaptiveQueries;
                    cached.measuredContact=body.Contact(current,current,settings.thickness,.08,false,&cached.bodyHint);
                    Certificate(cached,current,cached.measuredContact.outsideClearance);MeasurementKey(cached,current,settings);
                    cached.measuredDepth=std::isfinite(cached.measuredContact.signedGap)?std::max(0.,settings.thickness-cached.measuredContact.signedGap):0.;}
                if(cached.measuredDepth>Tolerance&&cached.measuredContact.hit&&BoneContactAllowed(p,cached.measuredContact)&&CorrectProbe(p,cached.measuredContact,animated)){
                    changed=true;for(const auto other:chainAffectedProbes[c])if(!adaptiveAffected[other]){adaptiveAffected[other]=true;++pendingVerification;}
                }
            }
            if(!changed)break;
            for(std::size_t p=0;p<probes.size();++p)if(adaptiveAffected[p]){
                const auto current=Point(asset->probes[p],probes[p],solved);
                if(!MeasurementMatches(probes[p],current,settings))ObserveProbe(p,current,settings,true);
                else probes[p].previous=current;
            }
            stats.maxPenetration=0;stats.worstPenetrationProbe=UINT32_MAX;
            for(std::size_t p=0;p<probes.size();++p)if(probes[p].measuredDepth>stats.maxPenetration){stats.maxPenetration=probes[p].measuredDepth;stats.worstPenetrationProbe=std::uint32_t(p);}
        }
        stats.collisionMilliseconds+=Milliseconds(start);MeasureShape(animated);
    }
    bool Tick(const SkeletalPose& animated,const HairSolverSettings& settings,double dt,bool finalTick,std::string* error){
        solved=animated;
        for(const auto& chain:asset->chains)for(const auto& link:chain.links)
            simulatedAnimatedRotations[link.bone]=animated.local[link.bone].rotation;
        const auto substeps=std::clamp(settings.substeps,1u,8u),iterations=std::clamp(settings.iterations,1u,32u);
        const double h=dt/double(substeps),inertia=std::clamp(settings.inertia,0.,1.);
        // A fractional rigid carry avoids an unbounded lag after large root
        // locomotion while leaving the world-space remainder to produce inertia.
        for(std::size_t c=0;c<chains.size();++c){auto& state=chains[c];const auto& chain=asset->chains[c];
            const auto anchor=chain.anchorBone>=0?animated.global[chain.anchorBone]:glm::dmat4(1);
            const auto carry=anchor*glm::inverse(state.previousAnchor);
            for(std::size_t i=1;i<state.position.size();++i){
                const auto carried=glm::dvec3(carry*glm::dvec4(state.position[i]*units,1))/units;
                state.position[i]=glm::mix(state.position[i],carried,1-inertia);
            }state.previousAnchor=anchor;
            state.goal[0]=glm::dvec3(animated.global[chain.links.front().bone][3])/units;
            for(std::size_t i=0;i<chain.links.size();++i){
                state.goal[i+1]=glm::dvec3(animated.global[chain.links[i].bone]*glm::dvec4(chain.links[i].restTip,1))/units;
                const auto pivot=glm::dvec3(animated.global[chain.links[i].bone][3])/units;
                state.length[i]=glm::length(state.goal[i+1]-pivot);
            }
        }
        for(std::uint32_t sub=0;sub<substeps;++sub){
            for(std::size_t c=0;c<chains.size();++c){auto& state=chains[c];const auto& chain=asset->chains[c];
                state.old=state.position;state.position[0]=state.goal[0];
                if(stableContactVelocity)std::fill(state.contactDelta.begin(),state.contactDelta.end(),glm::dvec3(0));
                for(std::size_t i=1;i<state.position.size();++i){const auto& link=chain.links[i-1];
                    const auto spring=std::clamp(link.stiffness,0.,60.);
                    state.velocity[i]+=h*(settings.gravity+settings.wind+(state.goal[i]-state.position[i])*(spring*spring));
                    state.velocity[i]*=std::exp(-std::clamp(link.damping,0.,100.)*h);
                    state.position[i]+=state.velocity[i]*h;
                }
            }
            for(std::uint32_t iteration=0;iteration<iterations;++iteration){
                for(std::size_t c=0;c<chains.size();++c){auto& state=chains[c];const auto& links=asset->chains[c].links;
                    state.position[0]=state.goal[0];
                    for(std::size_t i=0;i<links.size();++i){
                        auto direction=Direction(state.position[i+1]-state.position[i],Direction(state.goal[i+1]-state.goal[i]));
                        if(!localJointLimits){
                            // Reference path: this world-space cone competes
                            // with the parent-local cone below when a parent
                            // has already moved. It can undo a valid contact
                            // rotation on every subsequent substep.
                            const auto goalDirection=Direction(state.goal[i+1]-state.goal[i]);
                            const auto angle=std::acos(std::clamp(glm::dot(direction,goalDirection),-1.,1.));
                            if(angle>links[i].maxAngle){const auto q=Between(goalDirection,direction);direction=glm::slerp(glm::dquat(1,0,0,0),q,links[i].maxAngle/angle)*goalDirection;}
                        }
                        const auto targetPosition=state.position[i]+direction*state.length[i];
                        if(i==0)state.position[i+1]=targetPosition;
                        else {const auto correction=(targetPosition-state.position[i+1])*.5;state.position[i]-=correction;state.position[i+1]+=correction;}
                    }
                    // PoseFromNodes projects each joint in ascending parent
                    // order using its animated local reference. This is the
                    // same cone used by contact and the sole angular limit;
                    // NodesFromPose then restores exact rigid segment lengths.
                    PoseFromNodes(c,animated);NodesFromPose(c);
                }
                // A PMX bone center may intentionally lie inside the skull. Its
                // authoring rigid-body radius is not a rendered surface radius:
                // pushing that center would make an otherwise valid strand float.
                // Physical contact therefore acts only on actual skin samples.
                const bool contactPass=coarseContacts?(sub+1==substeps&&iteration+1==iterations)
                    :(iteration+1==iterations||iteration==iterations/2);
                if(boneSurfaceContacts&&settings.collision&&contactPass){
                    SurfacePass(animated,settings,coarseContacts||(sub==0&&iteration==iterations/2));
                }
            }
            if(adaptiveContacts&&finalTick&&sub+1==substeps){Measure(animated,settings);RefineContacts(animated,settings);}
            for(std::size_t c=0;c<chains.size();++c){auto& state=chains[c];
                for(std::size_t i=1;i<state.position.size();++i){auto displacement=state.position[i]-state.old[i];
                    if(stableContactVelocity)displacement-=state.contactDelta[i];
                    state.velocity[i]=displacement/h;
                    const auto correction=stableContactVelocity?state.contactDelta[i]:glm::dvec3(0);
                    if(glm::dot(correction,correction)>1e-24){
                        // A skin constraint acts through bone rotations: its
                        // separation normal at a physical node is the actual
                        // Jacobian-mapped node correction, not the body normal
                        // applied indiscriminately to every descendant.
                        const auto normal=Direction(correction);const double inward=glm::dot(state.velocity[i],normal);
                        if(inward<0)state.velocity[i]-=normal*inward; // zero restitution
                        const auto normalVelocity=normal*glm::dot(state.velocity[i],normal);
                        state.velocity[i]=normalVelocity+(state.velocity[i]-normalVelocity)*std::exp(-8*std::clamp(settings.friction,0.,1.)*h);
                    }
                    state.velocity[i]*=1-std::clamp(settings.friction,0.,1.)*.08;
                    if(!Finite(state.position[i])||!Finite(state.velocity[i])){state.position[i]=state.goal[i];state.velocity[i]={0,0,0};}}
            }
        }
        if(!surface.Step(*asset,*target,solved,body,dt,settings,stats,error))return false;
        ++stats.steps;ObserveSettling(animated,settings,dt);return true;
    }
};

HairSimulator::HairSimulator():impl_(std::make_unique<Impl>()){}
HairSimulator::~HairSimulator()=default;
HairSimulator::HairSimulator(HairSimulator&&) noexcept=default;
HairSimulator& HairSimulator::operator=(HairSimulator&&) noexcept=default;
const HairSimulationStats& HairSimulator::Stats()const{return impl_->stats;}
const std::vector<glm::dvec3>& HairSimulator::Positions()const{return impl_->debugPositions;}
const std::vector<HairSurfaceOffset>& HairSimulator::SurfaceOffsets()const{return impl_->surface.Offsets();}

bool HairSimulator::Reset(const HairAsset& asset,const SkeletalAssetPackage& target,const SkeletalPose& pose,std::string* error){
    auto fail=[&](std::string text){if(error)*error=std::move(text);return false;};
    if(!impl_)impl_=std::make_unique<Impl>();auto& s=*impl_;s.initialized=false;
    if(!std::isfinite(asset.unitsPerMeter)||asset.unitsPerMeter<=0||asset.chains.empty())return fail("Invalid hair units or empty chain set");
    const auto count=target.skeleton.bones.size();
    if(pose.local.size()!=count||pose.global.size()!=count)return fail("Hair pose size differs from the target skeleton");
    std::vector<bool> used(count,false);
    for(const auto& chain:asset.chains){if(chain.links.empty()||chain.links.size()>64)return fail("Hair chain is empty or exceeds 64 links");
        if(chain.anchorBone>=std::int32_t(count))return fail("Hair anchor is outside target skeleton");
        for(const auto& link:chain.links){if(link.bone>=count||used[link.bone]||!Finite(link.restTip)||glm::dot(link.restTip,link.restTip)<Tiny||!std::isfinite(link.maxAngle)||link.maxAngle<0||!std::isfinite(link.stiffness)||!std::isfinite(link.damping))
                return fail("Invalid or duplicate hair link");
            const auto& local=pose.local[link.bone];const auto q=glm::dot(local.rotation,local.rotation);
            if(!Finite(local.translation)||!Finite(local.scale)||!std::isfinite(q)||q<Tiny||std::abs(local.scale.x*local.scale.y*local.scale.z)<Tiny)
                return fail("Hair animated transform is nonfinite or singular");
            for(int column=0;column<4;++column)for(int row=0;row<4;++row)if(!std::isfinite(pose.global[link.bone][column][row]))
                return fail("Hair animated global transform is nonfinite");
            used[link.bone]=true;}}
    for(const auto& p:asset.probes){if(p.chain>=asset.chains.size())return fail("Hair probe chain is invalid");
        for(const auto& i:p.influences)if(i.bone>=count||!Finite(i.weightedPosition)||!std::isfinite(i.weight)||i.weight<0)return fail("Invalid hair probe influence");}
    for(const auto& a:asset.attachments)if(a.chain>=asset.chains.size()||!target.geometry||a.mesh>=target.skins.size()
        ||a.mesh>=target.geometry->meshes.size()||a.vertex>=target.skins[a.mesh].vertices.size()||a.vertex>=target.geometry->meshes[a.mesh].vertices.size())
        return fail("Hair attachment vertex is outside target geometry");
    s.asset=&asset;s.target=&target;s.units=asset.unitsPerMeter;s.accumulator=0;s.solved=pose;
    const auto* cacheOption=std::getenv("MODEL_HAIR_CONTACT_CACHE");s.contactCache=!(cacheOption&&cacheOption[0]=='0'&&cacheOption[1]=='\0');
    const auto* adaptiveOption=std::getenv("MODEL_HAIR_ADAPTIVE_CONTACTS");s.adaptiveContacts=adaptiveOption&&adaptiveOption[0]=='1'&&adaptiveOption[1]=='\0';
    // Dense post-skin contact handles the rendered mesh; one final skeletal
    // pass supplies coarse motion. Explicit 0 retains the four-pass reference.
    const auto* coarseOption=std::getenv("MODEL_HAIR_COARSE_CONTACTS");s.coarseContacts=!(coarseOption&&coarseOption[0]=='0'&&coarseOption[1]=='\0');
    // A dense, independently constrained surface has more contact freedom
    // than a handful of hair rotations. For authored scalp attachments, hard
    // sequential bone contacts compete and keep otherwise resting hair moving.
    // Legacy assets without a usable surface retain their skeletal contacts.
    const bool hasFineGeometry=target.geometry&&std::any_of(asset.hairMeshes.begin(),asset.hairMeshes.end(),[&](const auto mesh){
        return mesh<target.geometry->meshes.size()&&mesh<target.skins.size()&&!target.geometry->meshes[mesh].vertices.empty()
            &&target.skins[mesh].vertices.size()==target.geometry->meshes[mesh].vertices.size();});
    s.boneSurfaceContacts=asset.attachments.empty()||!hasFineGeometry;
    const auto* boneContactOption=std::getenv("MODEL_HAIR_BONE_SURFACE_CONTACTS");
    if(boneContactOption&&(boneContactOption[0]=='0'||boneContactOption[0]=='1')&&boneContactOption[1]=='\0')s.boneSurfaceContacts=boneContactOption[0]=='1';
    // Authored scalp attachments require restrained coarse steering; the dense
    // mesh layer still collides every free surface against the complete body.
    s.boneAnchorContacts=asset.attachments.empty();s.boneFreeProbes=!asset.attachments.empty();
    const auto* anchorContactOption=std::getenv("MODEL_HAIR_BONE_ANCHOR_CONTACTS");
    if(anchorContactOption&&(anchorContactOption[0]=='0'||anchorContactOption[0]=='1')&&anchorContactOption[1]=='\0')s.boneAnchorContacts=anchorContactOption[0]=='1';
    const auto* freeProbeOption=std::getenv("MODEL_HAIR_BONE_FREE_PROBES");
    if(freeProbeOption&&(freeProbeOption[0]=='0'||freeProbeOption[0]=='1')&&freeProbeOption[1]=='\0')s.boneFreeProbes=freeProbeOption[0]=='1';
    const auto* stableVelocityOption=std::getenv("MODEL_HAIR_STABLE_CONTACT_VELOCITY");s.stableContactVelocity=!(stableVelocityOption&&stableVelocityOption[0]=='0'&&stableVelocityOption[1]=='\0');
    const auto* jointLimitOption=std::getenv("MODEL_HAIR_LOCAL_JOINT_LIMITS");s.localJointLimits=!(jointLimitOption&&jointLimitOption[0]=='0'&&jointLimitOption[1]=='\0');
    const auto* measurementOption=std::getenv("MODEL_HAIR_MEASUREMENT_REUSE");s.measurementReuse=!(measurementOption&&measurementOption[0]=='0'&&measurementOption[1]=='\0');
    const auto* sleepOption=std::getenv("MODEL_HAIR_SLEEP");s.sleepEnabled=!(sleepOption&&sleepOption[0]=='0'&&sleepOption[1]=='\0');
    s.sleepEligible=!s.boneSurfaceContacts&&!asset.attachments.empty()&&hasFineGeometry;
    s.settledSeconds=0;s.settledTicks=0;s.wakePending=false;s.physicalInput=pose;s.physicalSettings=asset.settings;
    const auto previousResets=s.stats.resets;s.stats={};s.stats.resets=previousResets+1;
    s.stats.chains=asset.chains.size();s.stats.probes=asset.probes.size();
    s.chains.clear();s.chains.resize(asset.chains.size());s.probes.clear();s.probes.resize(asset.probes.size());
    s.attachments.clear();s.attachments.resize(asset.attachments.size());s.inverseAnchors.resize(asset.chains.size());
    s.boneChain.assign(count,-1);s.boneLink.assign(count,-1);s.simulatedAnimatedRotations.resize(count);std::size_t nodes=0;
    for(std::size_t c=0;c<asset.chains.size();++c){auto& state=s.chains[c];const auto& chain=asset.chains[c];const auto n=chain.links.size();
        state.position.resize(n+1);state.velocity.assign(n+1,glm::dvec3(0));state.old.resize(n+1);state.goal.resize(n+1);state.contactDelta.assign(n+1,glm::dvec3(0));state.length.resize(n);
        state.previousAnchor=chain.anchorBone>=0?pose.global[chain.anchorBone]:glm::dmat4(1);
        for(std::size_t i=0;i<n;++i){s.boneChain[chain.links[i].bone]=int(c);s.boneLink[chain.links[i].bone]=int(i);
            s.simulatedAnimatedRotations[chain.links[i].bone]=pose.local[chain.links[i].bone].rotation;}
        s.NodesFromPose(c);state.goal=state.position;nodes+=n+1;s.stats.bones+=n;
    }s.debugPositions.resize(nodes);
    // Index sparse morphs by mesh once. In this character hair has no morph
    // deltas, so adding attachment diagnostics does not rescan facial morphs
    // separately for every vertex during each timeline reset.
    s.meshMorphs.clear();s.meshMorphs.resize(target.skins.size());
    std::vector<bool> neededMorphMesh(target.skins.size(),false);
    for(const auto& p:asset.probes)if(p.mesh<neededMorphMesh.size())neededMorphMesh[p.mesh]=true;
    for(const auto& a:asset.attachments)neededMorphMesh[a.mesh]=true;
    for(std::uint32_t m=0;m<target.morphs.size();++m)for(const auto& delta:target.morphs[m].deltas)
        if(delta.mesh<s.meshMorphs.size()&&neededMorphMesh[delta.mesh])s.meshMorphs[delta.mesh].push_back({m,delta.vertex,delta.position});
    for(std::size_t p=0;p<asset.probes.size();++p)s.CacheMorphs(asset.probes[p],s.probes[p]);
    for(std::size_t a=0;a<asset.attachments.size();++a){const auto& source=asset.attachments[a];auto& cached=s.attachments[a];
        auto& probe=cached.probe;probe.mesh=source.mesh;probe.vertex=source.vertex;probe.chain=source.chain;
        const auto& skin=target.skins[source.mesh];const auto& vertex=target.geometry->meshes[source.mesh].vertices[source.vertex];
        const auto& weights=skin.vertices[source.vertex];double sum=0;
        for(int k=0;k<4;++k)if(weights.weights[k]>0&&weights.joints[k]<count)sum+=weights.weights[k];
        if(sum<=0)return fail("Hair attachment has no valid skin influence");
        for(int k=0;k<4;++k){const auto bone=weights.joints[k];if(weights.weights[k]<=0||bone>=count)continue;const auto weight=weights.weights[k]/sum;
            const auto offset=bone<skin.boneFromMesh.size()?skin.boneFromMesh[bone]:target.skeleton.bones[bone].inverseBind*skin.bindTransform;
            probe.influences.push_back({bone,weight,glm::dvec3(offset*glm::dvec4(vertex.position,1))*weight});}
        s.CacheMorphs(probe,cached.cached);
    }
    s.chainAffectedProbes.clear();s.chainAffectedProbes.resize(asset.chains.size());
    for(std::uint32_t p=0;p<asset.probes.size();++p){
        for(std::size_t c=0;c<asset.chains.size();++c){bool affects=false;
            for(const auto& influence:asset.probes[p].influences)affects|=influence.weight>0&&s.boneChain[influence.bone]==int(c);
            for(const auto& morph:s.probes[p].morph)affects|=s.boneChain[morph.bone]==int(c);
            if(affects)s.chainAffectedProbes[c].push_back(p);
        }
    }
    s.adaptiveCandidates.clear();s.adaptiveCandidates.reserve(asset.probes.size());s.adaptiveAffected.assign(asset.probes.size(),false);
    if(!s.body.Initialize(target,asset.hairMeshes,s.units,error)||!s.body.Update(pose,error))return false;s.NewBodyPose(true);
    s.body.ResetMotion();s.stats.bodyTriangles=s.body.TriangleCount();s.stats.collisionMilliseconds=s.body.LastUpdateMilliseconds();
    for(std::size_t p=0;p<asset.probes.size();++p){const auto point=s.Point(asset.probes[p],s.probes[p],pose);s.probes[p].previous=point;
        ++s.stats.collisionQueries;const auto nearest=s.body.Nearest(point,.08,&s.probes[p].bodyHint);
        s.Certificate(s.probes[p],point,nearest.found&&!nearest.inside?nearest.distance:0.);
        if(nearest.found)s.stats.sourcePenetration=std::max(s.stats.sourcePenetration,std::max(0.,asset.settings.thickness-nearest.signedDistance));}
    if(s.boneSurfaceContacts&&asset.settings.collision)for(int i=0;i<8;++i)s.SurfacePass(pose,asset.settings,false);
    for(std::size_t c=0;c<s.chains.size();++c)s.NodesFromPose(c);
    s.Measure(pose,asset.settings);
    // Reset has no time integration: the same bounded contact repair may
    // settle its initial shape while every initialized velocity stays zero.
    if(s.adaptiveContacts)s.RefineContacts(pose,asset.settings);
    if(!s.surface.Initialize(asset,target,error)||!s.surface.Reset(asset,target,s.solved,s.body,s.stats,error))return false;
    s.CaptureSettledState();
    s.initialized=true;if(error)error->clear();return true;
}

bool HairSimulator::Step(const HairAsset& asset,const SkeletalAssetPackage& target,const SkeletalPose& animated,double deltaSeconds,
    const HairSolverSettings& settings,SkeletalPose& output,std::string* error){
    const auto start=Clock::now();
    auto fail=[&](std::string text){if(error)*error=std::move(text);return false;};
    if(!std::isfinite(deltaSeconds)||deltaSeconds<0)return fail("Hair delta time must be finite and nonnegative");
    if(!Finite(settings.gravity)||!Finite(settings.wind)||!std::isfinite(settings.thickness)||settings.thickness<0||!std::isfinite(settings.inertia)||!std::isfinite(settings.friction))
        return fail("Hair settings contain nonfinite or negative contact parameters");
    if(animated.local.size()!=target.skeleton.bones.size()||animated.global.size()!=target.skeleton.bones.size())return fail("Hair pose size differs from target skeleton");
    for(const auto& chain:asset.chains)for(const auto& link:chain.links){
        if(link.bone>=animated.local.size())return fail("Hair link is outside the animated skeleton");
        const auto& local=animated.local[link.bone];const auto q=glm::dot(local.rotation,local.rotation);
        if(!Finite(local.translation)||!Finite(local.scale)||!std::isfinite(q)||q<Tiny||std::abs(local.scale.x*local.scale.y*local.scale.z)<Tiny)
            return fail("Hair animated transform is nonfinite or singular");
    }
    if(!impl_||!impl_->initialized||impl_->asset!=&asset||impl_->target!=&target)
        if(!Reset(asset,target,animated,error))return false;
    auto& s=*impl_;s.stats.contacts=0;s.stats.collisionMilliseconds=0;s.stats.maxSurfaceCorrection=0;s.stats.collisionQueries=0;s.stats.clearanceSkips=0;s.stats.measurementReuses=0;
    s.stats.adaptiveQueries=0;s.stats.adaptiveRounds=0;s.stats.adaptiveBudgetExhausted=false;
    s.WakeForInput(animated,settings);
    const auto fixed=std::isfinite(settings.fixedTimeStep)&&settings.fixedTimeStep>0?std::clamp(settings.fixedTimeStep,1./240.,1./15.):1./60.;
    s.accumulator+=deltaSeconds;
    const bool tickDue=deltaSeconds>0&&s.accumulator+1e-12>=fixed;
    if(tickDue&&s.stats.sleeping&&!s.wakePending){
        // Consume the same bounded logical fixed clock without refitting a
        // body or advancing an already converged physical history. A changed
        // input wakes above, including changes seen only by Step(0).
        std::uint32_t ticks=0;while(s.accumulator+1e-12>=fixed&&ticks<4){
            s.accumulator=std::max(0.,s.accumulator-fixed);++s.stats.steps;++s.stats.sleepTicks;++ticks;
        }
        s.stats.surfaceContacts=s.stats.surfaceSweeps=s.stats.surfaceBroadphaseRejects=0;s.stats.surfaceMilliseconds=0;
    }else if(tickDue){
        if(!s.body.Update(animated,error))return false;
        s.NewBodyPose();
        s.stats.collisionMilliseconds=s.body.LastUpdateMilliseconds();
        // The worker supplies complete fixed animation ticks. A direct caller
        // may accumulate fractions; cap catch-up without creating an unbounded
        // blocking loop, retaining all remaining time for the next invocation.
        std::uint32_t ticks=0;
        while(s.accumulator+1e-12>=fixed&&ticks<4){
            if(ticks){
                // The source body moved to this endpoint only for the first
                // tick. Catch-up ticks hold that endpoint and sweep hair from
                // the preceding physical tick, rather than replaying the same
                // source-body motion and old probe trajectory several times.
                if(ticks==1)s.body.ResetMotion();
                for(std::size_t p=0;p<asset.probes.size();++p)
                    s.probes[p].previous=s.Point(asset.probes[p],s.probes[p],s.solved);
            }
            const bool finalTick=ticks==3||s.accumulator+1e-12<2*fixed;
            if(!s.Tick(animated,settings,fixed,finalTick,error))return false;
            s.accumulator=std::max(0.,s.accumulator-fixed);++ticks;
        }
        if(!s.adaptiveContacts)s.Measure(animated,settings);
    }
    if(tickDue)output=s.solved;
    else {s.Present(animated,output);if(!s.surface.Step(asset,target,output,s.body,0,settings,s.stats,error))return false;}
    s.stats.simulationMilliseconds=Milliseconds(start);if(error)error->clear();return true;
}
} // namespace Model
