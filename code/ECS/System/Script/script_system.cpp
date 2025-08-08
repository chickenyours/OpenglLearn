#include "script_system.h"

using namespace ECS::System;

ScriptSystem::ScriptSystem():System("Script"){
}


bool ScriptSystem::InitDerive(){
    return true;
}


bool ScriptSystem::AddEntity(EntityID entity){
    auto scriptComponent = scene_->registry_->GetComponent<ECS::Component::Script>(entity);
    if(scriptComponent && scriptComponent->scriptInsterface){
        entities.push_back(entity);
        scripts.push_back(scriptComponent);
        return true;
    }
    return false;
}


void ScriptSystem::Update(){
    for(int i = 0; i < entities.size(); i++){
        if(scripts[i]->state == 0){
            scripts[i]->scriptInsterface->OnStart(scene_,entities[i]);
            scripts[i]->state = 1;
        }
        else if(scripts[i]->state == 1){
            scripts[i]->scriptInsterface->OnUpdate(scene_,entities[i]);
        }
    }
}