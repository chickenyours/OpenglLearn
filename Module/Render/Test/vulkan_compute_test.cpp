#include "Render/Public/Compute/gpu_compute.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace Render::Compute;
static void Require(bool success, const char* message) {
    if (!success) throw std::runtime_error(message);
}
template<class F> static void Reject(F&& operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    Require(rejected, message);
}
int main(int argc, char** argv) {
    try {
        auto device = Device::CreateVulkan(argc > 1 && std::string(argv[1]) == "--validation");
        const auto initialStats = device->Diagnostics();
        Require(initialStats.available, "Native GPU compute unavailable");
        constexpr uint32_t count = 256;
        std::vector<uint32_t> input(count);
        for (uint32_t i = 0; i < count; ++i) input[i] = i;
        auto a = device->CreateStorageBuffer(input.size() * sizeof(uint32_t), input.data(), "input");
        auto b = device->CreateStorageBuffer(input.size() * sizeof(uint32_t), nullptr, "intermediate");
        const std::string prefix = R"(#version 450
layout(local_size_x=64) in;
layout(std430,set=0,binding=0) buffer A {uint a[];};
layout(std430,set=0,binding=1) buffer B {uint b[];};
layout(push_constant) uniform Params {uint n;} p;
)";
        auto add = device->CreatePipeline(prefix + "void main(){uint i=gl_GlobalInvocationID.x;b[i]=a[i]+p.n;}", 2, 4, "add");
        auto multiply = device->CreatePipeline(prefix + "void main(){uint i=gl_GlobalInvocationID.x;a[i]=b[i]*p.n;}", 2, 4, "multiply");
        Dispatch first{add, {{0, a, Access::ReadOnly}, {1, b, Access::WriteOnly}}, count / 64};
        Dispatch second{multiply, {{0, a, Access::WriteOnly}, {1, b, Access::ReadOnly}}, count / 64};
        first.pushConstants.resize(4); second.pushConstants.resize(4);
        const uint32_t plus = 7, factor = 3;
        std::memcpy(first.pushConstants.data(), &plus, 4);
        std::memcpy(second.pushConstants.data(), &factor, 4);
        device->Submit({{first, second}});
        auto bytes = device->Readback(a);
        Require(bytes.size() == count * 4, "readback size");
        std::vector<uint32_t> output(count);
        std::memcpy(output.data(), bytes.data(), bytes.size());
        for (uint32_t i = 0; i < count; ++i)
            Require(output[i] == (input[i] + plus) * factor, "compute write/barrier/read dependency");
        auto middle = device->Readback(b, 4, 28);
        uint32_t value = 0; std::memcpy(&value, middle.data(), 4);
        Require(value == 14, "partial readback");
        const uint32_t uploaded = 99;
        device->Upload(b, &uploaded, 4, 12);
        auto partial = device->Readback(b, 4, 12);
        std::memcpy(&value, partial.data(), 4);
        Require(value == uploaded, "partial upload");
        auto unaligned = device->Readback(b, 3, 13);
        Require(std::memcmp(unaligned.data(), reinterpret_cast<const std::byte*>(&uploaded) + 1, 3) == 0,
                "unaligned partial readback");
        auto broken = first; broken.bindings.pop_back();
        Reject([&] { device->Submit({{broken}}); }, "missing descriptors accepted");
        broken = first; broken.groupsX = 0;
        Reject([&] { device->Submit({{broken}}); }, "zero dispatch accepted");
        broken = first; broken.bindings[0].buffer.device += 100;
        Reject([&] { device->Submit({{broken}}); }, "foreign handle accepted");
        Reject([&] { device->Upload(a, &value, 4, bytes.size()); }, "out-of-range upload accepted");
        Reject([&] { device->Readback(a, bytes.size() + 1); }, "out-of-range readback accepted");
        auto stats = device->Diagnostics();
        Require(stats.submittedBatches == 1 && stats.dispatchedKernels == 2, "rejected batches changed dispatch statistics");
        device->Destroy(add); device->Destroy(multiply);
        device->Destroy(a); device->Destroy(b);
        Reject([&] { device->Readback(a); }, "stale handle accepted");
        stats = device->Diagnostics();
        Require(stats.liveBuffers == initialStats.liveBuffers && stats.livePipelines == initialStats.livePipelines &&
                stats.allocatedBytes == initialStats.allocatedBytes, "GPU resource leak");
        Require(stats.validationErrors == 0, "Vulkan validation error");
        std::cout << "Vulkan compute: " << stats.deviceName << " | two dependent kernels, readback, range checks and resource release passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
