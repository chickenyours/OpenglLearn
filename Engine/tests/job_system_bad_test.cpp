#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// 你的头文件
#include "engine/ECS/Context/context.h"
#include "engine/ECS/JobSystem/job_system.h"
#include "engine/ECS/JobSystem/job_system_schedule.h"

//
// ===== 最小桩：为了让测试程序独立编译 =====
// 如果你工程里已经有真实的 Scene / context 定义，删掉这里，改成包含真实头文件。
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

// 防止编译器把任务优化没了
static volatile std::uint64_t g_sink = 0;

// 极轻任务：测试框架固定开销
void TinyTask()
{
    g_sink += 1;
}

// 略重任务：观察任务本体变重后，框架开销占比如何下降
void LightTask()
{
    volatile std::uint64_t local = 0;
    for (int i = 0; i < 64; ++i)
    {
        local += static_cast<std::uint64_t>(i);
    }
    g_sink += local;
}

// 再重一点
void MediumTask()
{
    volatile std::uint64_t local = 0;
    for (int i = 0; i < 512; ++i)
    {
        local += static_cast<std::uint64_t>(i * 3 + 7);
    }
    g_sink += local;
}

using TaskFunc = void(*)();

//
// ===== 计时工具 =====
//

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
    double perTaskNs = 0.0;
};

//
// ===== 两组对照 =====
//

// 直接顺序调用
static Result RunDirect(TaskFunc task, std::size_t count)
{
    Result r;
    r.name = "Direct function pointer call";

    r.totalNs = MeasureNs([&]()
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            task();
        }
    });

    r.perTaskNs = (count == 0) ? 0.0
                               : static_cast<double>(r.totalNs) / static_cast<double>(count);
    return r;
}

// 串行用 job_system：每次 1 个任务，提交 -> 分发 -> 等待 -> 下一个
static Result RunJobSystemSerial(TaskFunc task, std::size_t count)
{
    Result r;
    r.name = "JobSystem serial submit+dispatch+wait";

    ECS::Core::JobSystemSchedule schedule(1); // 只开 1 个 worker，尽量贴近串行
    ECS::Core::Scene scene;
    ECS::Core::JobSystem job(&scene, &schedule);

    r.totalNs = MeasureNs([&]()
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const bool submitOk = job.Submit(task);
            if (!submitOk)
            {
                std::cerr << "job.Submit failed at i = " << i << '\n';
                std::exit(1);
            }

            const std::size_t dispatched = job.DispatchAll();
            if (dispatched != 1)
            {
                std::cerr << "job.DispatchAll unexpected value at i = " << i
                          << ", dispatched = " << dispatched << '\n';
                std::exit(1);
            }

            job.WaitIdle();
        }
    });

    r.perTaskNs = (count == 0) ? 0.0
                               : static_cast<double>(r.totalNs) / static_cast<double>(count);

    schedule.Stop();
    return r;
}

//
// ===== 多轮跑，降低偶然误差 =====
//

struct Summary
{
    std::string caseName;
    std::uint64_t bestNs = 0;
    std::uint64_t worstNs = 0;
    double avgNs = 0.0;
    double avgPerTaskNs = 0.0;
};

static Summary RunRepeatedDirect(TaskFunc task, std::size_t count, int rounds)
{
    Summary s;
    s.caseName = "Direct";

    std::uint64_t sum = 0;
    for (int r = 0; r < rounds; ++r)
    {
        Result one = RunDirect(task, count);
        if (r == 0 || one.totalNs < s.bestNs) s.bestNs = one.totalNs;
        if (r == 0 || one.totalNs > s.worstNs) s.worstNs = one.totalNs;
        sum += one.totalNs;
    }

    s.avgNs = static_cast<double>(sum) / static_cast<double>(rounds);
    s.avgPerTaskNs = s.avgNs / static_cast<double>(count);
    return s;
}

static Summary RunRepeatedJobSystem(TaskFunc task, std::size_t count, int rounds)
{
    Summary s;
    s.caseName = "JobSystem";

    std::uint64_t sum = 0;
    for (int r = 0; r < rounds; ++r)
    {
        Result one = RunJobSystemSerial(task, count);
        if (r == 0 || one.totalNs < s.bestNs) s.bestNs = one.totalNs;
        if (r == 0 || one.totalNs > s.worstNs) s.worstNs = one.totalNs;
        sum += one.totalNs;
    }

    s.avgNs = static_cast<double>(sum) / static_cast<double>(rounds);
    s.avgPerTaskNs = s.avgNs / static_cast<double>(count);
    return s;
}

static void PrintCompare(
    const std::string& title,
    const Summary& direct,
    const Summary& jobsys,
    std::size_t count,
    int rounds)
{
    const double extraAvgNs = jobsys.avgNs - direct.avgNs;
    const double extraPerTaskNs = jobsys.avgPerTaskNs - direct.avgPerTaskNs;
    const double ratio = (direct.avgNs > 0.0) ? (jobsys.avgNs / direct.avgNs) : 0.0;

    std::cout << "\n============================================================\n";
    std::cout << title << '\n';
    std::cout << "Task count = " << count << ", rounds = " << rounds << '\n';
    std::cout << "------------------------------------------------------------\n";

    std::cout << std::left << std::setw(18) << "Direct"
              << " best(ns)=" << std::setw(14) << direct.bestNs
              << " worst(ns)=" << std::setw(14) << direct.worstNs
              << " avg(ns)=" << std::setw(14) << static_cast<std::uint64_t>(direct.avgNs)
              << " avg/task(ns)=" << direct.avgPerTaskNs << '\n';

    std::cout << std::left << std::setw(18) << "JobSystem"
              << " best(ns)=" << std::setw(14) << jobsys.bestNs
              << " worst(ns)=" << std::setw(14) << jobsys.worstNs
              << " avg(ns)=" << std::setw(14) << static_cast<std::uint64_t>(jobsys.avgNs)
              << " avg/task(ns)=" << jobsys.avgPerTaskNs << '\n';

    std::cout << "------------------------------------------------------------\n";
    std::cout << "Extra framework avg overhead(ns)      = " << extraAvgNs << '\n';
    std::cout << "Extra framework avg overhead/task(ns) = " << extraPerTaskNs << '\n';
    std::cout << "JobSystem / Direct ratio              = " << std::fixed << std::setprecision(3)
              << ratio << "x\n";
    std::cout << "============================================================\n";
}

static void Warmup(TaskFunc task)
{
    // 先做一点预热，减少首次线程创建/唤醒的偶然影响
    ECS::Core::JobSystemSchedule schedule(1);
    ECS::Core::Scene scene;
    ECS::Core::JobSystem job(&scene, &schedule);

    for (int i = 0; i < 2000; ++i)
    {
        task();
        job.Submit(task);
        job.DispatchAll();
        job.WaitIdle();
    }

    schedule.Stop();
}

int main()
{
    constexpr std::size_t taskCount = 200000;
    constexpr int rounds = 7;

    std::cout << "Serial overhead benchmark started...\n";

    Warmup(TinyTask);

    {
        auto d = RunRepeatedDirect(TinyTask, taskCount, rounds);
        auto j = RunRepeatedJobSystem(TinyTask, taskCount, rounds);
        PrintCompare("Case: TinyTask", d, j, taskCount, rounds);
    }

    {
        auto d = RunRepeatedDirect(LightTask, taskCount, rounds);
        auto j = RunRepeatedJobSystem(LightTask, taskCount, rounds);
        PrintCompare("Case: LightTask", d, j, taskCount, rounds);
    }

    {
        auto d = RunRepeatedDirect(MediumTask, taskCount, rounds);
        auto j = RunRepeatedJobSystem(MediumTask, taskCount, rounds);
        PrintCompare("Case: MediumTask", d, j, taskCount, rounds);
    }

    std::cout << "\nFinal sink = " << g_sink << '\n';
    return 0;
}