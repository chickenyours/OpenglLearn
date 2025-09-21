#pragma once

#include <cmath>
#include "code/ECS/System/Collision/collision_system_base.h"
#include "code/ECS/Component/Collision/aabb_3D.h"

namespace ECS::System{
    class AABBSystem : public CollisionSystemBase<ECS::Component::AABB_3D> {
        protected:
            virtual bool Compare(
                ECS::Component::Transform& t1, 
                ECS::Component::AABB_3D& a1, 
                ECS::Component::Transform& t2,
                ECS::Component::AABB_3D& a2) override 
            {
                const float dx = std::fabs(t1.position.x - t2.position.x);
                const float dy = std::fabs(t1.position.y - t2.position.y);
                const float dz = std::fabs(t1.position.z - t2.position.z);

                const float ex = a1.length + a2.length;  // half-extent sum on X
                const float ey = a1.height + a2.height;  // half-extent sum on Y
                const float ez = a1.width  + a2.width;   // half-extent sum on Z

                // AABB 相交条件：三轴都重叠
                return (dx <= ex) && (dy <= ey) && (dz <= ez);
            }
    };

}
