#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/ECS/Command/command_buffer.h"
#include "engine/ECS/Query/query.h"

#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ECS::Core;
using ECS::EntityHandle;

template<class T> struct TestComponent : ECS::Component::Component<T> {
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return false; }
};
struct Position : TestComponent<Position> { unsigned identity = 0; };
struct Speed : TestComponent<Speed> { float value = 0; };
struct Disabled : TestComponent<Disabled> {};
using Positions = ChunkQuery<Require<Position>, Optional<Speed>, Exclude<Disabled>>;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class... T> ObjectWeakPtr<ArchType> Archetype(Scene& scene, std::size_t capacity = 2) {
    auto description = scene.CreateArchTypeDescription();
    (description->AddComponentArray<T>(), ...);
    return scene.CreateArchType(description, capacity);
}
Position& PositionOf(Scene& scene, EntityHandle entity) {
    return *scene.GetActiveComponent<Position>(entity.GetID()).Get();
}
std::vector<EntityHandle> Populate(Scene& scene, ObjectWeakPtr<ArchType> archetype, std::size_t count) {
    auto entities = scene.CreateEntities(archetype, count);
    for (auto entity : entities) PositionOf(scene, entity).identity = entity.GetID();
    return entities;
}

void CheckRows(Positions& query, const Scene& scene, std::size_t expected) {
    std::set<ECS::EntityID> visited;
    for (auto chunk : query) {
        Check(chunk.count <= chunk.archType->SizePerChunk(), "view must not cross a component chunk");
        for (std::size_t row = 0; row < chunk.count; ++row) {
            const auto entity = chunk.Entity(row, scene);
            Check(scene.IsAlive(entity), "row returns the full live generation");
            Check(entity.GetID() == chunk.Get<Position>()[row].identity, "dense component and entity row agree");
            Check(chunk.TryGet<Position>(row) == &chunk.Get<Position>()[row], "bounded row access agrees across all chunks and compacted rows");
            Check(chunk.TryGet<Speed>(row) == (chunk.Get<Speed>() ? &chunk.Get<Speed>()[row] : nullptr), "optional row access returns the right component or null");
            Check(visited.insert(entity.GetID()).second, "each matching entity is visited once");
        }
        Check(chunk.Entity(chunk.count, scene).GetID() == 0, "out-of-range row is invalid");
        Check(chunk.TryGet<Position>(chunk.count) == nullptr && chunk.TryGet<Speed>(std::numeric_limits<std::size_t>::max()) == nullptr,
              "component row bounds cannot point into another chunk or overflow");
        Check(chunk.Entity(std::numeric_limits<std::size_t>::max(), scene).GetID() == 0, "oversize row cannot overflow");
    }
    Check(visited.size() == expected && query.Count() == expected, "count includes all matching archetypes and chunks");
}

void TestRowsAcrossChunksAndSwapRemoval() {
    Scene scene;
    auto a = Archetype<Position>(scene, 2);
    auto b = Archetype<Position, Speed>(scene, 3);
    auto excluded = Archetype<Position, Disabled>(scene, 2);
    auto first = Populate(scene, a, 5);
    Populate(scene, b, 7);
    Populate(scene, excluded, 4);
    Positions query;
    query.Refresh(scene);
    Check(query.ArchTypeCount() == 2, "require and exclude select archetypes independent of their creation path");
    CheckRows(query, scene, 12);
    bool sawOptional = false, sawMissingOptional = false;
    for (auto chunk : query) {
        sawOptional |= chunk.Get<Speed>() != nullptr;
        sawMissingOptional |= chunk.Get<Speed>() == nullptr;
    }
    Check(sawOptional && sawMissingOptional, "optional data is exposed only when present");
    scene.DeleteEntity(first[1]);
    Check(!scene.IsAlive(first[1]), "removed dense row is retired");
    CheckRows(query, scene, 11);
    const auto replacement = Populate(scene, b, 1).front();
    Check(replacement.GetID() == first[1].GetID(), "test exercises an ID recycled into another archetype");
    Check(!scene.IsAlive(first[1]) && scene.IsAlive(replacement), "recycled row has a new generation");
    CheckRows(query, scene, 12);
    scene.DeleteEntity(first[1]);
    Check(scene.IsAlive(replacement), "old full handle cannot remove the recycled entity");
    Positions::ChunkView empty;
    Check(empty.Entity(0, scene).GetID() == 0, "default view has no entity");
    Check(empty.TryGet<Position>(0) == nullptr, "default view has no component row");
}

void TestSceneHandleResolution() {
    Scene scene;
    auto a = Archetype<Position>(scene);
    auto original = scene.CreateEntity(a);
    const auto resolved = scene.GetEntityHandle(original.GetID());
    Check(scene.IsAlive(resolved), "resolver preserves current generation");
    auto otherArchetype = Archetype<Position, Speed>(scene);
    Check(scene.IsAlive(scene.GetEntityHandle(original.GetID(), a.Get())), "resolver validates expected archetype");
    Check(scene.GetEntityHandle(original.GetID(), otherArchetype.Get()).GetID() == 0 &&
          scene.GetEntityHandle(original.GetID(), nullptr).GetID() == 0, "wrong and null archetype are rejected");
    Scene other;
    auto foreignArchetype = Archetype<Position>(other);
    const auto foreignEntity = other.CreateEntity(foreignArchetype);
    Check(original.GetID() == foreignEntity.GetID(), "test exercises equal entity IDs in separate scenes");
    Positions query; query.Refresh(scene);
    Check((*query.begin()).Entity(0, other).GetID() == 0, "query row rejects a different scene's same-ID entity");
    Check(!scene.IsAlive(EntityHandle(original.GetID())), "ID-only handle is not a current generation");
    scene.DeleteEntity(original);
    Check(scene.GetEntityHandle(original.GetID()).GetID() == 0, "dead ID resolves invalid");
    auto replacement = scene.CreateEntity(a);
    Check(replacement.GetID() == original.GetID(), "test exercises ID reuse");
    Check(!scene.IsAlive(resolved), "resolved old handle stays expired");
    Check(scene.IsAlive(scene.GetEntityHandle(replacement.GetID())), "resolver obtains recycled generation");
    scene.DeleteArchType(a);
    Check(scene.GetEntityHandle(replacement.GetID()).GetID() == 0, "archetype deletion retires its entity handle");
    Check(scene.GetEntityHandle(0).GetID() == 0 &&
          scene.GetEntityHandle(std::numeric_limits<ECS::EntityID>::max()).GetID() == 0,
          "invalid and nonexistent IDs resolve invalid");
}

void TestQueryRefreshAndSceneBinding() {
    Scene scene;
    auto a = Archetype<Position>(scene);
    Populate(scene, a, 3);
    Positions query;
    Check(query.RefreshIfNeeded(scene), "first query binds the scene");
    Check(!query.RefreshIfNeeded(scene), "unchanged archetypes reuse the query");
    auto b = Archetype<Position, Speed>(scene);
    Populate(scene, b, 4);
    Check(query.RefreshIfNeeded(scene), "new archetype refreshes the query");
    CheckRows(query, scene, 7);
    Populate(scene, a, 1);
    Check(!query.RefreshIfNeeded(scene) && query.Count() == 8, "new rows do not require archetype refresh");
    scene.DeleteArchType(a);
    Check(query.RefreshIfNeeded(scene), "removed archetype is dropped before dereferencing cached storage");
    CheckRows(query, scene, 4);
    Scene other;
    auto otherA = Archetype<Position>(other);
    auto otherB = Archetype<Position>(other);
    auto otherC = Archetype<Position>(other);
    Populate(other, otherA, 1);
    Check(scene.GetArchTypeVersion() == other.GetArchTypeVersion(), "test scenes have equal archetype versions");
    for (auto chunk : query)
        for (std::size_t row = 0; row < chunk.count; ++row)
            Check(chunk.Entity(row, other).GetID() == 0, "a row cannot resolve a same-ID entity in another scene");
    Check(query.RefreshIfNeeded(other), "equal version from another scene must still refresh");
    CheckRows(query, other, 1);
    query.Clear();
    Check(query.Count() == 0 && query.RefreshIfNeeded(other), "clear resets query binding");
}

void TestTryGetComponentUsesFullHandle() {
    Scene scene;
    auto firstArchetype = Archetype<Position>(scene);
    auto replacementArchetype = Archetype<Position, Speed>(scene);
    const auto original = scene.CreateEntity(firstArchetype);
    auto* position = scene.TryGetComponent<Position>(original);
    Check(position != nullptr, "current entity exposes its matching component");
    position->identity = 23;
    Check(scene.TryGetComponent<Position>(original)->identity == 23, "borrowed access refers to live storage");
    Check(scene.TryGetComponent<Speed>(original) == nullptr, "absent component is a normal miss");
    Check(scene.TryGetComponent<Position>(EntityHandle(original.GetID())) == nullptr,
          "ID-only handle cannot bypass generation validation");
    Check(scene.TryGetComponent<Position>(EntityHandle(0)) == nullptr, "invalid handle returns null");
    scene.DeleteEntity(original);
    Check(scene.TryGetComponent<Position>(original) == nullptr, "retired handle returns null");
    const auto replacement = scene.CreateEntity(replacementArchetype);
    Check(original.GetID() == replacement.GetID(), "test exercises recycled ID across archetypes");
    Check(scene.TryGetComponent<Position>(original) == nullptr && scene.TryGetComponent<Speed>(original) == nullptr,
          "stale generation cannot read replacement components");
    Check(scene.TryGetComponent<Position>(replacement) != nullptr && scene.TryGetComponent<Speed>(replacement) != nullptr,
          "current replacement accesses its own component combination");
    Check(scene.TryGetComponent<Disabled>(replacement) == nullptr, "replacement's missing component returns null");
    scene.DeleteArchType(replacementArchetype);
    Check(scene.TryGetComponent<Position>(replacement) == nullptr, "removed archetype invalidates borrowed lookup");
}

void TestPlaybackBoundaryAndOrdering() {
    Scene scene;
    auto a = Archetype<Position>(scene);
    auto original = Populate(scene, a, 1).front();
    Positions query; query.Refresh(scene);
    CommandBuffer commands(scene);
    EntityHandle replacement(0);
    std::vector<int> order;
    commands.Create(a, [&](Scene& world, EntityHandle entity) {
        Check(!world.IsAlive(original), "all destroys precede first create even when recorded later");
        PositionOf(world, entity).identity = entity.GetID();
        replacement = entity; order.push_back(1);
    });
    commands.Destroy(original);
    commands.Destroy(original);
    commands.Destroy(EntityHandle(original.GetID()));
    commands.Destroy(EntityHandle(0));
    commands.Create(a, [&](Scene& world, EntityHandle entity) {
        PositionOf(world, entity).identity = entity.GetID(); order.push_back(2);
    });
    Check(scene.IsAlive(original) && query.Count() == 1 && order.empty(), "recording does not mutate a query's world");
    auto result = commands.Playback();
    Check(result.destroyed == 1 && result.created == 2 && result.skipped == 3, "result counts applied and rejected commands");
    Check(order == std::vector<int>({1, 2}), "create initializers preserve recording order");
    Check(replacement.GetID() == original.GetID(), "deletion is visible to creation at the same boundary");
    Check(commands.Empty() && !scene.IsAlive(original), "successful batch is consumed");
    CheckRows(query, scene, 2);
    commands.Destroy(original);
    result = commands.Playback();
    Check(result.destroyed == 0 && result.skipped == 1 && scene.IsAlive(replacement), "stale queued deletion cannot remove replacement");
    result = commands.Playback();
    Check(result.created == 0 && result.destroyed == 0 && result.skipped == 0, "empty playback has no side effects");
}

void TestInvalidArchetypesAndClear() {
    Scene scene, other;
    auto a = Archetype<Position>(scene);
    auto expired = a;
    auto foreign = Archetype<Position>(other);
    CommandBuffer commands(scene);
    int initialized = 0;
    commands.Create(expired, [&](Scene&, EntityHandle) { ++initialized; });
    scene.DeleteArchType(a);
    commands.Create({}, [&](Scene&, EntityHandle) { ++initialized; });
    commands.Create(foreign, [&](Scene&, EntityHandle) { ++initialized; });
    const auto result = commands.Playback();
    Check(result.created == 0 && result.skipped == 3 && initialized == 0, "expired, null, and foreign archetypes are rejected");
    auto live = Archetype<Position>(scene);
    auto entity = scene.CreateEntity(live);
    commands.Destroy(entity); commands.Create(live);
    commands.Clear();
    Check(commands.Empty() && scene.IsAlive(entity) && live->ActiveCount() == 1, "clear discards without applying");
}

void TestInitializerFailureCleanup() {
    Scene scene;
    auto a = Archetype<Position>(scene);
    auto removed = scene.CreateEntity(a);
    CommandBuffer commands(scene);
    EntityHandle retained(0), failed(0);
    int laterInitializations = 0;
    std::weak_ptr<int> discardedCapture;
    commands.Destroy(removed);
    commands.Create(a, [&](Scene&, EntityHandle entity) { retained = entity; });
    commands.Create(a, [&](Scene&, EntityHandle entity) {
        failed = entity;
        commands.Create(a, [&](Scene&, EntityHandle) { ++laterInitializations; });
        commands.Destroy(retained);
        throw std::runtime_error("initializer failed");
    });
    {
        auto capture = std::make_shared<int>(7); discardedCapture = capture;
        commands.Create(a, [capture, &laterInitializations](Scene&, EntityHandle) { laterInitializations += *capture; });
    }
    bool caught = false;
    try { commands.Playback(); }
    catch (const std::runtime_error& error) { caught = std::string(error.what()) == "initializer failed"; }
    Check(caught, "initializer exception is preserved");
    Check(!scene.IsAlive(removed) && scene.IsAlive(retained), "completed earlier commands are not rolled back");
    Check(!scene.IsAlive(failed) && a->ActiveCount() == 1, "throwing initializer's entity is rolled back");
    Check(commands.Empty() && laterInitializations == 0 && discardedCapture.expired(), "both remaining and next batches release callbacks");
    commands.Playback();
    Check(scene.IsAlive(retained) && laterInitializations == 0, "later playback never replays partial batch");
    commands.Create(a);
    Check(commands.Playback().created == 1, "buffer remains usable after failure");
}

void TestRollbackRespectsRecycledGeneration() {
    Scene scene;
    auto a = Archetype<Position>(scene);
    CommandBuffer commands(scene);
    EntityHandle original(0), replacement(0);
    commands.Create(a, [&](Scene& world, EntityHandle entity) {
        original = entity;
        world.DeleteEntity(entity);
        replacement = world.CreateEntity(a);
        throw std::runtime_error("after reuse");
    });
    try { commands.Playback(); } catch (const std::runtime_error&) {}
    Check(original.GetID() == replacement.GetID() && !scene.IsAlive(original) && scene.IsAlive(replacement),
          "initializer rollback cannot delete a replacement generation");
}

void TestNextBatchAndRecursivePlayback() {
    Scene scene;
    auto a = Archetype<Position>(scene);
    CommandBuffer commands(scene);
    EntityHandle original(0);
    int callbacks = 0;
    commands.Create(a, [&](Scene&, EntityHandle entity) {
        original = entity; ++callbacks;
        commands.Destroy(entity);
        commands.Create(a, [&](Scene&, EntityHandle) { ++callbacks; });
        bool rejected = false;
        try { commands.Playback(); } catch (const std::logic_error&) { rejected = true; }
        Check(rejected, "recursive playback is rejected before consuming next batch");
    });
    auto result = commands.Playback();
    Check(result.created == 1 && callbacks == 1 && scene.IsAlive(original) && !commands.Empty(),
          "callback commands remain invisible until next boundary");
    result = commands.Playback();
    Check(result.created == 1 && result.destroyed == 1 && callbacks == 2 && !scene.IsAlive(original) && commands.Empty(),
          "next batch executes once at the next boundary");
    commands.Create(a, [&](Scene&, EntityHandle) { commands.Playback(); });
    bool rejected = false;
    try { commands.Playback(); } catch (const std::logic_error&) { rejected = true; }
    Check(rejected && commands.Empty() && a->ActiveCount() == 1, "uncaught recursion rolls back only the failing creation");
    commands.Create(a);
    Check(commands.Playback().created == 1, "recursion failure releases playback guard");
}
} // namespace

int main() {
    REGISTER_COMPONENT("ecs_test_position", Position);
    REGISTER_COMPONENT("ecs_test_speed", Speed);
    REGISTER_COMPONENT("ecs_test_disabled", Disabled);
    const std::array tests {
        std::pair{"rows across chunks and dense removal", TestRowsAcrossChunksAndSwapRemoval},
        std::pair{"generation-safe scene handles", TestSceneHandleResolution},
        std::pair{"query refresh and scene binding", TestQueryRefreshAndSceneBinding},
        std::pair{"generation-safe optional component access", TestTryGetComponentUsesFullHandle},
        std::pair{"command boundary and ordering", TestPlaybackBoundaryAndOrdering},
        std::pair{"invalid archetypes and clear", TestInvalidArchetypesAndClear},
        std::pair{"initializer failure cleanup", TestInitializerFailureCleanup},
        std::pair{"rollback after generation reuse", TestRollbackRespectsRecycledGeneration},
        std::pair{"next batch and recursive playback", TestNextBatchAndRecursivePlayback},
    };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << name << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << passed << " ECS world operation groups passed\n";
    return 0;
}
