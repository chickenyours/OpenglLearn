#pragma once

#include "ForestFire/Public/forest_module.h"

namespace ForestFire {

class EvaluateSystem final : public ECS::System::System {
public:
    EvaluateSystem() : System("ForestFire.Evaluate", ECS::System::Phase::Update) {
        Reads<CellPosition>();
        Reads<Cell>();
        Writes<NextCell>();
    }
    void OnTick() override { GetContext()->GetService<ForestModule>()->Evaluate(); }
};

class CommitSystem final : public ECS::System::System {
public:
    CommitSystem() : System("ForestFire.Commit", ECS::System::Phase::Update) {
        Reads<NextCell>();
        Writes<Cell>();
    }
    void OnTick() override { GetContext()->GetService<ForestModule>()->Commit(); }
};

} // namespace ForestFire
