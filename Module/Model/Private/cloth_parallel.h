#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace Model::detail {

// One calling thread owns an executor. For is synchronous and must not be nested
// on the same executor; each index in a batch must be independent of the others.
class ClothParallelExecutor final {
public:
    ClothParallelExecutor() : threadCount_(ConfiguredThreadCount()), spinChecks_(ConfiguredSpinChecks()) {
        std::size_t started = 0;
        try {
            for (; started + 1 < threadCount_; ++started) {
                threads_[started] = std::thread([this, started] { WorkerLoop(started); });
            }
        } catch (...) {
            // Resource pressure should reduce parallelism, not disable cloth.
            threadCount_ = started + 1;
        }
    }

    ~ClothParallelExecutor() {
        stopping_.store(true, std::memory_order_release);
        session_.store(false, std::memory_order_release);
        for (std::size_t i = 0; i + 1 < threadCount_; ++i) {
            Publish(workers_[i]);
        }
        for (std::size_t i = 0; i + 1 < threadCount_; ++i) {
            threads_[i].join();
        }
    }

    ClothParallelExecutor(const ClothParallelExecutor&) = delete;
    ClothParallelExecutor& operator=(const ClothParallelExecutor&) = delete;
    ClothParallelExecutor(ClothParallelExecutor&&) = delete;
    ClothParallelExecutor& operator=(ClothParallelExecutor&&) = delete;

    [[nodiscard]] std::size_t ThreadCount() const noexcept { return threadCount_; }
    [[nodiscard]] unsigned SpinCheckBudget() const noexcept { return spinChecks_; }

    // Session boundaries only control bounded spinning between synchronous
    // batches. A worker always blocks after its spin budget or outside a session.
    void BeginSession() noexcept { session_.store(true, std::memory_order_release); }
    void EndSession() noexcept { session_.store(false, std::memory_order_release); }

    template <class Function>
    void For(std::size_t count, std::size_t minItemsPerWorker, Function&& function) {
        if (count == 0) return;
        const auto minimum = std::max<std::size_t>(minItemsPerWorker, 1);
        const auto chunks = std::min(threadCount_, std::max<std::size_t>(1, count / minimum));
        if (chunks == 1) {
            for (std::size_t i = 0; i < count; ++i) std::invoke(function, i);
            return;
        }

        using Callable = std::remove_reference_t<Function>;
        struct Batch {
            Callable* callable;
            std::array<std::exception_ptr, MaxThreads> exceptions{};
            static void Run(void* context, std::size_t begin, std::size_t end,
                            std::size_t chunk) noexcept {
                auto& batch = *static_cast<Batch*>(context);
                try {
                    for (auto i = begin; i < end; ++i) std::invoke(*batch.callable, i);
                } catch (...) {
                    // Each chunk has its own slot; completion publishes writes.
                    batch.exceptions[chunk] = std::current_exception();
                }
            }
        } batch{std::addressof(function)};

        const auto base = count / chunks;
        const auto remainder = count % chunks;
        const auto callerEnd = base + (remainder != 0 ? 1 : 0);
        auto begin = callerEnd;
        remaining_.store(static_cast<unsigned>(chunks - 1), std::memory_order_relaxed);
        for (std::size_t chunk = 1; chunk < chunks; ++chunk) {
            auto& worker = workers_[chunk - 1];
            worker.context = &batch;
            worker.run = &Batch::Run;
            worker.begin = begin;
            worker.end = begin + base + (chunk < remainder ? 1 : 0);
            worker.chunk = chunk;
            begin = worker.end;
            Publish(worker);
        }
        Batch::Run(&batch, 0, callerEnd, 0);
        WaitForCompletion();
        for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
            if (batch.exceptions[chunk]) std::rethrow_exception(batch.exceptions[chunk]);
        }
    }

    // Publish an entire constraint family once. Colors retain their input order;
    // every item in one color completes before any item in the next color starts.
    // Items within a color must be independent. The average nonempty color size
    // determines lane count, so small families automatically stay serial.
    template <class Function>
    void ForColored(const std::vector<std::vector<std::uint32_t>>& colors,
                    std::size_t minItemsPerWorker, Function&& function) {
        std::size_t total = 0, nonempty = 0;
        for (const auto& color : colors) {
            total += color.size();
            nonempty += !color.empty();
        }
        auto colorAt = [&](std::size_t color) -> std::span<const std::uint32_t> {
            return colors[color];
        };
        ForColoredImpl(colors.size(), nonempty, total, minItemsPerWorker, colorAt, function);
    }

    // Packed offsets/order overload avoids rebuilding the solver's cached graph
    // coloring as vectors of vectors. Offsets include both initial zero and end.
    template <class Function>
    void ForColored(std::span<const std::uint32_t> offsets,
                    std::span<const std::uint32_t> order,
                    std::size_t minItemsPerWorker, Function&& function) {
        ForPackedColored<false>(offsets, order, minItemsPerWorker, function);
    }

    // The same ordered family job, but invoke once per NONEMPTY lane/color range
    // rather than once per index. A callback may process the contiguous index
    // span with SIMD; it must finish using the span before returning.
    template <class Function>
    void ForColoredRanges(std::span<const std::uint32_t> offsets,
                          std::span<const std::uint32_t> order,
                          std::size_t minItemsPerWorker, Function&& function) {
        ForPackedColored<true>(offsets, order, minItemsPerWorker, function);
    }

private:
    static constexpr std::size_t MaxThreads = 8;
    static constexpr std::uint64_t Sleeping = 1;
    // Keep short, dependent color jobs in user mode through scheduling tails.
    // The budget is finite; EndSession makes idle workers block immediately.
    // Keep waits short before parking: long colored-barrier spins consume
    // otherwise usable CPU time without advancing the cloth. The budget can
    // still be overridden for measurements on other schedulers.
    static constexpr unsigned DefaultSpinChecks = 2048;
    using RunFunction = void (*)(void*, std::size_t, std::size_t, std::size_t) noexcept;

    struct alignas(64) Worker {
        // Low bit means parked; remaining bits are the task generation. The CAS
        // used to park and fetch_add used to publish cannot lose a wakeup.
        std::atomic<std::uint64_t> signal{0};
        void* context = nullptr;
        RunFunction run = nullptr;
        std::size_t begin = 0;
        std::size_t end = 0;
        std::size_t chunk = 0;
    };

    class ColorBarrier {
    public:
        ColorBarrier(std::size_t lanes, unsigned spinChecks) noexcept
            : lanes_(static_cast<unsigned>(lanes)), spinChecks_(spinChecks) {}

        void Arrive(std::size_t lane) noexcept {
            const auto observed = phase_.load(std::memory_order_acquire) & ~WaitMask;
            if (arrived_.fetch_add(1, std::memory_order_acq_rel) + 1 == lanes_) {
                arrived_.store(0, std::memory_order_relaxed);
                const auto previous = phase_.fetch_add(PhaseIncrement, std::memory_order_release);
                if ((previous & WaitMask) != 0) phase_.notify_all();
                return;
            }
            auto phase = phase_.load(std::memory_order_acquire);
            for (unsigned checks = 0; (phase & ~WaitMask) == observed && checks < spinChecks_; ++checks) {
                Pause();
                phase = phase_.load(std::memory_order_acquire);
            }
            if ((phase & ~WaitMask) != observed) return;

            // Publishing the parked bit and advancing the phase are RMWs on the
            // same atomic. The final arriving lane cannot miss a parked waiter.
            const auto parked = std::uint64_t(1) << lane;
            phase = phase_.fetch_or(parked, std::memory_order_acq_rel) | parked;
            while ((phase & ~WaitMask) == observed) {
                phase_.wait(phase, std::memory_order_acquire);
                phase = phase_.load(std::memory_order_acquire);
            }
            phase_.fetch_and(~parked, std::memory_order_acq_rel);
        }

    private:
        static constexpr std::uint64_t PhaseIncrement = std::uint64_t(1) << MaxThreads;
        static constexpr std::uint64_t WaitMask = PhaseIncrement - 1;
        alignas(64) std::atomic<unsigned> arrived_{0};
        alignas(64) std::atomic<std::uint64_t> phase_{0};
        unsigned lanes_;
        const unsigned spinChecks_;
    };

    template <bool Ranges, class Function>
    void ForPackedColored(std::span<const std::uint32_t> offsets,
                          std::span<const std::uint32_t> order,
                          std::size_t minItemsPerWorker, Function& function) {
        if (offsets.empty()) {
            if (!order.empty()) throw std::invalid_argument("Cloth colors require offsets");
            return;
        }
        if (offsets.front() != 0 || offsets.back() != order.size())
            throw std::invalid_argument("Cloth color offsets must cover the complete order array");
        std::size_t nonempty = 0;
        for (std::size_t color = 0; color + 1 < offsets.size(); ++color) {
            if (offsets[color] > offsets[color + 1])
                throw std::invalid_argument("Cloth color offsets must be monotonic");
            nonempty += offsets[color] != offsets[color + 1];
        }
        auto colorAt = [&](std::size_t color) -> std::span<const std::uint32_t> {
            return order.subspan(offsets[color], offsets[color + 1] - offsets[color]);
        };
        ForColoredImpl<Ranges>(offsets.size() - 1, nonempty, order.size(), minItemsPerWorker, colorAt, function);
    }

    template <bool Ranges = false, class ColorAt, class Function>
    void ForColoredImpl(std::size_t colorCount, std::size_t nonempty,
                        std::size_t total, std::size_t minItemsPerWorker,
                        ColorAt& colorAt, Function& function) {
        if (nonempty == 0) return;
        const auto average = total / nonempty;
        const auto minimum = std::max<std::size_t>(minItemsPerWorker, 1);
        const auto lanes = std::min(threadCount_, std::max<std::size_t>(1, average / minimum));
        if (lanes == 1) {
            for (std::size_t color = 0; color < colorCount; ++color) {
                const auto indices = colorAt(color);
                if constexpr (Ranges) {
                    if (!indices.empty()) std::invoke(function, indices);
                } else {
                    for (auto index : indices) std::invoke(function, index);
                }
            }
            return;
        }

        struct Batch {
            ColorAt* colorAt;
            Function* callable;
            std::size_t colorCount, lanes;
            ColorBarrier barrier;
            std::atomic<std::size_t> failedColor{std::numeric_limits<std::size_t>::max()};
            std::array<std::exception_ptr, MaxThreads> exceptions{};

            Batch(ColorAt& at, Function& fn, std::size_t colors, std::size_t threads, unsigned spinChecks)
                : colorAt(&at), callable(&fn), colorCount(colors), lanes(threads), barrier(threads, spinChecks) {}

            static void Run(void* context, std::size_t, std::size_t, std::size_t lane) noexcept {
                auto& batch = *static_cast<Batch*>(context);
                for (std::size_t color = 0; color < batch.colorCount; ++color) {
                    const auto indices = (*batch.colorAt)(color);
                    if (indices.empty()) continue;
                    const auto base = indices.size() / batch.lanes;
                    const auto extra = indices.size() % batch.lanes;
                    const auto begin = lane * base + std::min(lane, extra);
                    const auto end = begin + base + (lane < extra ? 1 : 0);
                    try {
                        if constexpr (Ranges) {
                            if (begin != end) std::invoke(*batch.callable, indices.subspan(begin, end - begin));
                        } else {
                            for (auto i = begin; i < end; ++i) std::invoke(*batch.callable, indices[i]);
                        }
                    } catch (...) {
                        batch.exceptions[lane] = std::current_exception();
                        auto failed = batch.failedColor.load(std::memory_order_relaxed);
                        while (color < failed && !batch.failedColor.compare_exchange_weak(
                                   failed, color, std::memory_order_release, std::memory_order_relaxed)) {}
                    }
                    // A throwing lane still arrives, so peers cannot be stranded
                    // at a barrier. All lanes stop together before the next color.
                    if (color + 1 < batch.colorCount) batch.barrier.Arrive(lane);
                    // A fast lane can already fail in the next color while a
                    // slow lane leaves this barrier. Only failures at or before
                    // THIS color cancel it; a shared bool could strand peers.
                    if (batch.failedColor.load(std::memory_order_acquire) <= color) break;
                }
            }
        } batch(colorAt, function, colorCount, lanes, spinChecks_);

        remaining_.store(static_cast<unsigned>(lanes - 1), std::memory_order_relaxed);
        for (std::size_t lane = 1; lane < lanes; ++lane) {
            auto& worker = workers_[lane - 1];
            worker.context = &batch;
            worker.run = &Batch::Run;
            worker.begin = worker.end = 0;
            worker.chunk = lane;
            Publish(worker);
        }
        Batch::Run(&batch, 0, 0, 0);
        WaitForCompletion();
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            if (batch.exceptions[lane]) std::rethrow_exception(batch.exceptions[lane]);
        }
    }

    static void Pause() noexcept {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
        _mm_pause();
#else
        std::this_thread::yield();
#endif
    }

    static std::size_t ConfiguredThreadCount() noexcept {
        const auto hardware = std::max(1u, std::thread::hardware_concurrency());
        // Two total threads keep the small colored batches below the point
        // where scheduling and wakeup tails outweigh their arithmetic work.
        auto requested = static_cast<unsigned long>(std::min(hardware, 2u));
        auto parse = [&](const char* value) {
            if (!value || !*value) return;
            char* end = nullptr;
            const auto parsed = std::strtoul(value, &end, 10);
            if (end != value && *end == '\0' && parsed > 0) requested = parsed;
        };
#ifdef _WIN32
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, "MODEL_CLOTH_THREADS") == 0) parse(value);
        std::free(value);
#else
        parse(std::getenv("MODEL_CLOTH_THREADS"));
#endif
        return std::min<std::size_t>({MaxThreads, hardware, requested});
    }

    static unsigned ConfiguredSpinChecks() noexcept {
        // Read the process configuration only once. No getenv, allocation or
        // static initialization check enters a per-color barrier/spin loop.
        static const unsigned configured = [] {
            unsigned result = DefaultSpinChecks;
            auto parse = [&](const char* value) {
                if (!value || !*value) return;
                unsigned parsed = 0;
                for (const char* digit = value; *digit; ++digit) {
                    if (*digit < '0' || *digit > '9') return;
                    // Saturate while parsing, including arbitrarily long valid
                    // decimal strings, before clamping the supported range.
                    parsed = std::min(262144u, parsed * 10u + unsigned(*digit - '0'));
                }
                result = std::clamp(parsed, 512u, 262144u);
            };
#ifdef _WIN32
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, "MODEL_CLOTH_SPIN_CHECKS") == 0) parse(value);
            std::free(value);
#else
            parse(std::getenv("MODEL_CLOTH_SPIN_CHECKS"));
#endif
            return result;
        }();
        return configured;
    }

    static void Publish(Worker& worker) noexcept {
        const auto previous = worker.signal.fetch_add(2, std::memory_order_release);
        if ((previous & Sleeping) != 0) worker.signal.notify_one();
    }

    void WorkerLoop(std::size_t index) noexcept {
        auto& worker = workers_[index];
        std::uint64_t observed = 0;
        for (;;) {
            auto signal = worker.signal.load(std::memory_order_acquire);
            unsigned checks = 0;
            while ((signal & ~Sleeping) == observed &&
                   session_.load(std::memory_order_relaxed) && checks++ < spinChecks_) {
                Pause();
                signal = worker.signal.load(std::memory_order_acquire);
            }
            if ((signal & ~Sleeping) == observed) {
                auto expected = observed;
                if (worker.signal.compare_exchange_strong(expected, observed | Sleeping,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_acquire)) {
                    worker.signal.wait(observed | Sleeping, std::memory_order_acquire);
                    signal = worker.signal.fetch_and(~Sleeping, std::memory_order_acq_rel) & ~Sleeping;
                } else {
                    signal = expected;
                }
            }
            if (stopping_.load(std::memory_order_acquire)) return;
            signal &= ~Sleeping;
            if (signal == observed) continue;
            observed = signal;
            worker.run(worker.context, worker.begin, worker.end, worker.chunk);
            if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                remaining_.notify_one();
            }
        }
    }

    void WaitForCompletion() noexcept {
        auto remaining = remaining_.load(std::memory_order_acquire);
        for (unsigned checks = 0; remaining != 0 && checks < spinChecks_; ++checks) {
            Pause();
            remaining = remaining_.load(std::memory_order_acquire);
        }
        while (remaining != 0) {
            remaining_.wait(remaining, std::memory_order_acquire);
            remaining = remaining_.load(std::memory_order_acquire);
        }
    }

    std::array<Worker, MaxThreads - 1> workers_{};
    std::array<std::thread, MaxThreads - 1> threads_{};
    alignas(64) std::atomic<unsigned> remaining_{0};
    std::atomic<bool> session_{false};
    std::atomic<bool> stopping_{false};
    std::size_t threadCount_;
    const unsigned spinChecks_;
};

} // namespace Model::detail
