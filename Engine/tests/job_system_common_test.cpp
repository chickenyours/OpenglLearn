#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

// 你的头文件
#include "engine/ECS/Context/context.h"
#include "engine/ECS/JobSystem/job_system.h"
#include "engine/ECS/JobSystem/job_system_schedule.h"

//
// ===== 最小桩：为了让测试程序独立编译 =====
// 如果你工程里已经有真实的 Scene 定义，就删掉这里。
//

// ---------- Scene 最小桩 ----------
namespace ECS::Core
{
    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;
    };
}

//
// ===== 测试任务 =====
//
// 注意：这次是并行测试，不能再像旧代码那样用 volatile 全局变量做累加，
// 否则多线程下会有数据竞争。
// 这里改成 atomic，只在任务末尾 fetch_add 一次，避免把 atomic 成本放大到循环内部。
//

// 防止任务被优化掉
static std::atomic<std::uint64_t> g_sink{0};

static inline void ConsumeResult(std::uint64_t value)
{
    g_sink.fetch_add(value, std::memory_order_relaxed);
}

// 轻任务：并行收益不明显，更多体现调度固定成本
void ParallelTinyTaskA()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 200; ++i)
    {
        local += static_cast<std::uint64_t>(i + 1);
    }
    ConsumeResult(local);
}

void ParallelTinyTaskB()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 200; ++i)
    {
        local += static_cast<std::uint64_t>(i * 2 + 3);
    }
    ConsumeResult(local);
}

void ParallelTinyTaskC()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 200; ++i)
    {
        local += static_cast<std::uint64_t>(i * 3 + 5);
    }
    ConsumeResult(local);
}

// 中任务：更容易看到并行收益
void ParallelMediumTaskA()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 50000; ++i)
    {
        local += static_cast<std::uint64_t>((i * 13 + 7) ^ (i >> 2));
    }
    ConsumeResult(local);
}

void ParallelMediumTaskB()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 50000; ++i)
    {
        local += static_cast<std::uint64_t>((i * 17 + 11) ^ (i >> 1));
    }
    ConsumeResult(local);
}

void ParallelMediumTaskC()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 50000; ++i)
    {
        local += static_cast<std::uint64_t>((i * 19 + 13) ^ (i >> 3));
    }
    ConsumeResult(local);
}

// 重任务：更适合体现 3 个 worker 的并行能力
void ParallelHeavyTaskA()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 200000; ++i)
    {
        local += static_cast<std::uint64_t>((i * 29 + 17) ^ (i >> 2));
    }
    ConsumeResult(local);
}

void ParallelHeavyTaskB()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 200000; ++i)
    {
        local += static_cast<std::uint64_t>((i * 31 + 19) ^ (i >> 1));
    }
    ConsumeResult(local);
}

void ParallelHeavyTaskC()
{
    std::uint64_t local = 0;
    for (int i = 0; i < 200000; ++i)
    {
        local += static_cast<std::uint64_t>((i * 37 + 23) ^ (i >> 3));
    }
    ConsumeResult(local);
}

using TaskFunc = void(*)();

struct TaskTriple
{
    TaskFunc a;
    TaskFunc b;
    TaskFunc c;
};

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

template<typename F>
static std::uint64_t MeasureNs(F&& func)
{
    const auto t0 = Clock::now();
    func();
    const auto t1 = Clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<Nanoseconds>(t1 - t0).count()
    );
}

struct Result
{
    std::string name;
    std::uint64_t totalNs = 0;
    double perGroupNs = 0.0;
};

struct Summary
{
    std::string caseName;
    std::uint64_t bestNs = 0;
    std::uint64_t worstNs = 0;
    double avgNs = 0.0;
    double avgPerGroupNs = 0.0;
};

//
// ===== 两组对照 =====
//

// 顺序执行：每组固定 A -> B -> C
static Result RunDirectTripleSerial(const TaskTriple& triple, std::size_t groupCount)
{
    Result r;
    r.name = "Direct serial A+B+C";

    r.totalNs = MeasureNs([&]()
    {
        for (std::size_t i = 0; i < groupCount; ++i)
        {
            triple.a();
            triple.b();
            triple.c();
        }
    });

    r.perGroupNs = (groupCount == 0) ? 0.0
                                     : static_cast<double>(r.totalNs) / static_cast<double>(groupCount);
    return r;
}

// JobSystem 并行执行：每组固定提交 A/B/C，然后统一 DispatchAll + WaitIdle
static Result RunJobSystemTripleParallel(const TaskTriple& triple, std::size_t groupCount)
{
    Result r;
    r.name = "JobSystem parallel A+B+C";

    ECS::Core::JobSystemSchedule schedule(3); // 假设始终有 3 个任务可并行
    ECS::Core::Scene scene;
    ECS::Core::JobSystem job(&scene, &schedule);

    r.totalNs = MeasureNs([&]()
    {
        for (std::size_t i = 0; i < groupCount; ++i)
        {
            if (!job.Submit(triple.a))
            {
                std::cerr << "Submit A failed at group " << i << '\n';
                std::exit(1);
            }

            if (!job.Submit(triple.b))
            {
                std::cerr << "Submit B failed at group " << i << '\n';
                std::exit(1);
            }

            if (!job.Submit(triple.c))
            {
                std::cerr << "Submit C failed at group " << i << '\n';
                std::exit(1);
            }

            const std::size_t dispatched = job.DispatchAll();
            if (dispatched != 3)
            {
                std::cerr << "DispatchAll unexpected value at group " << i
                          << ", dispatched = " << dispatched << '\n';
                std::exit(1);
            }

            job.WaitIdle();
        }
    });

    r.perGroupNs = (groupCount == 0) ? 0.0
                                     : static_cast<double>(r.totalNs) / static_cast<double>(groupCount);

    schedule.Stop();
    return r;
}

static Summary RunRepeatedDirectTriple(const TaskTriple& triple, std::size_t groupCount, int rounds)
{
    Summary s;
    s.caseName = "Direct serial triple";

    std::uint64_t sum = 0;
    for (int r = 0; r < rounds; ++r)
    {
        Result one = RunDirectTripleSerial(triple, groupCount);
        if (r == 0 || one.totalNs < s.bestNs) s.bestNs = one.totalNs;
        if (r == 0 || one.totalNs > s.worstNs) s.worstNs = one.totalNs;
        sum += one.totalNs;
    }

    s.avgNs = static_cast<double>(sum) / static_cast<double>(rounds);
    s.avgPerGroupNs = s.avgNs / static_cast<double>(groupCount);
    return s;
}

static Summary RunRepeatedJobSystemTriple(const TaskTriple& triple, std::size_t groupCount, int rounds)
{
    Summary s;
    s.caseName = "JobSystem parallel triple";

    std::uint64_t sum = 0;
    for (int r = 0; r < rounds; ++r)
    {
        Result one = RunJobSystemTripleParallel(triple, groupCount);
        if (r == 0 || one.totalNs < s.bestNs) s.bestNs = one.totalNs;
        if (r == 0 || one.totalNs > s.worstNs) s.worstNs = one.totalNs;
        sum += one.totalNs;
    }

    s.avgNs = static_cast<double>(sum) / static_cast<double>(rounds);
    s.avgPerGroupNs = s.avgNs / static_cast<double>(groupCount);
    return s;
}

static void PrintParallelCompare(
    const std::string& title,
    const Summary& direct,
    const Summary& jobsys,
    std::size_t groupCount,
    int rounds)
{
    const double savedNs = direct.avgNs - jobsys.avgNs;
    const double savedPerGroupNs = direct.avgPerGroupNs - jobsys.avgPerGroupNs;
    const double speedup = (jobsys.avgNs > 0.0) ? (direct.avgNs / jobsys.avgNs) : 0.0;

    std::cout << "\n============================================================\n";
    std::cout << title << '\n';
    std::cout << "Group count = " << groupCount << ", rounds = " << rounds << '\n';
    std::cout << "Each group contains exactly 3 independent tasks.\n";
    std::cout << "------------------------------------------------------------\n";

    std::cout << std::left << std::setw(22) << "Direct serial"
              << " best(ns)=" << std::setw(14) << direct.bestNs
              << " worst(ns)=" << std::setw(14) << direct.worstNs
              << " avg(ns)=" << std::setw(14) << static_cast<std::uint64_t>(direct.avgNs)
              << " avg/group(ns)=" << direct.avgPerGroupNs << '\n';

    std::cout << std::left << std::setw(22) << "JobSystem parallel"
              << " best(ns)=" << std::setw(14) << jobsys.bestNs
              << " worst(ns)=" << std::setw(14) << jobsys.worstNs
              << " avg(ns)=" << std::setw(14) << static_cast<std::uint64_t>(jobsys.avgNs)
              << " avg/group(ns)=" << jobsys.avgPerGroupNs << '\n';

    std::cout << "------------------------------------------------------------\n";
    std::cout << "Saved total time avg(ns)      = " << savedNs << '\n';
    std::cout << "Saved time per group avg(ns)  = " << savedPerGroupNs << '\n';
    std::cout << "Parallel speedup              = " << std::fixed << std::setprecision(3)
              << speedup << "x\n";
    std::cout << "============================================================\n";
}

static void WarmupTriple(const TaskTriple& triple)
{
    ECS::Core::JobSystemSchedule schedule(3);
    ECS::Core::Scene scene;
    ECS::Core::JobSystem job(&scene, &schedule);

    for (int i = 0; i < 1000; ++i)
    {
        triple.a();
        triple.b();
        triple.c();

        job.Submit(triple.a);
        job.Submit(triple.b);
        job.Submit(triple.c);
        job.DispatchAll();
        job.WaitIdle();
    }

    schedule.Stop();
}

int main()
{
    constexpr std::size_t groupCount = 20000;
    constexpr int rounds = 7;

    std::cout << "Parallel capability benchmark started...\n";
    std::cout << "Assumption: each dispatch group always has 3 independent tasks.\n";

    {
        const TaskTriple triple{ ParallelTinyTaskA, ParallelTinyTaskB, ParallelTinyTaskC };
        WarmupTriple(triple);

        auto d = RunRepeatedDirectTriple(triple, groupCount, rounds);
        auto j = RunRepeatedJobSystemTriple(triple, groupCount, rounds);
        PrintParallelCompare("Case: Tiny triple", d, j, groupCount, rounds);
    }

    {
        const TaskTriple triple{ ParallelMediumTaskA, ParallelMediumTaskB, ParallelMediumTaskC };
        WarmupTriple(triple);

        auto d = RunRepeatedDirectTriple(triple, groupCount, rounds);
        auto j = RunRepeatedJobSystemTriple(triple, groupCount, rounds);
        PrintParallelCompare("Case: Medium triple", d, j, groupCount, rounds);
    }

    {
        const TaskTriple triple{ ParallelHeavyTaskA, ParallelHeavyTaskB, ParallelHeavyTaskC };
        WarmupTriple(triple);

        auto d = RunRepeatedDirectTriple(triple, groupCount, rounds);
        auto j = RunRepeatedJobSystemTriple(triple, groupCount, rounds);
        PrintParallelCompare("Case: Heavy triple", d, j, groupCount, rounds);
    }

    std::cout << "\nFinal sink = " << g_sink.load(std::memory_order_relaxed) << '\n';
    return 0;
}