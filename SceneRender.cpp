#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/Component/component_register.h"
#include "code/ECS/Entity/entity.h"

#include "code/ECS/Component/Transform/transform.h"

int main(){

    ECS::Core::ComponentRegister reg;
    
    for(unsigned int i = 1;i<= 20 ; i++){
        ECS::Component::Transform trans;
        trans.pos = glm::vec3(static_cast<float>(i));
        reg.AddComponent<ECS::Component::Transform>(static_cast<ECS::EntityID>(i),trans);
    }

    auto entityList = reg.View<ECS::Component::Transform>();
    for(auto it : entityList){
        std::cout 
        << reg.GetComponent<ECS::Component::Transform>(it).pos.z
        << std::endl;
    }

    return 0;
}