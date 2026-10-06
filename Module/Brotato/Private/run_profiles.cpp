#include "Brotato/Public/game_world.h"

namespace Brotato {
const CharacterProfileDefinition& CharacterRules(const GameWorld& world,ECS::EntityHandle owner) {
    if(ProfileRulesEnabled(world.config) && world.scene)
        if(const auto* profile=world.scene->TryGetComponent<CharacterProfile>(owner); profile && profile->kind<CharacterProfiles.size())
            return CharacterProfiles[profile->kind];
    return CharacterProfiles[0];
}
const RunDifficultyDefinition& DifficultyRules(const GameWorld& world) {
    if(ProfileRulesEnabled(world.config) && world.scene)
        if(const auto* rules=world.scene->TryGetComponent<RunRules>(world.player); rules && rules->difficulty<RunDifficulties.size())
            return RunDifficulties[rules->difficulty];
    return RunDifficulties[0];
}
int ScaleRuleValue(int value,int percent) {
    // Integer ceiling avoids a rounded float turning an exact 6 HP into 7.
    return int(std::clamp<std::int64_t>((std::int64_t(std::max(0,value))*std::max(0,percent)+99)/100,0,100000));
}
int EnemyDamage(const GameWorld& world,int base) { return ScaleRuleValue(base,DifficultyRules(world).enemyDamage); }
float EnemySpeed(const GameWorld& world) { return DifficultyRules(world).enemySpeed/100.f; }
double SpawnDelay(const GameWorld& world,double base) { return std::max(SimulationStep,base*DifficultyRules(world).spawnDelay/100.); }
RunSetupSnapshot ExtractRunSetup(const GameWorld& world) {
    RunSetupSnapshot result;result.profiles=ProfileRulesEnabled(world.config);
    if(world.scene) {
        if(const auto* profile=world.scene->TryGetComponent<CharacterProfile>(world.player);profile && profile->kind<CharacterProfiles.size())result.character=profile->kind;
        if(result.profiles)if(const auto* rules=world.scene->TryGetComponent<RunRules>(world.player);rules && rules->difficulty<RunDifficulties.size())result.difficulty=rules->difficulty;
    }
    return result;
}
} // namespace Brotato
