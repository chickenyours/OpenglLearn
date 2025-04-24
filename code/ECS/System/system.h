#pragma once 

#include "code/ECS/Component/component_shortage.h"
#include "code/ECS/Component/component_register.h"

namespace ECS::System{

class System{
    public:
        inline void SetComponentRegister(ECS::Core::ComponentRegister* reg){reg_ = reg;}
    protected:
        ECS::Core::ComponentRegister* reg_;
};


}