#include "script_system.h"

using namespace ECS::System;

ScriptSystem::ScriptSystem():System("Script"){
}


bool ScriptSystem::InitDerive(){
    return true;
}


bool ScriptSystem::AddEntity(EntityID entity){
    if (map_.count(entity)) return true; // 已有就别再加
    auto scriptComponent = scene_->registry_->GetComponent<ECS::Component::Script>(entity);
    if(scriptComponent && scriptComponent->scriptInterface){
        entities_.push_back(entity);
        map_[entity] = scriptComponent;
        scripts_.push_back(scriptComponent);
        return true;
    }
    return false;
}

using EID = uint32_t; // 或 static_assert 确保宽度
static inline uint64_t make_key(EID a, EID b) {
    EID lo = (a < b) ? a : b;
    EID hi = (a < b) ? b : a;         // ✅ 直接与上行同判定
    return (uint64_t(hi) << 32) | uint64_t(lo);
}

void ScriptSystem::Update(){
    // 执行碰撞事件处理 cur本帧 / prev上帧
    auto& cur  = flip_ ? cacheA_ : cacheB_;
    auto& prev = flip_ ? cacheB_ : cacheA_;
    cur.clear();
    seen_.clear();
    seen_.reserve(queue.size());
    while (!queue.empty()) {
        auto ev = queue.front(); queue.pop();

        EID a = ev.entity1, b = ev.entity2;
        if (a == b) continue; // 可选：跳过自碰撞

        // 若需要仅脚本实体触发，可在此过滤：has_script(a/b)
        auto script_a_itor = map_.find(a);
        auto script_b_itor = map_.find(b);
        bool a_has_script = script_a_itor != map_.end();
        bool b_has_script = script_b_itor != map_.end();

        if (!a_has_script && !b_has_script) continue;

        uint64_t key = make_key(a, b);
        if (!seen_.insert(key).second) continue; // 去重

        bool was = prev.erase(key) > 0; // 从 prev 删除并判断是否存在
        cur.insert(key);

        if (a_has_script) {
            if (was) script_a_itor->second->scriptInterface->OnCollisionStay(scene_, a, b);
            else     script_a_itor->second->scriptInterface->OnCollisionStart(scene_, a, b);
        }
        if (b_has_script) {
            if (was) script_b_itor->second->scriptInterface->OnCollisionStay(scene_, b, a);
            else     script_b_itor->second->scriptInterface->OnCollisionStart(scene_, b, a);
        }
    }

    // prev 里剩下的都是“上帧有，这帧没”的 → Exit
    for (auto key : prev) {
        EID lo = (EID)(key & 0xffffffffu);
        EID hi = (EID)(key >> 32);

        // 分别给还“在场”的一方发 Exit（另一方可能已被移除）
        auto lo_script_itor = map_.find(lo);
        auto hi_script_itor = map_.find(hi);
        if (lo_script_itor != map_.end()) lo_script_itor->second->scriptInterface->OnCollisionExit(scene_, lo, hi);
        if (hi_script_itor != map_.end()) hi_script_itor->second->scriptInterface->OnCollisionExit(scene_, hi, lo);
    }
    prev.clear();

    flip_ = !flip_;

    // 正常更新逻辑
    for(int i = 0; i < entities_.size(); i++){
        if(scripts_[i]->state == 0){
            scripts_[i]->scriptInterface->OnStart(scene_,entities_[i]);
            scripts_[i]->state = 1;
        }
        else if(scripts_[i]->state == 1){
            scripts_[i]->scriptInterface->OnUpdate(scene_,entities_[i]);
        }
    }
}