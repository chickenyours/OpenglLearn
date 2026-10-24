#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <glm/glm.hpp>

namespace HairSimulation {

struct HairAsset;
enum class GravityMode : std::uint32_t { Normal, Low, Zero, Custom };

// The three gravity vectors are from Diana's recovered controller. Compliance
// and force conversions below are reconstruction controls, not an assertion
// that raw GPUS preset scalars have these physical units.
struct Parameters {
    float fixedTimeStep = 1.0f / 60.0f;
    std::uint32_t substeps = 10, iterations = 6;
    glm::vec3 gravity{0, -30, 0};
    glm::vec3 wind{0.25f, 0, 0.1f};
    float windPulse = 0.3f;
    float airDrag = 0.3f;
    float linearDamping = 0.001f, angularDamping = 0.001f;
    float stretchCompliance = 1.0e-8f, bendCompliance = 1.0e-5f;
    float shapeMemory = 0.25f;
    float friction = 0.4f, collisionMargin = 0.0002f;
    float collisionRadiusScale = 1;
    bool collision = true;
    // Derive shading tangents from the actual interpolated render curves.
    // False retains the original guide-direction interpolation for comparison.
    bool reconstructRenderTangents = true;
    float maxLinearSpeed = 3.0f, maxAngularSpeed = 30.0f;
    std::uint32_t maxFrameSteps = 4;
};

struct ParameterHelp {
    static constexpr const char* FixedTimeStep = "固定物理步长（秒）；渲染帧率变化时保持相同物理时间尺度。";
    static constexpr const char* Substeps = "每个固定步的子步数；增加可改善细长杆稳定性，同时增加 GPU 成本。";
    static constexpr const char* Iterations = "每个子步的伸长、弯曲约束迭代数；增加可减少约束残差。";
    static constexpr const char* Gravity = "世界空间加速度（米/秒²）；Diana 原始三档为 Y=-30、-1.6、0。";
    static constexpr const char* Wind = "目标空气速度（米/秒）；通过空气阻力向发丝传递运动。";
    static constexpr const char* WindPulse = "风速周期波动幅度；0 表示恒定风。此风场为预览重建。";
    static constexpr const char* AirDrag = "发丝速度趋向空气速度的每秒响应；较大时风更明显、运动更快衰减。";
    static constexpr const char* LinearDamping = "每秒线速度衰减率；用于抑制持续振动。";
    static constexpr const char* AngularDamping = "每秒角速度衰减率；用于抑制杆方向抖动。";
    static constexpr const char* StretchCompliance = "Cosserat 伸长/剪切 XPBD 顺应性；越小越难拉伸。是重建参数。";
    static constexpr const char* BendCompliance = "相邻杆四元数弯曲/扭转 XPBD 顺应性；越小越能保持原始曲率。是重建参数。";
    static constexpr const char* ShapeMemory = "参考发型恢复强度；按提取的根到梢曲线衰减，根部强、远端弱。";
    static constexpr const char* Friction = "碰撞后切向速度损失比例；0 可自由滑动，1 完全消除切向速度。";
    static constexpr const char* CollisionMargin = "碰撞代理表面外扩距离（米）；避免发丝穿入代理。";
    static constexpr const char* CollisionRadiusScale = "提取的两个头部变半径胶囊的半径倍率；默认 1 保留资源半径。";
    static constexpr const char* Collision = "启用原始头部胶囊碰撞；由 Head 静态绑定矩阵变换，没有还原身体动态骨架。";
    static constexpr const char* MaxLinearSpeed = "线速度安全上限（米/秒）；限制调参突变产生的非有限状态。";
    static constexpr const char* MaxAngularSpeed = "角速度安全上限（弧度/秒）；限制四元数方向快速旋转。";
    static constexpr const char* MaxFrameSteps = "单个显示帧最多补算的固定步数；超额时间丢弃并计入统计，避免卡顿追赶。";
    static constexpr const char* ReconstructRenderTangents = "从实际渲染发丝的相邻位置重建原曲线导数方向；关闭可对照旧 guide 方向混合，不改变位置、宽度和物理时间。";
};

// std430-compatible output consumed by the existing Model preview bridge.
struct alignas(16) RenderVertex {
    glm::vec4 positionRadius{0};
    glm::vec4 tangentArc{0};
};
struct Statistics {
    std::uint64_t fixedSteps = 0, resets = 0, dispatches = 0;
    std::uint64_t renderRevision = 0;
    std::uint64_t readbackBytes = 0;
    std::uint32_t lastFrameSteps = 0;
    std::size_t guideCount = 0, guideNodes = 0, strands = 0, renderVertices = 0;
    double simulationSeconds = 0, submitMilliseconds = 0, readbackMilliseconds = 0;
    double droppedSeconds = 0;
    float maxGuideDisplacement = 0, maxSegmentRelativeError = 0, maxRootError = 0;
    float maxOrientationError = 0;
    std::uint32_t nonFiniteNodes = 0, gpuRecoveredNodes = 0;
    bool gpuReady = false;
    std::string deviceName;
};

// Application-thread owner. GPU simulation and interpolation run on a dedicated
// Vulkan compute queue. The GL Model preview consumes an explicit synchronous
// readback; this is a compatibility bridge, with its cost reported in Stats().
class Simulator final {
public:
    Simulator();
    ~Simulator();
    Simulator(Simulator&&) noexcept;
    Simulator& operator=(Simulator&&) noexcept;
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    bool LoadAsset(const std::filesystem::path& path, std::string* error = nullptr);
    bool Initialize(std::string* error = nullptr);
    bool Reset(std::string* error = nullptr);
    // Refresh interpolation/shading data at the existing guide state. This
    // neither advances physics nor resets velocity, time or the accumulator.
    bool RefreshRenderTangents(std::string* error = nullptr);
    bool Step(double deltaSeconds, std::string* error = nullptr);
    void SetGravityMode(GravityMode mode);
    GravityMode GetGravityMode() const;
    Parameters& Settings();
    const Parameters& Settings() const;
    void SanitizeParameters();
    // Rest shape is rigidly translated on GPU; changing this also allows a
    // caller to observe inertial response to movement of the scalp attachment.
    void SetRootTranslation(const glm::vec3& translation);
    const Statistics& Stats() const;
    const HairAsset* Asset() const;
    std::span<const RenderVertex> GetRenderVertices() const;
    std::span<const glm::vec4> GetGuidePositions() const;
    const std::string& LastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace HairSimulation
