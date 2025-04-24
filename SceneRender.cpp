#include <iostream>

#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/Component/component_register.h"
#include "code/ECS/Entity/entity.h"

#include "code/ECS/Component/Transform/transform.h"

#include "code/ToolAndAlgorithm/container_algorithm.h"

#include "code/ECS/System/SceneTree/scene_tree.h"

ECS::Core::ComponentRegister reg;
ECS::System::SceneTreeSystem sceneTree;

void fun1(){
    std::vector<std::string> logs = {"LogA", "LogB", "LogC", "LogB", "LogAB" , "LogABC"};
    auto print = [&](){
        std::cout<<"========================"<<std::endl;
        for(auto& it : logs){
            std::cout<< it << std::endl;
        }
    };
    // 删除第一个 "LogB"
    Algorithm::UnorderValueEraseFirst(logs, std::string("LogB"));
    print();
    // 删除所有 "LogB"
    Algorithm::UnorderValueEraseFirst(logs, std::string("LogB"));
    print();
    // 删除所有长度为 4 的字符串
    Algorithm::StableEraseIf(logs, [](const std::string& s){ return s.size() == 4; });
    print();
}

void fun2(){
    for(unsigned int i = 1;i<= 20 ; i++){
        ECS::Component::Transform trans;
        trans.position = glm::vec3(static_cast<float>(i));
        reg.AddComponent<ECS::Component::Transform>(static_cast<ECS::EntityID>(i),trans);
    }

    auto entityList = reg.View<ECS::Component::Transform>();
    for(const auto& it : entityList){
        std::cout 
        << reg.GetComponent<ECS::Component::Transform>(it).position.z
        << std::endl;
    }
}

void fun3(){
    ECS::Entity a(1);
    ECS::Entity b(2);
    ECS::Entity c(3);
    ECS::Component::Transform ta;
    ta.scale = glm::vec3(2.0);
    ECS::Component::Transform tb;
    tb.position = glm::vec3(1.0,2.0,3.0);
    ECS::Component::Transform tc;
    tc.position = glm::vec3(2.0,3.0,4.0);
    reg.AddComponent<ECS::Component::Transform>(a.GetID(),ta);
    reg.AddComponent<ECS::Component::Transform>(b.GetID(),tb);
    reg.AddComponent<ECS::Component::Transform>(c.GetID(),tc);
    sceneTree.SetParent(b.GetID(),a.GetID());
    sceneTree.SetParent(c.GetID(),a.GetID()); 
    // sceneTree.
    // const auto& children = sceneTree.GetChildren(a.GetID());
    // for(auto& it : children){
    //     std::cout 
    //     << reg.GetComponent<ECS::Component::Transform>(it).position.z
    //     << std::endl;
    // }
}

void fun4(){
    std::unordered_map<int,std::vector<int>> a;
    a[1].push_back(2);
    std::cout<<a[1][0]<<std::endl;
    std::cout<<a.bucket_count()<<std::endl;
}

int main(){
    fun4();
    return 0;
}