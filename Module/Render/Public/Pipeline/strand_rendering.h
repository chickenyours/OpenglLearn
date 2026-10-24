#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/Pipeline/strand_roughness.h"
#include "Render/Public/rhi_resource_handle.h"

namespace Render {
class RHIDevice;

// Backend-independent simulation output. positionRadius is local position and
// physical radius; tangentArc is local tangent and normalized root-to-tip arc.
struct StrandVertex {
    glm::vec4 positionRadius{0};
    glm::vec4 tangentArc{0, -1, 0, 0};
};
static_assert(sizeof(StrandVertex) == 32);

// Optional immutable source appearance coordinates. Keep these separate from
// the simulation's 32-byte position/tangent output and preserve source bytes
// rather than synthesizing random values from topology or strand IDs.
struct StrandColorCoordinate {
    glm::vec2 gradientRandom{0}; // normalized source arc, normalized source random
};
static_assert(sizeof(StrandColorCoordinate)==8);

// Scene-linear CPU asset data. The Render pass creates and owns the RHI image;
// callers publish immutable tables and never supply native graphics handles.
struct StrandTextureTable {
    std::uint32_t width=0,height=0;
    std::vector<glm::vec4> texels;
};

enum class StrandDebugMode : std::uint32_t {
    Beauty=0,BaseColor,Direct,Indirect,DirectBallistic,DirectMultiple,
    IndirectBallistic,IndirectMultiple,ReflectionR,TransmissionTT,InternalTRT,
    SunPathCount,IblPathCounts,OpaqueVisibility,IndirectIncident
};

struct StrandMaterial {
    // Selected native all-hardware producer: endpoint clip/visibility and
    // floor(clamp(projected L1 pixel length/9,1,15)) children, followed by
    // the recovered four-point curve/ribbon consumer. CPU list scheduling
    // is a host adapter; curve evaluation and rasterization remain on GPU.
    bool sourceCurveGeometry = false;
    // Native START view/tangent with END light/path queries and color
    // coordinates, followed by packed endpoint colors and hardware interpolation.
    // The software raster's dominant-axis interpolation is not reproduced.
    bool sourceSegmentShading = false;
    // Linear RGB, deliberately separate from any recovered source encoding.
    glm::vec3 rootColor{.30f, .20f, .105f};
    glm::vec3 tipColor{.72f, .55f, .30f};
    bool sourceColor = false;
    std::shared_ptr<const StrandTextureTable> gradientTable;
    glm::vec3 gradientWeights{1,0,0};
    // Gradient choice, hue, saturation and value offsets, respectively.
    glm::vec4 colorRandomOffsets{0};
    glm::vec3 hsvVariation{0};
    std::shared_ptr<const StrandTextureTable> nttTable;
    bool useNttLookup = true;
    // Original 64 theta x64 roughness x16 absorption table, flattened as
    // x=theta, y=roughness+64*absorption. Half-expanded scene-linear values.
    std::shared_ptr<const StrandTextureTable> nativeMomentTable;
    glm::vec4 nativeMomentParameters{1.55f,.04652056470513344f,.8f,.02f}; // IOR,F0,azimuth,tilt
    bool useNativeMomentLut = true;
    float roughness = .30f;
    float secondaryRoughness = .55f;
    float specularStrength = .60f;
    float secondarySpecular = .30f;
    float transmission = .25f;
    float widthScale = 1.0f;
    // Full ribbon width in local metres. Zero maximum leaves it unbounded.
    // Source software-raster limits are authored in mm and converted by the
    // importer. The analytic raster adapter applies them before projection.
    float minWidthMetres = 0.f;
    float maxWidthMetres = 0.f;
    float opacity = .95f;
    float cuticleShift = .08f;
    // Opt-in cylinder R/TT/TRT scattering. The old tangent-lobe model remains
    // the default for callers that have not supplied physical hair parameters.
    bool physicalScattering = false;
    // Source Emissive direct-light closure; the generic physical closure is
    // retained for existing callers and explicit comparisons.
    bool sourceDirectScattering = false;
    float ior = 1.55f;
    float longitudinalRoughness = .5f;
    // Preserve the authored input above; restore the source CPU r->hm_v
    // polynomial only for callers that opt into this recovered material path.
    bool sourceRoughnessMapping = false;
    float azimuthalRoughness = .8f;
    float absorptionScale = 1.f;
    // Bounded local inter-fiber approximation; density is an effective fiber
    // encounter count, not the original game's unobserved DSVolume texture.
    float multipleScattering = .65f;
    float scatteringDensity = 1.f;
    // Recovered source precomputed-indirect-light colorization controls.
    float albedoBlendRate = .51f;
    float indirectLightingMultiplier = .8f;
    // Spatial hair-hair extinction, reconstructed from physical segments.
    bool spatialVolume = false;
    // Paired native Texture DSVolume producer/consumer. Dimensionless packed
    // occupancy, six resource-cube mips and actual-point N/G queries. The RHI
    // storage adapter uses RGBA32F and manual clamp trilinear filtering.
    bool sourceDSVolume = false;
    float volumeRasterTraceStep = .5f; // source DSVolumeRasterTraceStepScale
    float volumeTraceLimit = 32.f; // recovered DSInfo CPU writer step-multiplier cap
    float volumeInterpolationSamples = 4.f; // source cursor threshold, not read count
    // Native CPU selection consumes an ordered segment prefix and compensates
    // its raster width by total/selected. Rate applies below manager quality 2
    // or when MinLodFactor is enabled; it is not a density multiplier.
    float volumeInjectionRate = 1.f;
    float volumeInjectionLodFraction = 1.f;
    bool volumeInjectionMinLodFactor = false;
    std::uint32_t volumeInjectionQuality = 2;
    // Native DSVolume uses relative packed width times .005 cm (50 um),
    // independently of visible ribbon width. Geometry sample mean estimates
    // the source resource average; opt-in preserves physical callers.
    bool normalizedVolumeWidth = false;
    // Full-width average in metres from the source STRD header. Zero derives
    // it from the uploaded geometry. Native DS always consumes it; the legacy
    // volume path consumes it only with normalizedVolumeWidth enabled.
    float volumeAverageWidth = 0.f;
    // Include spatial optical paths in environment scattering. Generic
    // callers retain their existing unoccluded environment by default.
    bool volumeSkyTransmission = false;
    // Source dual-direction IBL closure; false retains the former six-axis
    // forward-only attenuation for controlled comparisons.
    bool dualScatteringIbl = true;
    // Host estimate of native mCenterPos: query precomputed indirect light
    // along position minus the transformed groom bounds center, once for both
    // scattering directions. The original runtime center is not captured.
    bool sourceSpatialIbl = false;
    // Recovered source TT / propagated-backscatter gain. Local backscatter
    // remains part of the source closure even when this gain is zero.
    float backscatterScale = 1.f;
    float volumeVoxelSize = .005f;
    std::uint32_t volumeResolution = 32;
    float volumeRasterWidthScale = 1.5f;
    float volumeTraceStep = 1.1f;
    float volumeBiasVoxels = 1.f;
    // Host resolution adapter: retain the requested physical bias when the
    // volume budget enlarges actual voxels. False preserves generic behavior.
    bool volumePreserveNearField = false;
    // Host accuracy option: integrate the reconstructed trilinear density field
    // by cell instead of using growing point samples. Does not change density.
    bool volumeExactIntegration = false;
    // Optional closed collision proxies supplement the camera-visible shell.
    // They are explicit approximations to opaque render geometry.
    bool volumeProxyBlockers = false;
    // Reuse the first sampled world-space opaque shell while the physical
    // view and opaque content remain unchanged; TAA jitter alone is not motion.
    bool volumeStableOpaqueCache = false;
    // Query IBL N/G from this actual shading point/direction, rather than
    // interpolating voxel-center paths built from a mean tangent.
    bool volumePointIbl = false;
    bool receiveOpaqueShadows = true;
    // Shape only the sun's filtered external CSM visibility. One preserves
    // existing callers; the native ShadowPow .5 is an explicit source study.
    // This exponent never changes DS encounter counts, DS visibility or IBL.
    float opaqueShadowPower = 1.f;
    bool volumeOpaqueBlockers = false;
    bool volumeHardwareFiltering = true;
    float volumeDepthThreshold = .05f; // visible-surface shell, world metres
    // Stable filtered-coverage CSM adapter; independent from DSVolume self paths.
    bool castDirectionalShadows = false;
    // Preserve the projected physical area when filtering thin ribbons. The
    // former inset smooth edge remains an explicit visual comparison mode.
    bool areaPreservingCoverage = true;
    // Minimum screen footprint preserves thin fibers with proportional alpha.
    float minPixelWidth = 1.0f;
    float maxPixelWidth = 12.0f;
};

inline float EffectiveStrandLongitudinalRoughness(const StrandMaterial& material) {
    return material.sourceRoughnessMapping
        ?StrandSourceLongitudinalVariance(material.longitudinalRoughness)
        :material.longitudinalRoughness;
}

struct StrandSegmentSourceData;
class StrandGeometry final {
public:
    RenderResourceHandle<VertexBufferSpec> Mesh() const { return mesh_; }
    std::uint32_t IndexCount() const { return indexCount_; }
    std::uint32_t PointCount() const { return pointCount_; }
    glm::vec3 BoundsMin() const { return boundsMin_; }
    glm::vec3 BoundsMax() const { return boundsMax_; }
    float MeanRadius() const { return meanRadius_; }
    std::uint64_t Revision() const { return revision_; }
    bool PreviousInputMatched() const { return previousInputMatched_; }
    // Local position/radius or tangent XYZ differs from valid supplied history.
    // An upload/revision change alone is not deformation; arc is appearance.
    bool HasDeformationMotion() const { return hasDeformationMotion_; }
    // Opaque immutable source retained for the optional RHI segment producer.
    std::shared_ptr<const StrandSegmentSourceData> SegmentSource() const { return segmentSource_; }
private:
    friend class StrandMesh;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    std::uint32_t indexCount_ = 0, pointCount_ = 0;
    glm::vec3 boundsMin_{0}, boundsMax_{0};
    float meanRadius_ = 0;
    std::uint64_t revision_ = 0;
    bool previousInputMatched_ = false;
    bool hasDeformationMotion_ = false;
    std::shared_ptr<const void> lifetime_;
    std::shared_ptr<const StrandSegmentSourceData> segmentSource_;
};

// Owns RHI resources and expands a line-list topology into triangle ribbons.
// Retained snapshots are immutable; an in-flight mesh slot is never updated.
// Device must outlive this object, all snapshots and submitted frame leases.
class StrandMesh final {
public:
    explicit StrandMesh(RHIDevice& device);
    ~StrandMesh();
    StrandMesh(const StrandMesh&) = delete;
    StrandMesh& operator=(const StrandMesh&) = delete;
    bool Update(std::span<const StrandVertex> vertices,
                std::span<const std::uint32_t> lineIndices,
                std::span<const StrandColorCoordinate> colorCoordinates = {},
                std::span<const StrandVertex> previousVertices = {});
    std::shared_ptr<const StrandGeometry> Snapshot() const;
    const std::string& LastError() const;
    void Clear();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct StrandOpaqueCapsule {
    glm::vec4 startRadius{0},endRadius{0}; // local metres, nonnegative radius
};
struct StrandRenderItem {
    std::shared_ptr<const StrandGeometry> geometry;
    glm::mat4 model{1};
    StrandMaterial material;
    bool guides = false;
    // Caller-selected strand count / full strand count. Recovered source CPU
    // width compensation is 1/f for f>1e-5, otherwise 100000. This does not
    // select geometry or change DSVolume's independent injection widths.
    float strandCountFraction = 1.f;
    // The producer supplies LAST EXECUTED positions/model, never merely the
    // last simulation tick or a cancelled frame. Invalidate after topology,
    // physical/raster width, LOD or coverage policy changes.
    glm::mat4 previousModel{1};
    bool previousTransformValid = false;
    std::shared_ptr<const std::vector<StrandOpaqueCapsule>> opaqueCapsules;
};
} // namespace Render
