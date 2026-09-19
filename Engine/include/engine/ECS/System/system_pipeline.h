#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <queue>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ECS/Context/context.h"
#include "engine/ECS/System/system.h"

namespace ECS::System {

// Deterministic lifecycle and dependency scheduler.  Execution is deliberately
// sequential for now; access declarations make later conflict-aware batching
// possible without changing individual systems.
class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    template <typename SystemT, typename... Args>
    SystemT& Add(Args&&... args) {
        static_assert(std::is_base_of_v<System, SystemT>);
        const std::type_index type(typeid(SystemT));
        if (typeToIndex_.contains(type)) {
            return *static_cast<SystemT*>(systems_[typeToIndex_.at(type)].get());
        }

        auto system = std::make_unique<SystemT>(std::forward<Args>(args)...);
        SystemT& result = *system;
        typeToIndex_[type] = systems_.size();
        systems_.push_back(std::move(system));
        dependencies_.emplace_back();
        built_ = false;
        return result;
    }

    template <typename BeforeT, typename AfterT>
    bool RunBefore() {
        const auto before = FindIndex<BeforeT>();
        const auto after = FindIndex<AfterT>();
        if (before == npos || after == npos || before == after) return false;
        dependencies_[after].push_back(before);
        built_ = false;
        return true;
    }

    template <typename SystemT>
    SystemT* Get() const {
        const auto index = FindIndex<SystemT>();
        return index == npos ? nullptr : static_cast<SystemT*>(systems_[index].get());
    }

    bool Build() {
        executionOrder_.clear();
        executionOrder_.reserve(systems_.size());

        for (std::size_t phaseIndex = 0; phaseIndex < phaseCount; ++phaseIndex) {
            if (!BuildPhase(static_cast<Phase>(phaseIndex))) {
                executionOrder_.clear();
                built_ = false;
                return false;
            }
        }
        built_ = true;
        return true;
    }

    bool Start(Context& context) {
        if (started_) return true;
        if (!built_ && !Build()) return false;

        SceneContextScope scope(context.scene);
        std::size_t startedCount = 0;
        for (const std::size_t index : executionOrder_) {
            if (!systems_[index]->StartInternal(context)) {
                for (std::size_t i = startedCount; i > 0; --i) {
                    systems_[executionOrder_[i - 1]]->EndInternal(context);
                }
                return false;
            }
            ++startedCount;
        }
        started_ = true;
        return true;
    }

    bool Tick(Context& context) {
        if (!started_ && !Start(context)) return false;
        SceneContextScope scope(context.scene);
        for (const std::size_t index : executionOrder_) {
            systems_[index]->TickInternal(context);
        }
        return true;
    }

    void Stop(Context& context) {
        if (!started_) return;
        SceneContextScope scope(context.scene);
        for (auto it = executionOrder_.rbegin(); it != executionOrder_.rend(); ++it) {
            systems_[*it]->EndInternal(context);
        }
        started_ = false;
    }

    const std::vector<std::size_t>& GetExecutionOrder() const noexcept {
        return executionOrder_;
    }

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    static constexpr std::size_t phaseCount = 5;

    class SceneContextScope {
    public:
        explicit SceneContextScope(Core::Scene* scene)
            : previous_(Core::localECSCoreContext.scene) {
            Core::localECSCoreContext.scene = scene;
        }
        ~SceneContextScope() { Core::localECSCoreContext.scene = previous_; }

    private:
        Core::Scene* previous_ = nullptr;
    };

    template <typename SystemT>
    std::size_t FindIndex() const {
        const auto found = typeToIndex_.find(std::type_index(typeid(SystemT)));
        return found == typeToIndex_.end() ? npos : found->second;
    }

    bool BuildPhase(Phase phase) {
        std::vector<std::size_t> phaseSystems;
        for (std::size_t index = 0; index < systems_.size(); ++index) {
            if (systems_[index]->GetPhase() == phase) phaseSystems.push_back(index);
        }

        std::unordered_map<std::size_t, std::size_t> localIndex;
        for (std::size_t i = 0; i < phaseSystems.size(); ++i) localIndex[phaseSystems[i]] = i;

        std::vector<std::size_t> indegree(phaseSystems.size(), 0);
        std::vector<std::vector<std::size_t>> outgoing(phaseSystems.size());
        for (std::size_t localAfter = 0; localAfter < phaseSystems.size(); ++localAfter) {
            const std::size_t after = phaseSystems[localAfter];
            for (const std::size_t before : dependencies_[after]) {
                if (systems_[before]->GetPhase() != phase) continue;
                const std::size_t localBefore = localIndex.at(before);
                if (std::find(outgoing[localBefore].begin(), outgoing[localBefore].end(), localAfter)
                    == outgoing[localBefore].end()) {
                    outgoing[localBefore].push_back(localAfter);
                    ++indegree[localAfter];
                }
            }
        }

        // Insertion index is the stable tie breaker.
        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t i = 0; i < indegree.size(); ++i) {
            if (indegree[i] == 0) ready.push(i);
        }

        std::size_t emitted = 0;
        while (!ready.empty()) {
            const std::size_t local = ready.top();
            ready.pop();
            executionOrder_.push_back(phaseSystems[local]);
            ++emitted;
            for (const std::size_t next : outgoing[local]) {
                if (--indegree[next] == 0) ready.push(next);
            }
        }
        return emitted == phaseSystems.size();
    }

    std::vector<std::unique_ptr<System>> systems_;
    std::unordered_map<std::type_index, std::size_t> typeToIndex_;
    std::vector<std::vector<std::size_t>> dependencies_;
    std::vector<std::size_t> executionOrder_;
    bool built_ = false;
    bool started_ = false;
};

} // namespace ECS::System
