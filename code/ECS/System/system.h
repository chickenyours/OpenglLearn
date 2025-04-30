#pragma once

#include <string>

#include "code/ECS/data_type.h"

#include "code/ECS/Component/component.h"
#include "code/ECS/Component/component_shortage.h"
#include "code/ECS/Component/component_register.h"


namespace ECS::System{

class System{
    public:
        inline void SetComponentRegister(ECS::Core::ComponentRegister* reg){reg_ = reg;}
    protected:
        inline System(std::string systemName) : systemName_(systemName){}

        // 会检查entity是否存在某个组件,如果不存在则会添加
        // template <typename ComponentT,std::enable_if_t<std::is_base_of<Component::Component, ComponentT>::value, int> = 0>
        // inline bool CheckAndAddComponent(EntityID entity){
            
        // }

        ECS::Core::ComponentRegister* reg_;
        std::string systemName_;
};


}