#pragma once
#include "IWanna/Public/game_module.h"
#include "IWanna/Public/collision.h"
#include "IWanna/Public/transform_math.h"

namespace IWanna {
class InputSystem final : public ECS::System::System {
public:
    InputSystem():System("IWanna.Input",ECS::System::Phase::PreUpdate) {
        Writes<Motion>();Writes<Player>();Writes<Sprite>();Writes<Transform>();Writes<Collider>();Writes<Behavior>();
    }
    void OnTick() override {
        auto& g=*GetContext()->GetService<GameModule>();
        if(g.input.restart) g.Restart();
        if(g.input.any || g.input.left || g.input.right || g.input.jump) for(const auto& view:g.Views())
            if(view.behavior->role==Role::Intro) view.sprite->visible=false;
        if(g.GetState()!=State::Playing) return;
        auto id=g.PlayerEntity();auto& velocity=g.Get<Motion>(id).velocity;auto& player=g.Get<Player>(id);
        velocity.x=(int(g.input.right)-int(g.input.left))*g.rules.runSpeed;
        if(velocity.x)player.facingLeft=velocity.x<0;
        if(velocity.x) g.Get<Sprite>(id).flipX=g.FacesLeft()?(velocity.x>0):(velocity.x<0);
        // Walking off a ledge or spawning in midair consumes the ground jump.
        // Only a grounded takeoff may use both jumps in the same airtime.
        if(!player.grounded && player.jumps==0)player.jumps=1;
        if(g.input.jump && player.jumps<2) {
            velocity.y=-(player.jumps==0?g.rules.jumpSpeed:g.rules.doubleJumpSpeed);
            ++player.jumps;player.grounded=false;player.jumpHoldRemaining=g.rules.jumpHoldSeconds;player.jumpJustStarted=true;
            g.Sound(player.jumps==1?"jump1":"jump2");
        }
        g.SelectPlayerAnimation();
    }
};
class PhysicsSystem final : public ECS::System::System {
    static Transform Detection(const EntityView& view) {
        auto t=*view.transform;if(view.collider->detectionBox){t.position+=RotateOffset(view.collider->detectionOffset,t.rotation);t.size=view.collider->detectionSize;}return t;
    }
    std::vector<const EntityView*> solids_,triggers_,moving_;
public:
    PhysicsSystem():System("IWanna.Physics") {
        Writes<Transform>();Writes<Motion>();Writes<Player>();Writes<Collider>();Writes<Behavior>();Writes<Sprite>();
    }
    void OnTick() override {
        auto& g=*GetContext()->GetService<GameModule>();if(g.GetState()!=State::Playing)return;
        auto player=g.PlayerEntity();auto& t=g.Get<Transform>(player);auto& velocity=g.Get<Motion>(player).velocity;
        auto& p=g.Get<Player>(player);const auto& settings=g.CollisionSettings();
        const auto& rectangle=RectangleMask();
        const bool wasGrounded=p.grounded;
        solids_.clear();triggers_.clear();moving_.clear();
        for(const auto& view:g.Views()) {
            if(view.id==player)continue;
            if(view.behavior->role==Role::Projectile)continue;
            if(view.behavior->role==Role::Hazard || view.motion->velocity!=glm::vec2(0))moving_.push_back(&view);
            if(!view.collider->enabled)continue;
            (view.behavior->role==Role::Solid?solids_:triggers_).push_back(&view);
        }
        auto solidHit=[&]() -> const EntityView* {
            auto body=g.PlayerBody();body.size-=glm::vec2(2*settings.skin);
            for(auto view:solids_) {
                if(!view->collider->enabled)continue;
                const auto other=Detection(*view);
                if(other.rotation==0 && (std::abs(body.position.x-other.position.x)>(body.size.x+other.size.x)*.5f ||
                    std::abs(body.position.y-other.position.y)>(body.size.y+other.size.y)*.5f))continue;
                if(MaskOverlap(body,rectangle,other,view->collider->detectionBox?rectangle:*view->sprite))return view;
            }
            return nullptr;
        };
        auto touchSolid=[&](const EntityView* contact) {
            if(contact&&(!contact->behavior->target.empty()||!contact->behavior->event.empty()))g.Touch(contact->id);
        };
        // Resolve a small existing penetration before casting either axis.
        // The previous solver assumed a clear start, so any overlap could stop
        // BOTH axes indefinitely. Prefer the shortest clear correction and do
        // not turn it into velocity (in particular, no wall-climbing impulse).
        auto recover=[&] {
            const auto* overlap=solidHit();if(!overlap)return;
            // This is an actual overlap before correction. The directional
            // probes below must never generate gameplay contact events.
            touchSolid(overlap);
            const auto start=t.position;float best=settings.recoveryDistance+settings.skin;
            glm::vec2 correction{};
            for(auto direction:{glm::vec2(-1,0),glm::vec2(1,0),glm::vec2(0,-1),glm::vec2(0,1)}) {
                float previous=0;
                const int steps=int(std::ceil(settings.recoveryDistance/.01f));
                for(int i=1;i<=steps;++i) {
                    float distance=settings.recoveryDistance*float(i)/steps;if(distance>=best)break;
                    t.position=start+direction*distance;
                    if(!solidHit()) {
                        float blocked=previous,clear=distance;
                        for(int n=0;n<12;++n) {
                            float mid=(blocked+clear)*.5f;t.position=start+direction*mid;
                            if(solidHit())blocked=mid;else clear=mid;
                        }
                        float padded=std::min(clear+settings.skin,settings.recoveryDistance);
                        t.position=start+direction*padded;
                        if(solidHit())padded=clear;
                        if(padded<best){best=padded;correction=direction*padded;}
                        break;
                    }
                    previous=distance;
                }
            }
            t.position=start+correction;
            // Keep tangential movement, cancel only motion into the surface.
            for(int axis=0;axis<2;++axis)if(correction[axis]*velocity[axis]<0)velocity[axis]=0;
        };
        recover();
        // Cast one axis from a non-overlapping position. A small separation
        // margin prevents float rounding at a wall from blocking the Y cast.
        auto castAxis=[&](int axis,float displacement) -> const EntityView* {
            float left=std::abs(displacement);const float sign=std::copysign(1.f,displacement);
            while(left>1e-7f) {
                const float delta=sign*std::min(left,g.rules.maxSubstepDistance);left-=std::abs(delta);
                const float start=t.position[axis];t.position[axis]=start+delta;
                const auto* contact=solidHit();if(!contact)continue;
                float safe=0,blocked=1;
                for(int n=0;n<12;++n) {
                    float middle=(safe+blocked)*.5f;t.position[axis]=start+delta*middle;
                    if(solidHit())blocked=middle;else safe=middle;
                }
                const float distance=std::max(0.f,std::abs(delta)*safe-settings.skin);
                t.position[axis]=start+sign*distance;
                return contact;
            }
            return nullptr;
        };
        const float dt=float(GetContext()->deltaSeconds);
        if(velocity.y<0 && p.jumpHoldRemaining>0 && !g.input.jumpHeld && !p.jumpJustStarted){
            velocity.y*=g.rules.jumpReleaseMultiplier;p.jumpHoldRemaining=0;
        }
        float gravityStep=g.rules.gravity*dt;
        if(velocity.y<0 && p.jumpHoldRemaining>0 && g.input.jumpHeld){
            const float held=std::min(p.jumpHoldRemaining,dt);
            gravityStep=g.rules.gravity*(held*g.rules.jumpHoldGravityScale+(dt-held));
            p.jumpHoldRemaining-=held;
        }
        velocity.y=std::min(velocity.y+gravityStep,g.rules.maxFallSpeed);
        if(velocity.y>=0)p.jumpHoldRemaining=0;
        p.jumpJustStarted=false;
        p.grounded=false;
        float remaining=dt;
        while(remaining>1e-7f && g.GetState()==State::Playing) {
            float movingSpeed=0;
            for(auto view:moving_) movingSpeed=std::max(movingSpeed,glm::length(view->motion->velocity));
            const float h=std::min(remaining,g.rules.maxSubstepDistance/std::max(glm::length(velocity)+movingSpeed,1.f));
            remaining-=h;
            for(auto view:moving_) view->transform->position+=view->motion->velocity*h;
            recover();
            // Resolve each axis independently. Never project horizontal velocity
            // onto a sloping polygon normal (the previous implementation's lift).
            for(int axis=0;axis<2;++axis) {
                float displacement=velocity[axis]*h;if(std::abs(displacement)<1e-9f)continue;
                const auto start=t.position;
                const auto* contact=castAxis(axis,displacement);
                if(!contact)continue;
                // Record the real attempted movement's contact even when the
                // solver subsequently steps over or slides around the edge.
                // castAxis itself stays side-effect free for trial paths.
                touchSolid(contact);
                // A tiny side protrusion must not become a shelf while falling
                // beside a wall. Permit a bounded sideways correction, never
                // vertical lift, and verify both the lateral and vertical path.
                if(axis==1 && !wasGrounded && !p.grounded && settings.wallSlide>0) {
                    const auto stopped=t.position;bool slid=false;
                    const int steps=int(std::ceil(settings.wallSlide/g.rules.maxSubstepDistance));
                    for(int i=1;i<=steps && !slid;++i)for(float direction:{-1.f,1.f}) {
                        const float offset=direction*settings.wallSlide*float(i)/steps;
                        t.position=start;
                        if(castAxis(0,offset))continue;
                        if(!castAxis(1,displacement)){slid=true;break;}
                    }
                    if(slid)continue;
                    t.position=stopped;
                }
                // Cross tiny tile height mismatches only when supported. Test
                // the upward path as well so a low ceiling cannot be bypassed.
                if(axis==0 && (wasGrounded||p.grounded) && velocity.y>=0 && settings.stepHeight>0) {
                    const auto stopped=t.position;t.position=start;
                    t.position.y+=settings.groundSnap+settings.skin;
                    const bool supported=solidHit()!=nullptr;t.position=start;
                    bool stepped=false;
                    if(supported) {
                        const int steps=int(std::ceil(settings.stepHeight/g.rules.maxSubstepDistance));
                        for(int i=1;i<=steps;++i) {
                            const float lift=settings.stepHeight*float(i)/steps;
                            t.position=start;t.position.y-=lift;if(solidHit())break;
                            t.position.x+=displacement;
                            if(solidHit())continue;
                            if(const auto* landing=castAxis(1,lift+settings.groundSnap)) {
                                stepped=true;p.grounded=true;velocity.y=0;touchSolid(landing);break;
                            }
                        }
                    }
                    if(stepped)continue;
                    t.position=stopped;
                }
                if(axis==1) {p.jumpHoldRemaining=0;if(displacement>0){p.grounded=true;p.jumps=0;}}
                velocity[axis]=0;
            }
            bool killed=false;
            const auto body=g.PlayerBody();
            for(auto view:triggers_) {
                const auto& mask=(view->collider->detectionBox||view->behavior->role==Role::Trigger||view->behavior->role==Role::Exit)?rectangle:*view->sprite;
                if(!view->collider->enabled || !MaskOverlap(body,rectangle,Detection(*view),mask))continue;
                if(view->behavior->role==Role::Hazard) {killed=true;break;}
                g.Touch(view->id);
            }
            const bool boundary=!killed&&g.IsRoomGame()&&g.World()->CrossBoundary(t.position);
            bool outside=!g.IsRoomGame()&&(t.position.y>45 || t.position.x < -100 || t.position.x>100);
            if(killed || outside)g.Kill();
            if(boundary)remaining=0;
        }
        if(g.GetState()==State::Playing && (wasGrounded||p.grounded) && velocity.y>=0 && settings.groundSnap>0) {
            const auto start=t.position;
            // Probe in substeps rather than jumping through a thin platform.
            float left=settings.groundSnap;bool landed=false;
            while(left>1e-6f) {
                const float distance=std::min(left,g.rules.maxSubstepDistance);left-=distance;
                if(const auto* contact=castAxis(1,distance)) {
                    landed=true;p.grounded=true;p.jumps=0;p.jumpHoldRemaining=0;velocity.y=0;
                    touchSolid(contact);
                    break;
                }
            }
            if(!landed)t.position=start;
        }
        // Ground snapping can move the body after the last normal trigger pass.
        if(g.GetState()==State::Playing) {
            const auto body=g.PlayerBody();
            for(auto view:triggers_) {
                const auto& mask=(view->collider->detectionBox||view->behavior->role==Role::Trigger||view->behavior->role==Role::Exit)?rectangle:*view->sprite;
                if(!view->collider->enabled || !MaskOverlap(body,rectangle,Detection(*view),mask))continue;
                if(view->behavior->role==Role::Hazard){g.Kill();break;}
                g.Touch(view->id);
            }
        }
        // Keep this collision frame until it is rendered. Animation selection
        // happens before the next physics tick, never after collision checks.
    }
};
class ProjectileSystem final : public ECS::System::System {
    struct Target {const EntityView* view;Transform detection;glm::vec2 lo,hi;};
    std::vector<Target> targets_;
public:
    ProjectileSystem():System("IWanna.Projectiles") {
        Writes<Transform>();Writes<Collider>();Writes<Sprite>();Writes<Lifetime>();Reads<Motion>();Reads<Behavior>();Reads<ShotReceiver>();
    }
    void OnTick() override {
        auto& g=*GetContext()->GetService<GameModule>();
        if(!g.World()||g.GetState()!=State::Playing)return;
        const float dt=float(GetContext()->deltaSeconds);
        targets_.clear();
        for(const auto& v:g.Views()) {
            if(!v.collider->enabled||v.behavior->role==Role::Projectile||v.behavior->role==Role::Player)continue;
            if(v.behavior->role!=Role::Solid&&!v.receiver->enabled)continue;
            auto t=*v.transform;
            if(v.collider->detectionBox){t.position+=RotateOffset(v.collider->detectionOffset,t.rotation);t.size=v.collider->detectionSize;}
            const float angle=t.rotation*.01745329252f,cs=std::abs(std::cos(angle)),sn=std::abs(std::sin(angle));
            const glm::vec2 half=glm::vec2(cs*t.size.x+sn*t.size.y,sn*t.size.x+cs*t.size.y)*.5f;
            targets_.push_back({&v,t,t.position-half,t.position+half});
        }
        for(const auto& v:g.Views()) {
            if(v.behavior->role==Role::Projectile&&v.collider->enabled) {
                auto& t=*v.transform;const glm::vec2 start=t.position;
                const float alive=v.lifetime->remaining<0?dt:std::min(dt,v.lifetime->remaining);
                const glm::vec2 delta=v.motion->velocity*std::max(0.f,alive);
                const auto half=t.size*.5f;
                const auto lo=glm::min(start,start+delta)-half,hi=glm::max(start,start+delta)+half;
                std::vector<const Target*> candidates;
                // Broad phase runs once per projectile/tick, not at every small cast step.
                for(const auto& target:targets_)if(lo.x<target.hi.x&&hi.x>target.lo.x&&lo.y<target.hi.y&&hi.y>target.lo.y)candidates.push_back(&target);
                const float step=std::min(g.rules.maxSubstepDistance,std::min(t.size.x,t.size.y)*.25f);
                const int steps=std::max(1,int(std::ceil(glm::length(delta)/step)));
                bool stopped=false;
                // Test the muzzle before advancing, so a nearby wall cannot be skipped.
                for(int n=0;n<=steps&&!stopped;++n) {
                    t.position=start+delta*(float(n)/steps);
                    // Opaque solids occlude receivers at the same sample position.
                    for(int pass=0;pass<2&&!stopped;++pass)for(const auto* target:candidates) {
                        const auto& other=*target->view;
                        if(!other.collider->enabled||(other.behavior->role==Role::Solid)!=(pass==0))continue;
                        if(!MaskOverlap(t,RectangleMask(),target->detection,other.collider->detectionBox?RectangleMask():*other.sprite))continue;
                        if(other.receiver->enabled)g.World()->Hit(other.id,v.id);
                        g.World()->Retire(v.id);stopped=true;break;
                    }
                }
            }
            if(v.lifetime->remaining>=0) {
                v.lifetime->remaining=std::max(0.f,v.lifetime->remaining-dt);
                if(v.lifetime->remaining==0)g.World()->Retire(v.id);
            }
        }
    }
};
class AnimationSystem final : public ECS::System::System {
public:
    AnimationSystem():System("IWanna.Animation",ECS::System::Phase::PreUpdate) {Writes<Sprite>();}
    void OnTick() override {
        auto& g=*GetContext()->GetService<GameModule>();
        for(const auto& view:g.Views()) {
            auto& s=*view.sprite;if(s.frames.size()<2)continue;
            const float duration=std::max(s.duration,.001f),next=s.elapsed+float(GetContext()->deltaSeconds);
            s.elapsed=s.loop?std::fmod(next,duration):std::min(next,duration);
        }
    }
};
}

