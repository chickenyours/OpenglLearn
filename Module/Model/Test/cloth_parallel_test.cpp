#include "Model/Private/cloth_parallel.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Model::detail::ClothParallelExecutor;
int failures = 0;

bool Expect(bool condition, const char* description) {
    std::printf("  %-86s %s\n", description, condition ? "ok" : "FAIL");
    failures += !condition;
    return condition;
}

std::uint64_t Value(std::size_t batch, std::size_t index) {
    auto value = (std::uint64_t(batch) + 1) * 0x9e3779b97f4a7c15ull + index;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

struct Payload {
    std::uint64_t value = 0;
    std::uint64_t complement = 0;
    std::size_t batch = 0;
};

void ConsecutiveBatches(ClothParallelExecutor& executor) {
    constexpr std::size_t capacity = 2049;
    constexpr std::array<std::size_t, 22> sizes{
        0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 63, 64, 65, 97, 127, 128, 129, 257, 511, 1024, 2049};
    constexpr std::array<std::size_t, 6> minimums{0, 1, 8, 16, 64, 4096};
    std::array<std::atomic<unsigned>, capacity> visits{};
    std::array<Payload, capacity> output{};
    bool exactlyOnce = true, visible = true, untouched = true;
    executor.BeginSession();
    for (std::size_t batch = 0; batch < 3000; ++batch) {
        const auto count = sizes[(batch * 13 + batch / sizes.size()) % sizes.size()];
        const auto minimum = minimums[(batch / sizes.size()) % minimums.size()];
        for (std::size_t i = 0; i < capacity; ++i) visits[i].store(0, std::memory_order_relaxed);
        // These ordinary writes deliberately rely on the executor's publication
        // and completion barriers, rather than adding a mutex in the callback.
        executor.For(count, minimum, [&](std::size_t index) {
            const auto previous = visits[index].fetch_add(1, std::memory_order_relaxed);
            if (previous == 0) {
                const auto value = Value(batch, index);
                output[index] = {value, ~value, batch};
            }
        });
        for (std::size_t i = 0; i < count; ++i) {
            exactlyOnce &= visits[i].load(std::memory_order_relaxed) == 1;
            const auto value = Value(batch, i);
            visible &= output[i].value == value && output[i].complement == ~value && output[i].batch == batch;
        }
        for (std::size_t i = count; i < capacity; ++i) {
            untouched &= visits[i].load(std::memory_order_relaxed) == 0;
        }
    }
    executor.EndSession();
    Expect(exactlyOnce, "3000 varying batches execute every requested index exactly once");
    Expect(visible, "Every synchronous return publishes complete ordinary-memory payloads");
    Expect(untouched, "Zero, tiny, uneven and full batches never execute out-of-range indices");

    // A later batch reads values produced by a different worker in the previous
    // batch. This exercises both directions of the stack-closure handoff.
    constexpr std::size_t count = 521;
    std::array<std::uint64_t, count> first{}, second{}, expected{}, temporary{};
    for (std::size_t i = 0; i < count; ++i) first[i] = expected[i] = Value(0, i);
    executor.BeginSession();
    for (std::size_t batch = 0; batch < 300; ++batch) {
        executor.For(count, 1, [&](std::size_t i) { second[i] = first[(i + 73) % count] ^ Value(batch, i); });
        for (std::size_t i = 0; i < count; ++i) temporary[i] = expected[(i + 73) % count] ^ Value(batch, i);
        first.swap(second);
        expected.swap(temporary);
    }
    executor.EndSession();
    Expect(first == expected, "Dependent batches observe prior results across changing worker ownership");
}

void ContiguousDivision(ClothParallelExecutor& executor) {
    constexpr std::size_t count = 113;
    std::array<std::thread::id, count> owners{};
    const auto caller = std::this_thread::get_id();
    executor.For(count, 1, [&](std::size_t i) { owners[i] = std::this_thread::get_id(); });
    const auto chunks = executor.ThreadCount();
    const auto base = count / chunks, remainder = count % chunks;
    std::array<std::thread::id, 8> chunkOwners{};
    std::size_t begin = 0;
    bool contiguous = true, distinct = true;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        const auto end = begin + base + (chunk < remainder ? 1 : 0);
        chunkOwners[chunk] = owners[begin];
        for (std::size_t i = begin; i < end; ++i) contiguous &= owners[i] == chunkOwners[chunk];
        for (std::size_t previous = 0; previous < chunk; ++previous) {
            distinct &= chunkOwners[previous] != chunkOwners[chunk];
        }
        begin = end;
    }
    Expect(contiguous && distinct && begin == count && chunkOwners[0] == caller,
           "Caller and workers own deterministic, distinct contiguous chunks");
}

class BatchFailure : public std::runtime_error {
public:
    explicit BatchFailure(std::size_t index) : std::runtime_error("expected batch failure"), index(index) {}
    std::size_t index;
};

void ExceptionsAndRecovery(ClothParallelExecutor& executor) {
    const auto count = executor.ThreadCount() * 16;
    std::vector<unsigned> output(count, 0);
    bool exceptions = true, completedBeforeRethrow = true, recovered = true;
    executor.BeginSession();
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        // Alternate a caller exception and an exception at a worker chunk's
        // first index. In serial mode both paths correctly become index zero.
        const auto failedIndex = (attempt % 2 != 0 && executor.ThreadCount() > 1) ? std::size_t(16) : std::size_t(0);
        std::fill(output.begin(), output.end(), 0);
        bool caught = false;
        try {
            executor.For(count, 1, [&](std::size_t i) {
                if (i == failedIndex) throw BatchFailure(i);
                if (i % 16 == 0) std::this_thread::yield();
                output[i] = attempt + 1;
            });
        } catch (const BatchFailure& failure) {
            caught = failure.index == failedIndex;
        } catch (...) {
            caught = false;
        }
        exceptions &= caught;
        for (std::size_t i = 0; i < count; ++i) {
            const bool skipped = i / 16 == failedIndex / 16;
            completedBeforeRethrow &= output[i] == (skipped ? 0u : attempt + 1);
        }
        executor.For(count, 1, [&](std::size_t i) { output[i] = 0x12340000u + static_cast<unsigned>(i); });
        for (std::size_t i = 0; i < count; ++i) recovered &= output[i] == 0x12340000u + i;
    }
    executor.EndSession();
    Expect(exceptions, "Caller and worker exceptions preserve their type and payload on the caller");
    Expect(completedBeforeRethrow, "All nonthrowing chunks finish before an exception leaves For");
    Expect(recovered, "The same executor accepts complete new batches after repeated exceptions");
}

void ColoredFamilies(ClothParallelExecutor& executor) {
    struct Pair { std::uint32_t a, b; double offset; };
    constexpr std::size_t half = 128;
    std::vector<Pair> pairs;
    std::vector<std::vector<std::uint32_t>> colors(11);
    for (std::size_t color = 0; color < colors.size(); ++color) {
        if (color == 0 || color == 4 || color == 10) continue;
        for (std::uint32_t i = 0; i < half; ++i) {
            colors[color].push_back(static_cast<std::uint32_t>(pairs.size()));
            pairs.push_back({i, static_cast<std::uint32_t>(half + (i + color * 13) % half),
                             static_cast<double>(color + i % 7) * .000125});
        }
    }
    std::vector<std::uint32_t> offsets{0}, order;
    for (const auto& color : colors) {
        order.insert(order.end(), color.begin(), color.end());
        offsets.push_back(static_cast<std::uint32_t>(order.size()));
    }
    std::array<double, half * 2> expected{}, actual{};
    for (std::size_t i = 0; i < actual.size(); ++i) expected[i] = actual[i] = static_cast<double>(i) * .007;
    std::vector<std::atomic<unsigned>> visits(pairs.size());
    auto solve = [&](auto& state, std::uint32_t index) {
        const auto& pair = pairs[index];
        const double delta = (state[pair.b] - state[pair.a]) * .125 + pair.offset;
        state[pair.a] += delta;
        state[pair.b] -= delta;
    };
    bool once = true, identical = true;
    executor.BeginSession();
    for (unsigned repeat = 0; repeat < 80; ++repeat) {
        for (auto& count : visits) count.store(0, std::memory_order_relaxed);
        auto callback = [&](std::uint32_t index) {
            visits[index].fetch_add(1, std::memory_order_relaxed);
            solve(actual, index);
        };
        if (repeat % 3 == 0) executor.ForColored(colors, 1, callback);
        else if (repeat % 3 == 1) executor.ForColored(offsets, order, 1, callback);
        else executor.ForColoredRanges(offsets, order, 1, [&](std::span<const std::uint32_t> indices) {
            for (auto index : indices) callback(index);
        });
        for (const auto& color : colors) for (auto index : color) solve(expected, index);
        for (const auto& count : visits) once &= count.load(std::memory_order_relaxed) == 1;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            identical &= std::bit_cast<std::uint64_t>(actual[i]) == std::bit_cast<std::uint64_t>(expected[i]);
        }
    }
    executor.EndSession();
    Expect(once, "Index and range colored families execute each constraint exactly once");
    Expect(identical, "Cross-color dependent floating-point solves match serial execution bit for bit");

    bool callerOnly = true;
    const auto caller = std::this_thread::get_id();
    executor.ForColored(colors, pairs.size(), [&](std::uint32_t) {
        callerOnly &= std::this_thread::get_id() == caller;
    });
    Expect(callerOnly, "Insufficient work per color selects the caller-only path");
    unsigned emptyCalls = 0;
    const std::vector<std::vector<std::uint32_t>> emptyColors(5);
    const std::vector<std::uint32_t> emptyOrder, emptyOffsets, zeroOffsets{0, 0, 0};
    executor.ForColored(emptyColors, 1, [&](std::uint32_t) { ++emptyCalls; });
    executor.ForColored(emptyOffsets, emptyOrder, 1, [&](std::uint32_t) { ++emptyCalls; });
    executor.ForColored(zeroOffsets, emptyOrder, 1, [&](std::uint32_t) { ++emptyCalls; });
    Expect(emptyCalls == 0, "Empty families and empty colors complete without dispatch or callbacks");

    bool layoutsRejected = true;
    const std::vector<std::uint32_t> sample{0, 1, 2, 3};
    for (const auto& invalid : std::vector<std::vector<std::uint32_t>>{{}, {1, 4}, {0, 3}, {0, 2, 1, 4}}) {
        bool rejected = false;
        try { executor.ForColored(invalid, sample, 1, [](std::uint32_t) {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        layoutsRejected &= rejected;
    }
    Expect(layoutsRejected, "Malformed packed offsets fail before a worker can access an invalid span");
}

void ColoredRanges(ClothParallelExecutor& executor) {
    const auto lanes = executor.ThreadCount();
    const std::array<std::size_t, 8> sizes{0, 1, 2, 0, 3, 127, lanes * 33 + 1, 0};
    std::vector<std::uint32_t> offsets{0}, order;
    for (auto size : sizes) {
        const auto begin = order.size();
        // Constraint IDs differ from their order-array positions. Range callbacks
        // must retain both the contiguous span and the cached index permutation.
        for (std::size_t i = 0; i < size; ++i) order.push_back(static_cast<std::uint32_t>(begin + size - 1 - i));
        offsets.push_back(static_cast<std::uint32_t>(order.size()));
    }
    std::array<std::atomic<unsigned>, 8> calls{};
    std::vector<std::size_t> rangeBegin(order.size()), rangeEnd(order.size());
    std::vector<unsigned> visits(order.size(), 0);
    std::vector<std::thread::id> owners(order.size());
    std::atomic<bool> valid{true};
    executor.BeginSession();
    executor.ForColoredRanges(offsets, order, 1, [&](std::span<const std::uint32_t> indices) {
        if (indices.empty()) { valid.store(false, std::memory_order_relaxed); return; }
        const auto begin = static_cast<std::size_t>(indices.data() - order.data());
        const auto end = begin + indices.size();
        const auto color = static_cast<std::size_t>(std::upper_bound(offsets.begin(), offsets.end(), begin) - offsets.begin() - 1);
        if (color >= sizes.size() || end > offsets[color + 1]) {
            valid.store(false, std::memory_order_relaxed); return;
        }
        calls[color].fetch_add(1, std::memory_order_relaxed);
        for (std::size_t i = begin; i < end; ++i) {
            rangeBegin[i] = begin; rangeEnd[i] = end; owners[i] = std::this_thread::get_id();
        }
        for (auto index : indices) ++visits[index];
    });
    executor.EndSession();
    bool division = valid.load(std::memory_order_relaxed), once = true;
    const auto caller = std::this_thread::get_id();
    for (std::size_t color = 0; color < sizes.size(); ++color) {
        division &= calls[color].load(std::memory_order_relaxed) == std::min(lanes, sizes[color]);
        const auto base = sizes[color] / lanes, extra = sizes[color] % lanes;
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            const auto begin = offsets[color] + lane * base + std::min(lane, extra);
            const auto end = begin + base + (lane < extra ? 1 : 0);
            for (auto i = begin; i < end; ++i) {
                division &= rangeBegin[i] == begin && rangeEnd[i] == end;
                if (lane == 0) division &= owners[i] == caller;
            }
        }
    }
    for (auto count : visits) once &= count == 1;
    Expect(division && once, "Range callbacks receive exactly one nonempty contiguous chunk per lane and color");

    unsigned serialCalls = 0;
    bool callerOnly = true;
    executor.ForColoredRanges(offsets, order, order.size(), [&](std::span<const std::uint32_t> indices) {
        ++serialCalls; callerOnly &= !indices.empty() && std::this_thread::get_id() == caller;
    });
    Expect(callerOnly && serialCalls == 5, "Serial range fallback invokes once for each nonempty color");
    unsigned emptyCalls = 0;
    const std::vector<std::uint32_t> emptyOrder, zeroOffsets{0, 0, 0};
    executor.ForColoredRanges(zeroOffsets, emptyOrder, 1, [&](std::span<const std::uint32_t>) { ++emptyCalls; });
    bool rejected = false;
    try {
        const std::vector<std::uint32_t> invalid{0, 2, 1, static_cast<std::uint32_t>(order.size())};
        executor.ForColoredRanges(invalid, order, 1, [](std::span<const std::uint32_t>) {});
    } catch (const std::invalid_argument&) { rejected = true; }
    Expect(emptyCalls == 0 && rejected, "Range dispatch skips empty spans and rejects malformed packed offsets");
}

void ColoredFailureAndBarrier(ClothParallelExecutor& executor) {
    const auto perColor = executor.ThreadCount() * 8;
    std::vector<std::vector<std::uint32_t>> colors(4);
    for (std::size_t color = 0; color < colors.size(); ++color)
        for (std::size_t i = 0; i < perColor; ++i)
            colors[color].push_back(static_cast<std::uint32_t>(color * perColor + i));
    std::vector<std::uint32_t> offsets{0}, order;
    for (const auto& color : colors) {
        order.insert(order.end(), color.begin(), color.end());
        offsets.push_back(static_cast<std::uint32_t>(order.size()));
    }
    std::vector<unsigned> output(perColor * colors.size());
    bool propagated = true, cancelled = true, recovered = true;
    executor.BeginSession();
    for (std::size_t attempt = 0; attempt < 32; ++attempt) {
        const auto failedColor = attempt % colors.size();
        const auto failedLane = (attempt % 2 && executor.ThreadCount() > 1) ? std::size_t(1) : std::size_t(0);
        const auto failedIndex = failedColor * perColor + failedLane * 8;
        std::fill(output.begin(), output.end(), 0);
        bool caught = false;
        try {
            auto callback = [&](std::uint32_t index) {
                if (index == failedIndex) throw BatchFailure(index);
                output[index] = 1;
            };
            if (attempt % 3 == 0) executor.ForColoredRanges(offsets, order, 1, [&](std::span<const std::uint32_t> indices) {
                for (auto index : indices) callback(index);
            });
            else executor.ForColored(colors, 1, callback);
        } catch (const BatchFailure& failure) {
            caught = failure.index == failedIndex;
        } catch (...) {}
        propagated &= caught;
        for (std::size_t i = 0; i < output.size(); ++i) {
            const auto color = i / perColor;
            const bool completed = color < failedColor ||
                (color == failedColor && (i % perColor) / 8 != failedLane);
            cancelled &= output[i] == (completed ? 1u : 0u);
        }
        executor.ForColoredRanges(offsets, order, 1, [&](std::span<const std::uint32_t> indices) {
            for (auto index : indices) output[index] = 2;
        });
        for (auto value : output) recovered &= value == 2;
    }
    executor.EndSession();
    Expect(propagated, "Colored caller/worker failures propagate safely from early and final colors");
    Expect(cancelled, "A failing color finishes peer lanes and cancels every later color");
    Expect(recovered, "Colored barriers recover after repeated cancellation without stranded lanes");

    std::atomic<bool> delayedDone{false}, ordered{true};
    executor.BeginSession();
    executor.ForColored(colors, 1, [&](std::uint32_t index) {
        if (index == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            delayedDone.store(true, std::memory_order_relaxed);
        }
        if (index >= perColor && !delayedDone.load(std::memory_order_relaxed))
            ordered.store(false, std::memory_order_relaxed);
    });
    executor.EndSession();
    Expect(ordered.load(std::memory_order_relaxed),
           "A delayed color lane wakes parked peers before any dependent color executes");
}

void SessionsAndShutdown(ClothParallelExecutor& executor) {
    std::array<std::uint64_t, 257> output{};
    bool resumed = true;
    for (std::size_t session = 0; session < 32; ++session) {
        executor.BeginSession();
        executor.For(output.size(), 1, [&](std::size_t i) { output[i] = Value(session, i); });
        executor.EndSession();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // For also works without BeginSession, when each idle worker parks.
        executor.For(output.size(), 1, [&](std::size_t i) { output[i] ^= Value(session + 1, i); });
        for (std::size_t i = 0; i < output.size(); ++i) {
            resumed &= output[i] == (Value(session, i) ^ Value(session + 1, i));
        }
    }
    Expect(resumed, "Repeated sessions and batches outside sessions wake blocked workers reliably");

    bool destroyed = true;
    for (unsigned lifetime = 0; lifetime < 12; ++lifetime) {
        ClothParallelExecutor temporary;
        destroyed &= temporary.SpinCheckBudget() == executor.SpinCheckBudget();
        if (lifetime % 3 == 0) {
            // Destroy workers that have never received a task.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        temporary.BeginSession();
        temporary.For(output.size(), 1, [&](std::size_t i) { output[i] = Value(lifetime, i); });
        for (std::size_t i = 0; i < output.size(); ++i) destroyed &= output[i] == Value(lifetime, i);
        if (lifetime % 3 == 1) {
            temporary.EndSession();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // The other case deliberately destroys during the active spin window.
    }
    Expect(destroyed, "Destruction joins never-used, parked and active-session workers without deadlock");
}
} // namespace

int main(int argc, char** argv) {
    const auto started = std::chrono::steady_clock::now();
    ClothParallelExecutor executor;
    std::printf("Cloth persistent executor (%zu threads including caller, %u spin checks):\n",
                executor.ThreadCount(), executor.SpinCheckBudget());
    // Allows separate-process configuration coverage without mutating the
    // environment observed by live workers or the cached process setting.
    // Example: MODEL_CLOTH_SPIN_CHECKS=65536 ... --expect-spin-checks 65536.
    if (argc != 1) {
        unsigned expected = 0;
        if (argc != 3 || std::strcmp(argv[1], "--expect-spin-checks") != 0) {
            std::fprintf(stderr, "Usage: cloth_parallel_test [--expect-spin-checks value]\n");
            return 1;
        }
        const auto end = argv[2] + std::strlen(argv[2]);
        const auto parsed = std::from_chars(argv[2], end, expected);
        if (parsed.ec != std::errc{} || parsed.ptr != end) {
            std::fprintf(stderr, "Expected spin budget must be an unsigned decimal integer\n");
            return 1;
        }
        Expect(executor.SpinCheckBudget() == expected, "The process spin setting matches the explicitly expected parsed value");
    }
    Expect(executor.SpinCheckBudget() >= 512 && executor.SpinCheckBudget() <= 262144,
           "The cached finite spin budget remains inside its supported range");
    Expect(executor.ThreadCount() >= 1 && executor.ThreadCount() <= 8 &&
               executor.ThreadCount() <= std::max(1u, std::thread::hardware_concurrency()),
           "Thread count respects hardware availability and the eight-thread cap");
    ConsecutiveBatches(executor);
    ContiguousDivision(executor);
    ExceptionsAndRecovery(executor);
    ColoredFamilies(executor);
    ColoredRanges(executor);
    ColoredFailureAndBarrier(executor);
    SessionsAndShutdown(executor);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("Cloth parallel executor: %d failure(s), %.3f seconds\n", failures, elapsed);
    return failures == 0 ? 0 : 1;
}
