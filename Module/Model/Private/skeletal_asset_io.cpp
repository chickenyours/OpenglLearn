#include "Model/Public/skeletal_asset.h"
#include "Model/Public/vmd_animation.h"
#include "source_physics_json.h"
#include "animation_limits.h"
#include <json/json.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Model {
namespace {
namespace fs = std::filesystem;
constexpr std::uint32_t FormatVersion = 1;
constexpr std::uint64_t MaxBinaryBytes = 2ull * 1024 * 1024 * 1024;
constexpr std::uint64_t MaxJsonBytes = 64ull * 1024 * 1024;
constexpr std::uint64_t MaxAnimationJsonBytes = 256ull * 1024 * 1024;
constexpr std::uint64_t MaxAnimationKeys = AnimationLimits::MaximumStoredKeys;
constexpr std::uint64_t MaxElements = 100000000;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string Utf8(const fs::path& value) {
    const auto bytes = value.generic_u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

fs::path Path(const std::string& value) {
    return fs::u8path(value);
}

std::string StableId(const std::string& kind, const std::string& name) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : kind + ":" + name) { hash ^= c; hash *= 1099511628211ull; }
    std::ostringstream output;
    output << kind << '-' << std::hex << hash;
    return output.str();
}

void CheckVersion(const Json::Value& root, const char* type) {
    Require(root.isObject() && root["assetType"].asString() == type,
        std::string("Expected ") + type + " asset.");
    Require(root["version"].isUInt() && root["version"].asUInt() == FormatVersion,
        std::string("Unsupported ") + type + " version.");
}

Json::Value Header(const char* type, const std::string& id, const std::string& name) {
    Json::Value value(Json::objectValue);
    value["assetType"] = type; value["version"] = FormatVersion;
    value["id"] = id; value["name"] = name;
    return value;
}

Json::Value ReadJson(const fs::path& path, std::uint64_t maximumBytes = MaxJsonBytes) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    Require(!ec && size > 0 && size <= maximumBytes, "Missing, empty, or oversized JSON: " + Utf8(path));
    std::ifstream stream(path, std::ios::binary);
    Require(bool(stream), "Cannot open JSON: " + Utf8(path));
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"] = true; builder["failIfExtra"] = true;
    builder["allowSpecialFloats"] = false;
    Json::Value root; std::string errors;
    Require(Json::parseFromStream(builder, stream, &root, &errors), "Invalid JSON: " + errors);
    return root;
}

void WriteJson(const fs::path& path, const Json::Value& value, bool compact = false) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(bool(stream), "Cannot create JSON: " + Utf8(path));
    Json::StreamWriterBuilder builder; builder["indentation"] = compact ? "" : "  ";
    const auto writer = std::unique_ptr<Json::StreamWriter>(builder.newStreamWriter());
    writer->write(value, &stream); stream << '\n';
    stream.close();
    Require(bool(stream), "Failed writing JSON: " + Utf8(path));
}

void PublishPackageDirectory(const fs::path& candidate, const fs::path& target) {
#ifdef _WIN32
    // A newly verified package can briefly be held by an external scanner
    // without FILE_SHARE_DELETE. Retry only those transient Windows failures;
    // every attempt remains one no-replace directory rename. Never copy a
    // partially published package into the destination.
    constexpr std::array<DWORD, 6> delays{20, 40, 80, 160, 200, 250};
    const auto fail = [&](DWORD code, const char* message) {
        throw fs::filesystem_error(message, candidate, target,
            std::error_code(static_cast<int>(code), std::system_category()));
    };
    for (std::size_t attempt = 0;; ++attempt) {
        const auto sourceAttributes = GetFileAttributesW(candidate.c_str());
        if (sourceAttributes == INVALID_FILE_ATTRIBUTES)
            fail(GetLastError(), "Package candidate became inaccessible before publication");
        if ((sourceAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (sourceAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            fail(ERROR_DIRECTORY, "Package candidate is no longer a regular directory");
        const auto targetAttributes = GetFileAttributesW(target.c_str());
        if (targetAttributes != INVALID_FILE_ATTRIBUTES)
            fail(ERROR_ALREADY_EXISTS, "Package destination appeared before publication");
        const auto targetError = GetLastError();
        if (targetError != ERROR_FILE_NOT_FOUND && targetError != ERROR_PATH_NOT_FOUND)
            fail(targetError, "Cannot confirm package destination is absent");
        // MoveFileW omits MOVEFILE_REPLACE_EXISTING, so another writer winning
        // after the attribute check cannot have its package overwritten.
        if (MoveFileW(candidate.c_str(), target.c_str())) return;
        const auto code = GetLastError();
        const bool transient = code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
        if (!transient || attempt >= delays.size()) fail(code, "Cannot atomically publish skeletal asset directory");
        Sleep(delays[attempt]);
    }
#else
    fs::rename(candidate, target);
#endif
}

void CheckRegularPath(const fs::path& path) {
    // canonical()/weakly_canonical() open directory handles that are denied by
    // some Windows app sandboxes. Lexical containment plus checking every
    // existing component excludes symlinks/junctions without those handles.
    auto prefix = path.root_path();
    for (const auto& component : path.relative_path()) {
        prefix /= component;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(prefix.c_str());
        Require(attributes != INVALID_FILE_ATTRIBUTES, "Missing or inaccessible asset path: " + Utf8(prefix));
        Require((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "Asset references through Windows reparse points are not permitted.");
#else
        const auto status = fs::symlink_status(prefix);
        Require(fs::exists(status) && !fs::is_symlink(status), "Missing asset path or symlink in package reference.");
#endif
    }
}

std::string ReferenceKey(const fs::path& path) {
#ifdef _WIN32
    const auto text = path.native();
    const auto count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr, 0);
    Require(count > 0, "Cannot normalize animation asset path.");
    std::wstring normalized(count, L'\0');
    Require(LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()),
        normalized.data(), count, nullptr, nullptr, 0) == count, "Cannot normalize animation asset path.");
    return Utf8(fs::path(normalized));
#else
    return Utf8(path);
#endif
}

fs::path Child(const fs::path& directory, const Json::Value& value) {
    Require(value.isString() && !value.asString().empty(), "Asset reference must be a nonempty relative path.");
    const auto relative = Path(value.asString());
    Require(!relative.is_absolute() && !relative.has_root_name() && !relative.has_root_directory(),
        "Absolute asset references are not permitted.");
    for (const auto& component : relative) {
        Require(component != ".." && component != "." && component != "" && Utf8(component).find(':') == std::string::npos,
            "Asset reference escapes its package or names an alternate data stream.");
#ifdef _WIN32
        const auto name = component.native();
        Require(!name.empty() && name.back() != L'.' && name.back() != L' '
            && name.find_first_of(L"<>\"|?*") == std::wstring::npos,
            "Asset references must use unambiguous regular Windows filenames.");
#endif
    }
    const auto root = fs::absolute(directory).lexically_normal();
    const auto child = (root / relative).lexically_normal();
    const auto normalized = child.lexically_relative(root);
    Require(!normalized.empty() && *normalized.begin() != "..", "Asset reference escapes its package.");
    CheckRegularPath(child);
    return child;
}

template <typename T> void Finite(T value) {
    Require(std::isfinite(value), "Asset contains a non-finite numeric value.");
}
template <typename V> Json::Value Vector(const V& v) {
    Json::Value result(Json::arrayValue);
    for (glm::length_t i = 0; i < v.length(); ++i) { Finite(v[i]); result.append(v[i]); }
    return result;
}
template <typename V> V ReadVector(const Json::Value& value) {
    V result(0);
    Require(value.isArray() && value.size() == static_cast<Json::ArrayIndex>(result.length()), "Invalid vector component count.");
    for (glm::length_t i = 0; i < result.length(); ++i) {
        Require(value[static_cast<Json::ArrayIndex>(i)].isNumeric(), "Expected numeric vector component.");
        const auto number = value[static_cast<Json::ArrayIndex>(i)].asDouble(); Finite(number);
        result[i] = static_cast<typename V::value_type>(number); Finite(result[i]);
    }
    return result;
}
Json::Value Matrix(const glm::dmat4& matrix) {
    Json::Value result(Json::arrayValue);
    for (int column = 0; column < 4; ++column) result.append(Vector(matrix[column]));
    return result;
}
glm::dmat4 ReadMatrix(const Json::Value& value) {
    Require(value.isArray() && value.size() == 4, "Invalid matrix component count.");
    glm::dmat4 result(1);
    for (int column = 0; column < 4; ++column) result[column] = ReadVector<glm::dvec4>(value[column]);
    return result;
}

// Every scalar is encoded explicitly. These payloads do not depend on native
// struct padding, GLM alignment, compiler ABI, or the host's endian order.
constexpr std::size_t BinaryBufferBytes = 64 * 1024;
struct PayloadHash {
    std::uint64_t value = 14695981039346656037ull;
    void Append(std::span<const std::uint8_t> bytes) {
        for (const auto byte : bytes) { value ^= byte; value *= 1099511628211ull; }
    }
    std::string String() const { std::ostringstream stream; stream << std::hex << value; return stream.str(); }
};
struct BinaryWriter {
    std::ofstream stream;
    std::array<std::uint8_t, BinaryBufferBytes> buffer{};
    std::size_t buffered = 0, size = 0;
    PayloadHash hash;
    explicit BinaryWriter(const fs::path& path) {
        fs::create_directories(path.parent_path());
        stream.open(path, std::ios::binary | std::ios::trunc);
        Require(bool(stream), "Cannot create payload.");
    }
    std::size_t Size() const { return size; }
    void Flush() {
        if (!buffered) return;
        stream.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffered));
        Require(bool(stream), "Failed writing payload."); buffered = 0;
    }
    void Raw(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > MaxBinaryBytes - size) throw std::runtime_error("Asset payload exceeds format limit.");
        hash.Append(bytes); size += bytes.size();
        while (!bytes.empty()) {
            if (!buffered && bytes.size() >= buffer.size()) {
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                Require(bool(stream), "Failed writing payload."); return;
            }
            const auto count = std::min(bytes.size(), buffer.size() - buffered);
            std::memcpy(buffer.data() + buffered, bytes.data(), count);
            buffered += count; bytes = bytes.subspan(count);
            if (buffered == buffer.size()) Flush();
        }
    }
    template <typename T> void Scalar(T value) {
        static_assert(std::is_arithmetic_v<T>);
        if constexpr (std::is_floating_point_v<T>) Finite(value);
        auto raw = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
        if constexpr (std::endian::native != std::endian::little) std::reverse(raw.begin(), raw.end());
        Raw(raw);
    }
    template <typename V> void Vec(const V& value) {
        for (glm::length_t i = 0; i < value.length(); ++i) Scalar(value[i]);
    }
    Json::Value Range(std::size_t offset, std::size_t count, std::size_t stride) const {
        Json::Value range(Json::objectValue);
        range["offset"] = Json::UInt64(offset); range["count"] = Json::UInt64(count); range["stride"] = Json::UInt64(stride);
        return range;
    }
    void Save() {
        Flush();
        stream.close(); Require(bool(stream), "Failed writing payload.");
    }
};

struct BinaryReader {
    std::ifstream stream;
    std::array<std::uint8_t, BinaryBufferBytes> buffer{};
    std::size_t size = 0, bufferedOffset = 0, bufferedCount = 0;
    std::size_t position = 0, end = 0;
    std::string hash;
    // At the unchanged 2-GiB limit this index occupies at most 256 KiB.
    // It binds later decoded bytes to the initial checksum scan, preserving
    // the old memory-snapshot check when another process changes the file.
    std::vector<std::uint64_t> blockHashes;
    std::map<std::size_t, std::size_t> ranges;
    explicit BinaryReader(const fs::path& path) {
        std::error_code ec; const auto fileSize = fs::file_size(path, ec);
        Require(!ec && fileSize <= MaxBinaryBytes, "Missing or oversized asset payload.");
        size = end = static_cast<std::size_t>(fileSize);
        stream.open(path, std::ios::binary);
        Require(bool(stream), "Cannot open asset payload.");
        PayloadHash checksum;
        blockHashes.reserve((size + buffer.size() - 1) / buffer.size());
        for (std::size_t offset = 0; offset < size;) {
            const auto count = std::min(buffer.size(), size - offset);
            stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
            Require(bool(stream), "Truncated asset payload.");
            const std::span<const std::uint8_t> bytes(buffer.data(), count);
            checksum.Append(bytes);
            PayloadHash block; block.Append(bytes); blockHashes.push_back(block.value);
            offset += count;
        }
        hash = checksum.String();
    }
    void Raw(std::span<std::uint8_t> destination) {
        if (position > end || destination.size() > end - position) throw std::runtime_error("Truncated binary scalar.");
        while (!destination.empty()) {
            if (position < bufferedOffset || position - bufferedOffset >= bufferedCount) {
                bufferedOffset = position / buffer.size() * buffer.size();
                stream.clear(); stream.seekg(static_cast<std::streamoff>(bufferedOffset));
                Require(bool(stream), "Cannot seek asset payload.");
                bufferedCount = std::min(buffer.size(), size - bufferedOffset);
                stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(bufferedCount));
                Require(bool(stream), "Truncated asset payload.");
                PayloadHash block; block.Append(std::span(buffer.data(), bufferedCount));
                Require(block.value == blockHashes[bufferedOffset / buffer.size()],
                    "Asset payload changed after checksum verification.");
            }
            const auto count = std::min(destination.size(), bufferedCount - (position - bufferedOffset));
            std::memcpy(destination.data(), buffer.data() + position - bufferedOffset, count);
            position += count; destination = destination.subspan(count);
        }
    }
    bool Signature() {
        std::array<std::uint8_t, 8> signature{};
        if (size < signature.size()) return false;
        position = 0; end = size; Raw(signature);
        return std::equal(signature.begin(), signature.end(), "SKASSET1");
    }
    std::size_t Range(const Json::Value& range, std::size_t expectedStride,
        std::uint64_t maximumCount = MaxElements) {
        Require(range.isObject() && range["offset"].isUInt64() && range["count"].isUInt64() && range["stride"].isUInt64(), "Invalid binary range.");
        const auto offset = range["offset"].asUInt64(), count = range["count"].asUInt64(), stride = range["stride"].asUInt64();
        Require(stride == expectedStride && count <= maximumCount && offset >= 8 && offset <= size
            && count <= (size - offset) / expectedStride, "Binary range exceeds its payload.");
        position = static_cast<std::size_t>(offset); end = position + static_cast<std::size_t>(count) * expectedStride;
        if (count) {
            const auto next = ranges.lower_bound(position);
            Require((next == ranges.end() || next->first >= end)
                && (next == ranges.begin() || std::prev(next)->second <= position), "Overlapping binary ranges are not permitted.");
            ranges.emplace(position, end);
        }
        return static_cast<std::size_t>(count);
    }
    template <typename T> T Scalar() {
        Require(position <= end && sizeof(T) <= end - position, "Truncated binary scalar.");
        std::array<std::uint8_t, sizeof(T)> raw{};
        Raw(raw);
        if constexpr (std::endian::native != std::endian::little) std::reverse(raw.begin(), raw.end());
        const auto value = std::bit_cast<T>(raw);
        if constexpr (std::is_floating_point_v<T>) Finite(value);
        return value;
    }
    template <typename V> V Vec() {
        V value(0); for (glm::length_t i = 0; i < value.length(); ++i) value[i] = Scalar<typename V::value_type>();
        return value;
    }
};

std::uint32_t U32(const Json::Value& value, const char* description) {
    Require(value.isUInt() && value.asUInt64() <= std::numeric_limits<std::uint32_t>::max(), std::string("Invalid ") + description);
    return value.asUInt();
}
std::int32_t I32(const Json::Value& value, const char* description) {
    Require(value.isInt(), std::string("Invalid ") + description); return value.asInt();
}
double Number(const Json::Value& value, const char* description) {
    Require(value.isNumeric(), std::string("Invalid ") + description); const auto result = value.asDouble(); Finite(result); return result;
}
void Array(const Json::Value& value, const char* description) {
    Require(value.isArray() && value.size() <= MaxElements, std::string("Invalid ") + description + " array.");
}

Json::Value Quaternion(const glm::dquat& q) {
    Json::Value result(Json::arrayValue);
    for (int i = 0; i < 4; ++i) { Finite(q[i]); result.append(q[i]); }
    return result;
}
glm::dquat ReadQuaternion(const Json::Value& value) {
    const auto q = ReadVector<glm::dvec4>(value); return glm::dquat(q.w, q.x, q.y, q.z);
}
Json::Value Transform(const BoneTransform& transform) {
    Json::Value result(Json::objectValue);
    result["translation"] = Vector(transform.translation); result["rotation"] = Quaternion(transform.rotation);
    result["scale"] = Vector(transform.scale); return result;
}
BoneTransform ReadTransform(const Json::Value& value) {
    BoneTransform result; result.translation = ReadVector<glm::dvec3>(value["translation"]);
    result.rotation = ReadQuaternion(value["rotation"]); result.scale = ReadVector<glm::dvec3>(value["scale"]);
    return result;
}
Json::Value BoundsJson(const Bounds& bounds) {
    Json::Value result(Json::objectValue); result["valid"] = bounds.valid;
    result["minimum"] = Vector(bounds.minimum); result["maximum"] = Vector(bounds.maximum); return result;
}
Bounds ReadBounds(const Json::Value& value) {
    Bounds bounds; bounds.valid = value["valid"].asBool(); bounds.minimum = ReadVector<glm::dvec3>(value["minimum"]);
    bounds.maximum = ReadVector<glm::dvec3>(value["maximum"]); return bounds;
}
Json::Value Strings(const std::vector<std::string>& values) {
    Json::Value result(Json::arrayValue); for (const auto& value : values) result.append(value); return result;
}
std::vector<std::string> ReadStrings(const Json::Value& values) {
    Array(values, "string"); std::vector<std::string> result;
    for (const auto& value : values) { Require(value.isString(), "Expected a string."); result.push_back(value.asString()); }
    return result;
}
// Import annotations deliberately live in the package manifest. They neither
// change mesh payload bytes nor participate in skeleton/animation binding IDs.
constexpr std::size_t MaxRegionStringBytes = 16384;
constexpr std::size_t MaxRegionListItems = 4096;
constexpr std::size_t MaxRegionTextBytes = 4 * 1024 * 1024;
constexpr std::size_t MaxRegionTotalListItems = 100000;
void CheckRegionString(const std::string& value) {
    Require(value.size() <= MaxRegionStringBytes && value.find('\0') == std::string::npos,
        "Mesh region metadata contains an oversized string or embedded NUL.");
}
void CheckRegionStrings(const std::vector<std::string>& values) {
    Require(values.size() <= MaxRegionListItems, "Too many mesh region metadata strings.");
    for (const auto& value : values) CheckRegionString(value);
}
std::string ReadRegionString(const Json::Value& value) {
    Require(value.isString(), "Expected a mesh region metadata string.");
    auto result = value.asString(); CheckRegionString(result); return result;
}
std::vector<std::string> ReadRegionStrings(const Json::Value& values) {
    Require(values.isArray() && values.size() <= MaxRegionListItems, "Invalid mesh region metadata string array.");
    std::vector<std::string> result; result.reserve(values.size());
    for (const auto& value : values) result.push_back(ReadRegionString(value));
    return result;
}
const char* RegionKindString(MeshRegionKind kind) {
    switch (kind) {
    case MeshRegionKind::Unknown: return "unknown";
    case MeshRegionKind::Body: return "body";
    case MeshRegionKind::Hair: return "hair";
    case MeshRegionKind::Garment: return "garment";
    case MeshRegionKind::Accessory: return "accessory";
    }
    throw std::runtime_error("Invalid mesh region kind.");
}
MeshRegionKind ReadRegionKind(const Json::Value& value) {
    Require(value.isString(), "Expected a mesh region kind string.");
    const auto text = ReadRegionString(value);
    for (const auto kind : {MeshRegionKind::Unknown, MeshRegionKind::Body, MeshRegionKind::Hair,
        MeshRegionKind::Garment, MeshRegionKind::Accessory}) if (text == RegionKindString(kind)) return kind;
    throw std::runtime_error("Unknown mesh region kind: " + text);
}
Json::Value MeshRegionsJson(const std::vector<MeshRegionInfo>& regions) {
    Json::Value result(Json::arrayValue);
    for (const auto& region : regions) {
        Json::Value value(Json::objectValue);
        value["mesh"] = region.mesh; value["kind"] = RegionKindString(region.kind); value["confidence"] = region.confidence;
        value["authoredCloth"] = region.authoredCloth; value["requiresReview"] = region.requiresReview;
        value["userOverride"] = region.userOverride;
        value["sourceObject"] = region.sourceObject; value["sourceMesh"] = region.sourceMesh;
        value["sourceMaterial"] = region.sourceMaterial; value["clothPinGroup"] = region.clothPinGroup;
        value["collections"] = Strings(region.collections); value["vertexGroups"] = Strings(region.vertexGroups);
        value["modifiers"] = Strings(region.modifiers); value["evidence"] = Strings(region.evidence);
        result.append(std::move(value));
    }
    return result;
}
std::vector<MeshRegionInfo> ReadMeshRegions(const Json::Value& values, std::size_t meshCount) {
    Require(values.isArray() && values.size() <= meshCount && values.size() <= 100000,
        "Invalid mesh region array or too many region records.");
    std::vector<MeshRegionInfo> result; result.reserve(values.size());
    for (const auto& value : values) {
        Require(value.isObject(), "Expected a mesh region object.");
        MeshRegionInfo region; region.mesh = U32(value["mesh"], "mesh region index");
        region.kind = ReadRegionKind(value["kind"]);
        const auto confidence = Number(value["confidence"], "mesh region confidence");
        Require(confidence >= 0 && confidence <= 1, "Mesh region confidence must be between zero and one.");
        region.confidence = static_cast<float>(confidence);
        for (const auto* name : {"authoredCloth", "requiresReview", "userOverride"})
            Require(value[name].isBool(), std::string("Expected mesh region boolean: ") + name);
        region.authoredCloth = value["authoredCloth"].asBool(); region.requiresReview = value["requiresReview"].asBool();
        region.userOverride = value["userOverride"].asBool();
        region.sourceObject = ReadRegionString(value["sourceObject"]); region.sourceMesh = ReadRegionString(value["sourceMesh"]);
        region.sourceMaterial = ReadRegionString(value["sourceMaterial"]); region.clothPinGroup = ReadRegionString(value["clothPinGroup"]);
        region.collections = ReadRegionStrings(value["collections"]); region.vertexGroups = ReadRegionStrings(value["vertexGroups"]);
        region.modifiers = ReadRegionStrings(value["modifiers"]); region.evidence = ReadRegionStrings(value["evidence"]);
        result.push_back(std::move(region));
    }
    return result;
}
Json::Value SkeletonJson(const SkeletonAsset& skeleton) {
    auto root = Header("Skeleton", skeleton.id, skeleton.name); root["bones"] = Json::Value(Json::arrayValue);
    for (const auto& bone : skeleton.bones) {
        Json::Value b; b["name"] = bone.name; b["englishName"] = bone.englishName; b["parent"] = bone.parent;
        b["restLocal"] = Transform(bone.restLocal); b["inverseBind"] = Matrix(bone.inverseBind);
        b["sourceFlags"] = bone.sourceFlags; b["fixedAxis"] = Vector(bone.fixedAxis);
        b["localAxisX"] = Vector(bone.localAxisX); b["localAxisZ"] = Vector(bone.localAxisZ);
        b["inheritParent"] = bone.inheritParent; b["inheritWeight"] = bone.inheritWeight;
        root["bones"].append(b);
    }
    return root;
}
SkeletonAsset ReadSkeleton(const Json::Value& root) {
    CheckVersion(root, "Skeleton"); SkeletonAsset result; result.id = root["id"].asString(); result.name = root["name"].asString();
    Array(root["bones"], "bones"); Require(root["bones"].size() <= 100000, "Too many skeleton bones.");
    for (const auto& b : root["bones"]) {
        SkeletonBone bone; bone.name = b["name"].asString(); bone.englishName = b["englishName"].asString();
        bone.parent = I32(b["parent"], "bone parent"); bone.restLocal = ReadTransform(b["restLocal"]);
        bone.inverseBind = ReadMatrix(b["inverseBind"]); bone.sourceFlags = U32(b["sourceFlags"], "bone flags");
        bone.fixedAxis = ReadVector<glm::dvec3>(b["fixedAxis"]); bone.localAxisX = ReadVector<glm::dvec3>(b["localAxisX"]);
        bone.localAxisZ = ReadVector<glm::dvec3>(b["localAxisZ"]); bone.inheritParent = I32(b["inheritParent"], "inherit parent");
        bone.inheritWeight = static_cast<float>(Number(b["inheritWeight"], "inherit weight")); result.bones.push_back(bone);
    }
    return result;
}
Json::Value AnimationJson(const AnimationSequenceAsset& animation) {
    auto root = Header("AnimationSequence", animation.id, animation.name);
    root["skeletonId"] = animation.skeletonId; root["source"] = animation.source;
    root["generated"] = animation.generated; root["duration"] = animation.duration; root["framesPerSecond"] = animation.framesPerSecond;
    root["baked"] = animation.baked; root["sourceSkeletonId"] = animation.sourceSkeletonId;
    root["diagnostics"] = Strings(animation.diagnostics);
    root["boneTracks"] = Json::Value(Json::arrayValue); root["morphTracks"] = Json::Value(Json::arrayValue);
    for (const auto& track : animation.boneTracks) {
        Json::Value t; t["bone"] = track.bone;
        t["translations"] = Json::Value(Json::arrayValue); t["scales"] = Json::Value(Json::arrayValue); t["rotations"] = Json::Value(Json::arrayValue);
        for (const auto& key : track.translations) { Json::Value k; k["time"] = key.time; k["value"] = Vector(key.value); t["translations"].append(k); }
        for (const auto& key : track.scales) { Json::Value k; k["time"] = key.time; k["value"] = Vector(key.value); t["scales"].append(k); }
        for (const auto& key : track.rotations) { Json::Value k; k["time"] = key.time; k["value"] = Quaternion(key.value); t["rotations"].append(k); }
        root["boneTracks"].append(t);
    }
    for (const auto& track : animation.morphTracks) {
        Json::Value t; t["morph"] = track.morph; t["keys"] = Json::Value(Json::arrayValue);
        for (const auto& key : track.keys) { Json::Value k; k["time"] = key.time; k["value"] = key.value; t["keys"].append(k); }
        root["morphTracks"].append(t);
    }
    return root;
}
AnimationSequenceAsset ReadAnimation(const Json::Value& root) {
    CheckVersion(root, "AnimationSequence"); AnimationSequenceAsset result;
    for (const auto* field : {"id", "name", "skeletonId", "source"}) Require(root[field].isString(), std::string("Invalid animation ") + field + '.');
    Require(root["generated"].isBool(), "Invalid animation generated flag.");
    result.id = root["id"].asString(); result.name = root["name"].asString(); result.skeletonId = root["skeletonId"].asString();
    result.source = root["source"].asString(); result.generated = root["generated"].asBool();
    if (root.isMember("baked")) { Require(root["baked"].isBool(), "Invalid animation baked flag."); result.baked = root["baked"].asBool(); }
    if (root.isMember("sourceSkeletonId")) { Require(root["sourceSkeletonId"].isString(), "Invalid source skeleton ID."); result.sourceSkeletonId = root["sourceSkeletonId"].asString(); }
    if (root.isMember("diagnostics")) result.diagnostics = ReadStrings(root["diagnostics"]);
    result.duration = Number(root["duration"], "animation duration"); result.framesPerSecond = Number(root["framesPerSecond"], "animation frame rate");
    Array(root["boneTracks"], "bone tracks"); Array(root["morphTracks"], "morph tracks");
    Require(root["boneTracks"].size() <= 100000 && root["morphTracks"].size() <= 100000, "Too many animation tracks.");
    std::uint64_t keyCount = 0;
    const auto countKeys = [&](const Json::Value& keys) { keyCount += keys.size(); Require(keyCount <= MaxAnimationKeys, "Animation exceeds two million keys; split the sequence or lower the sample rate."); };
    for (const auto& t : root["boneTracks"]) {
        BoneAnimationTrack track; track.bone = U32(t["bone"], "animation bone");
        Array(t["translations"], "translation keys"); Array(t["scales"], "scale keys"); Array(t["rotations"], "rotation keys");
        countKeys(t["translations"]); countKeys(t["scales"]); countKeys(t["rotations"]);
        for (const auto& k : t["translations"]) track.translations.push_back({Number(k["time"], "key time"), ReadVector<glm::dvec3>(k["value"])});
        for (const auto& k : t["scales"]) track.scales.push_back({Number(k["time"], "key time"), ReadVector<glm::dvec3>(k["value"])});
        for (const auto& k : t["rotations"]) track.rotations.push_back({Number(k["time"], "key time"), ReadQuaternion(k["value"])});
        result.boneTracks.push_back(std::move(track));
    }
    for (const auto& t : root["morphTracks"]) {
        MorphAnimationTrack track; track.morph = U32(t["morph"], "animation morph"); Array(t["keys"], "morph keys");
        countKeys(t["keys"]);
        for (const auto& k : t["keys"]) track.keys.push_back({Number(k["time"], "key time"), Number(k["value"], "morph value")});
        result.morphTracks.push_back(std::move(track));
    }
    return result;
}
Json::Value RigJson(const IkRigAsset& rig) {
    auto root = Header("IKRig", rig.id, rig.name); root["skeletonId"] = rig.skeletonId;
    root["diagnostics"] = Strings(rig.diagnostics); root["humanoid"] = Json::Value(Json::arrayValue); root["chains"] = Json::Value(Json::arrayValue);
    for (const auto& mapping : rig.humanoid) {
        Json::Value m; m["role"] = mapping.role; m["bone"] = mapping.bone; m["confidence"] = mapping.confidence; m["method"] = mapping.method;
        root["humanoid"].append(m);
    }
    for (const auto& chain : rig.chains) {
        Json::Value c; c["name"] = chain.name; c["role"] = chain.role; c["root"] = chain.root; c["effector"] = chain.effector;
        c["targetBone"] = chain.targetBone; c["target"] = Vector(chain.target); c["iterations"] = chain.iterations;
        c["angleLimit"] = chain.angleLimit; c["native"] = chain.native; c["enabled"] = chain.enabled; c["links"] = Json::Value(Json::arrayValue);
        for (const auto& link : chain.links) {
            Json::Value l; l["bone"] = link.bone; l["limited"] = link.limited;
            l["minimum"] = Vector(link.minimum); l["maximum"] = Vector(link.maximum); c["links"].append(l);
        }
        root["chains"].append(c);
    }
    return root;
}
IkRigAsset ReadRig(const Json::Value& root) {
    CheckVersion(root, "IKRig"); IkRigAsset result;
    result.id = root["id"].asString(); result.name = root["name"].asString(); result.skeletonId = root["skeletonId"].asString();
    result.diagnostics = ReadStrings(root["diagnostics"]); Array(root["humanoid"], "humanoid mappings"); Array(root["chains"], "IK chains");
    for (const auto& m : root["humanoid"]) result.humanoid.push_back({m["role"].asString(), I32(m["bone"], "humanoid bone"),
        static_cast<float>(Number(m["confidence"], "mapping confidence")), m["method"].asString()});
    for (const auto& c : root["chains"]) {
        IkChain chain; chain.name = c["name"].asString(); chain.role = c["role"].asString(); chain.root = I32(c["root"], "IK root");
        chain.effector = I32(c["effector"], "IK effector"); chain.targetBone = I32(c["targetBone"], "IK target");
        chain.target = ReadVector<glm::dvec3>(c["target"]); chain.iterations = U32(c["iterations"], "IK iterations");
        chain.angleLimit = Number(c["angleLimit"], "IK angle limit"); chain.native = c["native"].asBool(); chain.enabled = c["enabled"].asBool();
        Array(c["links"], "IK links");
        for (const auto& l : c["links"]) chain.links.push_back({U32(l["bone"], "IK link bone"), l["limited"].asBool(),
            ReadVector<glm::dvec3>(l["minimum"]), ReadVector<glm::dvec3>(l["maximum"])});
        result.chains.push_back(std::move(chain));
    }
    return result;
}

Json::Value MaterialJson(const PbrMaterial& material) {
    Json::Value m; m["name"] = material.name; m["baseColor"] = Vector(material.baseColor); m["emissive"] = Vector(material.emissive);
    m["metallic"] = material.metallic; m["roughness"] = material.roughness; m["ao"] = material.ao; m["normalScale"] = material.normalScale;
    m["uvTransform"] = Vector(material.uvTransform); m["alphaMode"] = static_cast<unsigned>(material.alphaMode);
    m["alphaCutoff"] = material.alphaCutoff; m["twoSided"] = material.twoSided; m["textures"] = Json::Value(Json::arrayValue);
    for (const auto texture : material.textures) m["textures"].append(texture); return m;
}
PbrMaterial ReadMaterial(const Json::Value& m) {
    PbrMaterial result; result.name = m["name"].asString(); result.baseColor = ReadVector<glm::vec4>(m["baseColor"]);
    result.emissive = ReadVector<glm::vec3>(m["emissive"]); result.metallic = static_cast<float>(Number(m["metallic"], "metallic"));
    result.roughness = static_cast<float>(Number(m["roughness"], "roughness")); result.ao = static_cast<float>(Number(m["ao"], "AO"));
    result.normalScale = static_cast<float>(Number(m["normalScale"], "normal scale")); result.uvTransform = ReadVector<glm::vec4>(m["uvTransform"]);
    const auto mode = U32(m["alphaMode"], "alpha mode"); Require(mode <= 2, "Invalid material alpha mode."); result.alphaMode = static_cast<AlphaMode>(mode);
    result.alphaCutoff = static_cast<float>(Number(m["alphaCutoff"], "alpha cutoff")); result.twoSided = m["twoSided"].asBool();
    Require(m["textures"].isArray() && m["textures"].size() == result.textures.size(), "Invalid material texture slots.");
    for (unsigned i = 0; i < result.textures.size(); ++i) result.textures[i] = I32(m["textures"][i], "texture slot");
    return result;
}

Json::Value MeshJson(const SkeletalAssetPackage& asset, BinaryWriter& binary,
    const std::string& packageId, const std::string& packageName, const std::string& skeletonId) {
    auto root = Header("SkeletalMesh", StableId("mesh", packageId), packageName); root["skeletonId"] = skeletonId;
    root["payload"] = "model.skmesh.bin"; root["byteOrder"] = "little"; root["vertexStride"] = 60;
    root["bounds"] = BoundsJson(asset.geometry->bounds); root["warnings"] = Strings(asset.geometry->warnings);
    root["meshes"] = Json::Value(Json::arrayValue); root["instances"] = Json::Value(Json::arrayValue);
    root["materials"] = Json::Value(Json::arrayValue); root["textures"] = Json::Value(Json::arrayValue); root["morphs"] = Json::Value(Json::arrayValue);
    for (const unsigned char c : std::string("SKASSET1")) binary.Scalar(c);
    for (std::size_t i = 0; i < asset.geometry->meshes.size(); ++i) {
        const auto& mesh = asset.geometry->meshes[i]; const auto& skin = asset.skins[i]; Json::Value m;
        m["name"] = mesh.name; m["materialIndex"] = mesh.materialIndex; m["bounds"] = BoundsJson(mesh.bounds);
        auto offset = binary.Size();
        for (const auto& v : mesh.vertices) { binary.Vec(v.position); binary.Vec(v.normal); binary.Vec(v.uv); binary.Vec(v.tangent); }
        m["vertices"] = binary.Range(offset, mesh.vertices.size(), 60); offset = binary.Size();
        for (auto index : mesh.indices) binary.Scalar(index);
        m["indices"] = binary.Range(offset, mesh.indices.size(), 4); offset = binary.Size();
        for (const auto& v : skin.vertices) {
            for (auto joint : v.joints) binary.Scalar(joint);
            binary.Vec(v.weights); binary.Scalar(static_cast<std::uint8_t>(v.mode)); binary.Vec(v.sdefC); binary.Vec(v.sdefR0); binary.Vec(v.sdefR1);
        }
        m["skin"] = binary.Range(offset, skin.vertices.size(), 69); offset = binary.Size();
        if (!skin.influenceOffsets.empty()) {
            for (const auto begin : skin.influenceOffsets) binary.Scalar(begin);
            m["fullInfluenceOffsets"] = binary.Range(offset, skin.influenceOffsets.size(), 4);
            offset = binary.Size();
            for (const auto& influence : skin.influences) {
                binary.Scalar(influence.joint); binary.Scalar(influence.weight);
            }
            m["fullInfluences"] = binary.Range(offset, skin.influences.size(), 8);
            offset = binary.Size();
        }
        for (const auto index : skin.sourceVertexIds) binary.Scalar(index);
        m["sourceVertexIds"] = binary.Range(offset, skin.sourceVertexIds.size(), 4);
        m["bindTransform"] = Matrix(skin.bindTransform); m["boneFromMesh"] = Json::Value(Json::arrayValue);
        for (const auto& matrix : skin.boneFromMesh) m["boneFromMesh"].append(Matrix(matrix));
        root["meshes"].append(m);
    }
    for (const auto& instance : asset.geometry->instances) {
        Json::Value i; i["nodeName"] = instance.nodeName; i["meshIndex"] = instance.meshIndex; i["transform"] = Matrix(instance.transform); root["instances"].append(i);
    }
    for (const auto& material : asset.geometry->materials) root["materials"].append(MaterialJson(material));
    for (const auto& texture : asset.geometry->textures) {
        Json::Value t; t["name"] = texture.name; t["width"] = texture.width; t["height"] = texture.height;
        t["hasAlpha"] = texture.hasAlpha; t["linearColor"] = texture.linearColor; t["format"] = texture.rgba.empty() ? "RGBA32F" : "RGBA8";
        const auto offset = binary.Size();
        if (!texture.rgba.empty()) {
            // Bytes have no endian conversion. Large decoded albedo atlases
            // must be appended once instead of invoking scalar encoding for
            // every channel (particularly expensive in Debug builds).
            binary.Raw(texture.rgba);
            t["pixels"] = binary.Range(offset, texture.rgba.size(), 1);
        }
        else { for (const auto channel : texture.rgba32f) binary.Scalar(channel); t["pixels"] = binary.Range(offset, texture.rgba32f.size(), 4); }
        root["textures"].append(t);
    }
    for (const auto& morph : asset.morphs) {
        Json::Value m; m["name"] = morph.name; m["englishName"] = morph.englishName; m["panel"] = morph.panel; m["sourceType"] = morph.sourceType;
        const auto offset = binary.Size();
        for (const auto& delta : morph.deltas) { binary.Scalar(delta.mesh); binary.Scalar(delta.vertex); binary.Vec(delta.position); binary.Vec(delta.normal); }
        m["deltas"] = binary.Range(offset, morph.deltas.size(), 44); root["morphs"].append(m);
    }
    root["payloadBytes"] = Json::UInt64(binary.Size()); root["payloadHashFNV1a64"] = binary.hash.String(); return root;
}

void ReadMesh(const Json::Value& root, const fs::path& directory, SkeletalAssetPackage& asset) {
    CheckVersion(root, "SkeletalMesh"); Require(root["byteOrder"].asString() == "little", "Unsupported payload byte order.");
    Require(root["skeletonId"].asString() == asset.skeleton.id, "Skeletal mesh references another skeleton.");
    BinaryReader binary(Child(directory, root["payload"]));
    Require(root["payloadBytes"].isUInt64() && root["payloadBytes"].asUInt64() == binary.size, "Payload size mismatch.");
    Require(root["payloadHashFNV1a64"].asString() == binary.hash, "Payload checksum mismatch.");
    Require(binary.Signature(), "Invalid payload signature.");
    asset.geometry = std::make_shared<StaticModel>(); auto& geometry = *asset.geometry;
    geometry.sourcePath = asset.sourcePath; geometry.bounds = ReadBounds(root["bounds"]); geometry.warnings = ReadStrings(root["warnings"]);
    Array(root["meshes"], "meshes"); Array(root["instances"], "instances"); Array(root["materials"], "materials");
    Array(root["textures"], "textures"); Array(root["morphs"], "morphs");
    std::uint64_t fullOffsetCount = 0, fullInfluenceCount = 0;
    for (const auto& m : root["meshes"]) {
        StaticMesh mesh; MeshSkin skin; mesh.name = m["name"].asString(); mesh.materialIndex = U32(m["materialIndex"], "material index"); mesh.bounds = ReadBounds(m["bounds"]);
        const auto vertexCount = binary.Range(m["vertices"], 60); mesh.vertices.resize(vertexCount);
        for (auto& v : mesh.vertices) { v.position = binary.Vec<glm::dvec3>(); v.normal = binary.Vec<glm::vec3>(); v.uv = binary.Vec<glm::vec2>(); v.tangent = binary.Vec<glm::vec4>(); }
        const auto indexCount = binary.Range(m["indices"], 4); mesh.indices.resize(indexCount);
        for (auto& index : mesh.indices) index = binary.Scalar<std::uint32_t>();
        const auto skinCount = binary.Range(m["skin"], 69); Require(skinCount == vertexCount, "Skin vertex count differs from its mesh."); skin.vertices.resize(skinCount);
        for (auto& v : skin.vertices) {
            for (auto& joint : v.joints) joint = binary.Scalar<std::uint32_t>();
            v.weights = binary.Vec<glm::vec4>(); const auto mode = binary.Scalar<std::uint8_t>(); Require(mode <= 4, "Invalid skinning mode.");
            v.mode = static_cast<SkinningMode>(mode); v.sdefC = binary.Vec<glm::vec3>(); v.sdefR0 = binary.Vec<glm::vec3>(); v.sdefR1 = binary.Vec<glm::vec3>();
        }
        const bool hasFullOffsets = m.isMember("fullInfluenceOffsets"), hasFullInfluences = m.isMember("fullInfluences");
        Require(hasFullOffsets == hasFullInfluences, "Full skin influence ranges must be present together.");
        if (hasFullOffsets) {
            const auto offsetCount = binary.Range(m["fullInfluenceOffsets"], 4);
            Require(offsetCount == vertexCount + 1 && fullOffsetCount <= MaxElements - offsetCount,
                "Full skin influence offsets differ from mesh vertices or exceed the element budget.");
            fullOffsetCount += offsetCount; skin.influenceOffsets.resize(offsetCount);
            for (auto& begin : skin.influenceOffsets) begin = binary.Scalar<std::uint32_t>();
            const auto influenceCount = binary.Range(m["fullInfluences"], 8);
            Require(influenceCount > 0 && fullInfluenceCount <= MaxElements - influenceCount,
                "Full skin influences are empty or exceed the element budget.");
            fullInfluenceCount += influenceCount; skin.influences.resize(influenceCount);
            for (auto& influence : skin.influences) {
                influence.joint = binary.Scalar<std::uint32_t>(); influence.weight = binary.Scalar<float>();
            }
        }
        const auto sourceCount = binary.Range(m["sourceVertexIds"], 4); skin.sourceVertexIds.resize(sourceCount);
        for (auto& id : skin.sourceVertexIds) id = binary.Scalar<std::uint32_t>();
        skin.bindTransform = ReadMatrix(m["bindTransform"]); Array(m["boneFromMesh"], "bone offsets");
        for (const auto& matrix : m["boneFromMesh"]) skin.boneFromMesh.push_back(ReadMatrix(matrix));
        geometry.meshes.push_back(std::move(mesh)); asset.skins.push_back(std::move(skin));
    }
    for (const auto& i : root["instances"]) geometry.instances.push_back({i["nodeName"].asString(), U32(i["meshIndex"], "instance mesh"), ReadMatrix(i["transform"])});
    for (const auto& m : root["materials"]) geometry.materials.push_back(ReadMaterial(m));
    for (const auto& t : root["textures"]) {
        TextureImage texture; texture.name = t["name"].asString(); texture.width = U32(t["width"], "texture width"); texture.height = U32(t["height"], "texture height");
        texture.hasAlpha = t["hasAlpha"].asBool(); texture.linearColor = t["linearColor"].asBool();
        Require(texture.width > 0 && texture.height > 0 && texture.width <= 32768 && texture.height <= 32768, "Invalid texture dimensions.");
        const auto pixels = std::uint64_t(texture.width) * texture.height * 4; const auto format = t["format"].asString();
        Require(format == "RGBA8" || format == "RGBA32F", "Unsupported texture payload format.");
        const std::size_t channelBytes = format == "RGBA8" ? 1 : 4;
        // Texture ranges count scalar channels, not vertices. A valid 6144²
        // RGBA atlas already has 150,994,944 channels, above the mesh-element
        // budget. Bound it by checked dimensions and the unchanged payload
        // byte budget instead; Range still checks offsets and overlap.
        Require(pixels <= MaxBinaryBytes / channelBytes, "Texture payload exceeds the binary byte budget.");
        const auto count = binary.Range(t["pixels"], channelBytes, pixels); Require(count == pixels, "Texture dimensions do not match pixel payload.");
        if (format == "RGBA8") {
            // Range() already checked the complete span and overlap rules.
            texture.rgba.resize(count); binary.Raw(texture.rgba);
        }
        else { texture.rgba32f.resize(count); for (auto& channel : texture.rgba32f) channel = binary.Scalar<float>(); }
        geometry.textures.push_back(std::move(texture));
    }
    for (const auto& m : root["morphs"]) {
        MorphTarget morph; morph.name = m["name"].asString(); morph.englishName = m["englishName"].asString();
        const auto panel = U32(m["panel"], "morph panel"), sourceType = U32(m["sourceType"], "morph type");
        Require(panel <= 255 && sourceType <= 255, "Invalid morph metadata."); morph.panel = static_cast<std::uint8_t>(panel); morph.sourceType = static_cast<std::uint8_t>(sourceType);
        const auto count = binary.Range(m["deltas"], 44); morph.deltas.resize(count);
        for (auto& d : morph.deltas) { d.mesh = binary.Scalar<std::uint32_t>(); d.vertex = binary.Scalar<std::uint32_t>(); d.position = binary.Vec<glm::dvec3>(); d.normal = binary.Vec<glm::vec3>(); }
        asset.morphs.push_back(std::move(morph));
    }
}

template <typename V> void CheckVector(const V& value) {
    for (glm::length_t i = 0; i < value.length(); ++i) Finite(value[i]);
}
void CheckMatrix(const glm::dmat4& matrix) { for (int i = 0; i < 4; ++i) CheckVector(matrix[i]); }
void CheckQuaternion(const glm::dquat& q) {
    for (int i = 0; i < 4; ++i) Finite(q[i]); const auto lengthSquared = glm::dot(q, q); Finite(lengthSquared);
    Require(lengthSquared > 1e-16, "Zero-length rotation quaternion.");
}
void CheckTransform(const BoneTransform& transform) {
    CheckVector(transform.translation); CheckVector(transform.scale); CheckQuaternion(transform.rotation);
    Require(std::abs(transform.scale.x) > 1e-12 && std::abs(transform.scale.y) > 1e-12 && std::abs(transform.scale.z) > 1e-12, "Singular bone scale.");
}
void CheckBounds(const Bounds& bounds) {
    CheckVector(bounds.minimum); CheckVector(bounds.maximum);
    if (bounds.valid) for (int i = 0; i < 3; ++i) Require(bounds.minimum[i] <= bounds.maximum[i], "Inverted asset bounds.");
}
template <typename Keys> void CheckKeys(const Keys& keys, double duration) {
    double previous = -1;
    for (const auto& key : keys) {
        Finite(key.time); Require(key.time >= 0 && key.time <= duration + 1e-5 && key.time >= previous, "Animation key time is outside duration or unsorted.");
        previous = key.time;
        using Value = std::remove_cvref_t<decltype(key.value)>;
        if constexpr (std::is_arithmetic_v<Value>) Finite(key.value);
        else if constexpr (std::is_same_v<Value, glm::dquat>) CheckQuaternion(key.value);
        else CheckVector(key.value);
    }
}

void ValidateAnimation(const AnimationSequenceAsset& sequence, const SkeletalAssetPackage& target, bool standalone) {
    Require((!standalone || !target.skeleton.id.empty()) && !target.skeleton.bones.empty() && target.skeleton.bones.size() <= 100000,
        "Animation target requires an identified skeleton with 1 to 100000 bones.");
    Require((!standalone && sequence.skeletonId.empty()) || sequence.skeletonId == target.skeleton.id,
        "Animation references a different target skeleton.");
    if (standalone) {
        Require(target.morphs.size() <= 100000, "Animation target has too many expression entries.");
        std::vector<std::uint8_t> visited(target.skeleton.bones.size(), 0);
        for (const auto& bone : target.skeleton.bones) {
            CheckTransform(bone.restLocal);
            Require(bone.parent >= -1 && (bone.parent < 0 || static_cast<std::size_t>(bone.parent) < visited.size()),
                "Animation target bone parent is out of range.");
        }
        for (std::size_t b = 0; b < visited.size(); ++b) {
            auto current = static_cast<std::int32_t>(b);
            while (current >= 0 && visited[current] == 0) { visited[current] = 1; current = target.skeleton.bones[current].parent; }
            Require(current < 0 || visited[current] != 1, "Animation target skeleton contains a hierarchy cycle.");
            current = static_cast<std::int32_t>(b);
            while (current >= 0 && visited[current] == 1) { visited[current] = 2; current = target.skeleton.bones[current].parent; }
        }
    }
    if (standalone) Require(!sequence.id.empty() && !sequence.name.empty(), "Animation asset ID and name must not be empty.");
    Finite(sequence.duration); Finite(sequence.framesPerSecond);
    Require(sequence.duration >= 0 && sequence.framesPerSecond > 0 && sequence.framesPerSecond <= 1000,
        "Invalid animation duration/frame rate (maximum 1000 fps).");
    Require(sequence.boneTracks.size() <= 100000 && sequence.morphTracks.size() <= 100000, "Too many animation tracks.");
    std::uint64_t keyCount = 0;
    const auto check = [&](const auto& keys) {
        keyCount += keys.size(); Require(keyCount <= MaxAnimationKeys, "Animation exceeds two million keys; split the sequence or lower the sample rate.");
        CheckKeys(keys, sequence.duration);
        for (std::size_t i = 1; i < keys.size(); ++i) Require(keys[i].time > keys[i - 1].time, "Duplicate animation key time.");
    };
    std::unordered_set<std::uint32_t> bones, morphs;
    for (const auto& track : sequence.boneTracks) {
        Require(track.bone < target.skeleton.bones.size() && bones.insert(track.bone).second, "Invalid or duplicate animation bone track.");
        Require(!track.translations.empty() || !track.scales.empty() || !track.rotations.empty(), "Animation bone track has no keys.");
        check(track.translations); check(track.scales); check(track.rotations);
        for (const auto& key : track.scales) for (int axis = 0; axis < 3; ++axis)
            Require(std::abs(key.value[axis]) > 1e-12, "Singular animated scale.");
    }
    for (const auto& track : sequence.morphTracks) {
        Require(track.morph < target.morphs.size() && morphs.insert(track.morph).second, "Invalid or duplicate animation morph track.");
        Require(!track.keys.empty(), "Animation morph track has no keys."); check(track.keys);
    }
}

std::string AnimationLayoutHash(const SkeletalAssetPackage& target) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](const std::string& value) {
        for (const unsigned char c : std::to_string(value.size()) + ":" + value) { hash ^= c; hash *= 1099511628211ull; }
    };
    const auto addDouble = [&](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (unsigned shift = 0; shift < 64; shift += 8) { hash ^= (bits >> shift) & 255; hash *= 1099511628211ull; }
    };
    add(target.skeleton.id); add(std::to_string(target.skeleton.bones.size()));
    for (const auto& bone : target.skeleton.bones) {
        add(bone.name); add(bone.englishName); add(std::to_string(bone.parent));
        for (int i = 0; i < 3; ++i) { addDouble(bone.restLocal.translation[i]); addDouble(bone.restLocal.scale[i]); }
        for (int i = 0; i < 4; ++i) addDouble(bone.restLocal.rotation[i]);
    }
    add(std::to_string(target.morphs.size()));
    for (const auto& morph : target.morphs) { add(morph.name); add(morph.englishName); add(std::to_string(morph.sourceType)); }
    std::ostringstream result; result << std::hex << hash; return result.str();
}

void Validate(const SkeletalAssetPackage& asset) {
    Require(asset.version == SkeletalAssetVersion, "Unsupported skeletal package version.");
    Require(asset.geometry != nullptr, "Missing skeletal mesh geometry.");
    const auto boneCount = asset.skeleton.bones.size();
    Require(boneCount > 0 && boneCount <= 100000, "Skeleton must contain 1 to 100000 bones.");
    auto validBone = [&](std::int32_t bone) { return bone >= -1 && (bone == -1 || static_cast<std::size_t>(bone) < boneCount); };
    std::vector<std::uint8_t> colors(boneCount, 0);
    for (const auto& bone : asset.skeleton.bones)
        Require(validBone(bone.parent) && validBone(bone.inheritParent), "Bone parent/inherit parent index out of range.");
    for (std::size_t i = 0; i < boneCount; ++i) {
        const auto& bone = asset.skeleton.bones[i];
        Require(bone.parent != static_cast<std::int32_t>(i), "Bone cannot be its own parent.");
        CheckTransform(bone.restLocal); CheckMatrix(bone.inverseBind); CheckVector(bone.fixedAxis);
        CheckVector(bone.localAxisX); CheckVector(bone.localAxisZ); Finite(bone.inheritWeight);
        auto current = static_cast<std::int32_t>(i);
        while (current >= 0 && colors[current] == 0) { colors[current] = 1; current = asset.skeleton.bones[current].parent; }
        Require(current < 0 || colors[current] != 1, "Cycle in skeleton hierarchy.");
        current = static_cast<std::int32_t>(i);
        while (current >= 0 && colors[current] == 1) { colors[current] = 2; current = asset.skeleton.bones[current].parent; }
    }
    const auto& geometry = *asset.geometry;
    Require(!geometry.meshes.empty() && geometry.meshes.size() == asset.skins.size(), "Every skeletal mesh must have corresponding skin data.");
    Require(!geometry.materials.empty(), "Skeletal geometry has no materials."); CheckBounds(geometry.bounds);
    Require(asset.meshRegions.size() <= geometry.meshes.size() && asset.meshRegions.size() <= 100000,
        "Too many mesh region records or more records than skeletal meshes.");
    std::unordered_set<std::uint32_t> regionMeshes;
    std::size_t regionTextBytes = 0, regionListItems = 0;
    for (const auto& region : asset.meshRegions) {
        Require(region.mesh < geometry.meshes.size() && regionMeshes.insert(region.mesh).second,
            "Mesh region index is out of range or duplicated.");
        (void)RegionKindString(region.kind); Finite(region.confidence);
        Require(region.confidence >= 0 && region.confidence <= 1, "Mesh region confidence must be between zero and one.");
        for (const auto* value : {&region.sourceObject, &region.sourceMesh, &region.sourceMaterial, &region.clothPinGroup}) {
            CheckRegionString(*value); regionTextBytes += value->size();
        }
        for (const auto* values : {&region.collections, &region.vertexGroups, &region.modifiers, &region.evidence}) {
            CheckRegionStrings(*values);
            regionListItems += values->size();
            for (const auto& value : *values) regionTextBytes += value.size();
        }
        Require(regionTextBytes <= MaxRegionTextBytes && regionListItems <= MaxRegionTotalListItems,
            "Mesh region metadata exceeds the package annotation budget.");
    }
    std::uint64_t fullOffsetCount = 0, fullInfluenceCount = 0;
    for (std::size_t i = 0; i < geometry.meshes.size(); ++i) {
        const auto& mesh = geometry.meshes[i]; const auto& skin = asset.skins[i];
        Require(!mesh.vertices.empty() && !mesh.indices.empty() && mesh.indices.size() % 3 == 0, "Skeletal mesh is empty or has incomplete triangles.");
        Require(mesh.materialIndex < geometry.materials.size(), "Mesh material index out of range.");
        Require(mesh.vertices.size() == skin.vertices.size(), "Skin vertex count differs from its mesh.");
        Require(skin.sourceVertexIds.empty() || skin.sourceVertexIds.size() == mesh.vertices.size(), "Source vertex ID count differs from its mesh.");
        Require(skin.boneFromMesh.empty() || skin.boneFromMesh.size() == boneCount, "Mesh-specific bone offset count differs from skeleton.");
        Require(skin.influenceOffsets.empty() == skin.influences.empty(), "Full skin offsets and influences must both be empty or populated.");
        if (!skin.influenceOffsets.empty()) {
            Require(skin.influenceOffsets.size() == mesh.vertices.size() + 1,
                "Full skin influence offsets differ from mesh vertices.");
            Require(skin.influenceOffsets.size() <= MaxElements && skin.influences.size() <= MaxElements
                && fullOffsetCount <= MaxElements - skin.influenceOffsets.size()
                && fullInfluenceCount <= MaxElements - skin.influences.size(), "Full skin data exceeds the element budget.");
            fullOffsetCount += skin.influenceOffsets.size(); fullInfluenceCount += skin.influences.size();
            Require(fullOffsetCount * 4 + fullInfluenceCount * 8 <= MaxBinaryBytes, "Full skin data exceeds the binary byte budget.");
            Require(skin.influenceOffsets.front() == 0 && skin.influenceOffsets.back() == skin.influences.size(),
                "Full skin influence offsets must span every influence exactly.");
            // Reuse a bone-sized visitation array instead of allocating a set
            // per vertex on densely subdivided character meshes.
            std::vector<std::size_t> visited(boneCount, std::numeric_limits<std::size_t>::max());
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
                const auto begin = skin.influenceOffsets[v], end = skin.influenceOffsets[v + 1];
                Require(begin < end && end <= skin.influences.size(), "Full skin influence offsets must increase for every vertex.");
                double total = 0;
                for (auto k = begin; k < end; ++k) {
                    const auto& influence = skin.influences[k]; Finite(influence.weight);
                    Require(influence.joint < boneCount, "Full skin influence joint exceeds skeleton.");
                    Require(influence.weight > 0, "Full skin influence weights must be positive.");
                    Require(visited[influence.joint] != v, "Full skin influences contain a duplicate joint for a vertex.");
                    visited[influence.joint] = v; total += influence.weight;
                }
                Require(std::abs(total - 1.0) <= 0.002, "Full skin influence weights must sum to one.");
            }
        }
        CheckBounds(mesh.bounds); CheckMatrix(skin.bindTransform);
        for (const auto& matrix : skin.boneFromMesh) CheckMatrix(matrix);
        for (const auto index : mesh.indices) Require(index < mesh.vertices.size(), "Triangle index exceeds mesh vertex range.");
        for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
            const auto& vertex = mesh.vertices[v]; const auto& weight = skin.vertices[v];
            CheckVector(vertex.position); CheckVector(vertex.normal); CheckVector(vertex.uv); CheckVector(vertex.tangent);
            CheckVector(weight.weights); CheckVector(weight.sdefC); CheckVector(weight.sdefR0); CheckVector(weight.sdefR1);
            Require(static_cast<unsigned>(weight.mode) <= 4, "Invalid skinning mode.");
            double total = 0;
            for (int k = 0; k < 4; ++k) {
                Require(weight.weights[k] >= 0, "Negative skin weight."); total += weight.weights[k];
                if (weight.weights[k] > 0) Require(weight.joints[k] < boneCount, "Skin joint index exceeds skeleton.");
            }
            Require(std::abs(total - 1.0) <= 0.002, "Skin weights must sum to one.");
        }
    }
    for (const auto& instance : geometry.instances) { Require(instance.meshIndex < geometry.meshes.size(), "Mesh instance index out of range."); CheckMatrix(instance.transform); }
    for (const auto& texture : geometry.textures) {
        Require(texture.width > 0 && texture.height > 0 && texture.width <= 32768 && texture.height <= 32768, "Invalid texture dimensions.");
        const auto count = std::uint64_t(texture.width) * texture.height * 4;
        Require((texture.rgba.size() == count && texture.rgba32f.empty()) || (texture.rgba32f.size() == count && texture.rgba.empty()), "Texture dimensions do not match pixel payload.");
        Require(count <= MaxBinaryBytes / (texture.rgba.empty() ? 4 : 1), "Texture payload exceeds the binary byte budget.");
        for (const auto channel : texture.rgba32f) Finite(channel);
    }
    for (const auto& material : geometry.materials) {
        CheckVector(material.baseColor); CheckVector(material.emissive); CheckVector(material.uvTransform);
        Finite(material.metallic); Finite(material.roughness); Finite(material.ao); Finite(material.normalScale); Finite(material.alphaCutoff);
        Require(static_cast<unsigned>(material.alphaMode) <= 2, "Invalid material alpha mode.");
        for (const auto texture : material.textures) Require(texture >= -1 && (texture == -1 || static_cast<std::size_t>(texture) < geometry.textures.size()), "Material texture reference out of range.");
    }
    for (const auto& morph : asset.morphs) for (const auto& delta : morph.deltas) {
        Require(delta.mesh < geometry.meshes.size() && delta.vertex < geometry.meshes[delta.mesh].vertices.size(), "Morph vertex reference out of range.");
        CheckVector(delta.position); CheckVector(delta.normal);
    }
    std::unordered_set<std::string> animationIds;
    for (const auto& sequence : asset.animations) {
        if (!sequence.id.empty()) Require(animationIds.insert(sequence.id).second, "Duplicate animation asset ID.");
        ValidateAnimation(sequence, asset, false);
    }
    Require(asset.ikRig.skeletonId.empty() || asset.ikRig.skeletonId == asset.skeleton.id, "IK rig references a different skeleton.");
    for (const auto& mapping : asset.ikRig.humanoid) {
        Require(validBone(mapping.bone), "Humanoid mapping bone out of range."); Finite(mapping.confidence);
        Require(mapping.confidence >= 0 && mapping.confidence <= 1, "Mapping confidence must be between zero and one.");
    }
    for (const auto& chain : asset.ikRig.chains) {
        Require(validBone(chain.root) && validBone(chain.effector) && validBone(chain.targetBone), "IK chain bone out of range.");
        CheckVector(chain.target); Finite(chain.angleLimit); Require(chain.angleLimit >= 0 && chain.iterations <= 100000, "Invalid IK solver settings.");
        if (chain.enabled) Require(chain.root >= 0 && chain.effector >= 0 && !chain.links.empty(), "Enabled IK chain requires root, effector, and links.");
        std::unordered_set<std::uint32_t> links;
        for (const auto& link : chain.links) {
            Require(link.bone < boneCount && links.insert(link.bone).second, "Invalid or duplicate IK link bone.");
            CheckVector(link.minimum); CheckVector(link.maximum);
            if (link.limited) for (int k = 0; k < 3; ++k) Require(link.minimum[k] <= link.maximum[k], "Inverted IK angular limits.");
        }
    }
    Require(asset.rigidBodies.empty()||asset.rigidBodies.size()==asset.sourceRigidBodies,"Rigid body records differ from source count.");
    Require(asset.physicsJoints.empty()||asset.physicsJoints.size()==asset.sourceJoints,"Physics joint records differ from source count.");
    Require(asset.rigidBodies.size()<=100000&&asset.physicsJoints.size()<=100000,"Too many source physical records.");
    for(const auto& b:asset.rigidBodies){
        Require(validBone(b.bone)&&b.collisionGroup<16&&b.shape<=2&&b.mode<=2,"Invalid source rigid body reference/type.");
        CheckVector(b.dimensions);CheckVector(b.position);CheckVector(b.rotationEuler);
        Require(glm::all(glm::greaterThanEqual(b.dimensions,glm::dvec3(0))),"Negative rigid body dimensions.");
        for(double v:{b.mass,b.linearDamping,b.angularDamping,b.restitution,b.friction}){Finite(v);Require(v>=0,"Negative rigid body parameter.");}
    }
    const auto validBody=[&](std::int32_t i){return i>=-1&&(i<0||static_cast<std::uint32_t>(i)<asset.sourceRigidBodies);};
    for(const auto& j:asset.physicsJoints){
        Require(j.type<=5&&validBody(j.rigidBodyA)&&validBody(j.rigidBodyB),"Invalid source joint body/type.");
        for(const auto& v:{j.position,j.rotationEuler,j.translationMinimum,j.translationMaximum,j.rotationMinimum,j.rotationMaximum,j.translationSpring,j.rotationSpring})CheckVector(v);
        Require(glm::all(glm::lessThanEqual(j.translationMinimum,j.translationMaximum))&&glm::all(glm::lessThanEqual(j.rotationMinimum,j.rotationMaximum)),"Inverted source joint limits.");
    }
}

std::string Filename(const std::string& name, std::size_t index) {
    std::string result;
    for (const unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') result += static_cast<char>(c);
        if (result.size() >= 60) break;
    }
    if (result.empty()) result = "Sequence";
    return std::to_string(index) + "_" + result + ".anim.json";
}

} // namespace

bool ValidateSkeletalAsset(const SkeletalAssetPackage& asset, std::string* error) {
    try { Validate(asset); if (error) error->clear(); return true; }
    catch (const std::exception& exception) { if (error) *error = exception.what(); return false; }
}

bool SaveSkeletalAssetPackage(const SkeletalAssetPackage& source, const fs::path& directory, std::string* error) {
    fs::path candidate;
    try {
        Validate(source);
        const auto target = fs::absolute(directory).lexically_normal();
        Require(!directory.empty() && !target.filename().empty() && target != target.root_path(), "A package output directory is required.");
        Require(!fs::exists(target) || (fs::is_directory(target) && fs::is_empty(target)), "Output directory is not empty; choose a new package directory.");
        fs::create_directories(target.parent_path());
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 20; ++attempt) {
            const auto path = target.parent_path() / (target.filename().native() + Path(".candidate-" + std::to_string(stamp) + "-" + std::to_string(attempt)).native());
            if (fs::create_directory(path)) { candidate = fs::absolute(path).lexically_normal(); break; }
        }
        Require(!candidate.empty() && candidate.parent_path() == target.parent_path(), "Could not create an isolated package candidate.");
        // Normalize only small headers. Copying the whole package duplicates
        // every skin, sparse morph and animation while the source stays live.
        const auto packageId = source.id.empty() ? StableId("package", source.name + ":" + Utf8(source.sourcePath)) : source.id;
        const auto packageName = source.name.empty() ? std::string("SkeletalAsset") : source.name;
        auto skeleton = source.skeleton; auto rig = source.ikRig;
        if (skeleton.id.empty()) skeleton.id = StableId("skeleton", packageId);
        if (skeleton.name.empty()) skeleton.name = packageName + "_Skeleton";
        if (rig.id.empty()) rig.id = StableId("ikrig", packageId);
        if (rig.name.empty()) rig.name = packageName + "_IKRig";
        rig.skeletonId = skeleton.id;
        auto manifest = Header("SkeletalAssetPackage", packageId, packageName);
        manifest["sourcePath"] = Utf8(source.sourcePath); manifest["sourceFormat"] = source.sourceFormat;
        manifest["sourceRigidBodies"] = source.sourceRigidBodies; manifest["sourceJoints"] = source.sourceJoints;
        if(!source.rigidBodies.empty())manifest["rigidBodies"]=SourcePhysicsJson::Bodies(source.rigidBodies);
        if(!source.physicsJoints.empty())manifest["physicsJoints"]=SourcePhysicsJson::Joints(source.physicsJoints);
        if (!source.meshRegions.empty()) manifest["meshRegions"] = MeshRegionsJson(source.meshRegions);
        manifest["warnings"] = Strings(source.warnings); manifest["skeleton"] = "Skeletons/model.skeleton.json";
        manifest["skeletalMesh"] = "SkeletalMeshes/model.skmesh.json"; manifest["ikRig"] = "IKRigs/model.ikrig.json";
        manifest["animations"] = Json::Value(Json::arrayValue); manifest["assets"] = Json::Value(Json::arrayValue);
        auto reference = [&](const std::string& id, const char* type, const std::string& path) {
            Json::Value ref; ref["id"] = id; ref["assetType"] = type; ref["path"] = path; manifest["assets"].append(ref);
        };
        WriteJson(candidate / "Skeletons/model.skeleton.json", SkeletonJson(skeleton));
        WriteJson(candidate / "IKRigs/model.ikrig.json", RigJson(rig));
        Json::Value mesh;
        {
            BinaryWriter binary(candidate / "SkeletalMeshes/model.skmesh.bin");
            mesh = MeshJson(source, binary, packageId, packageName, skeleton.id); binary.Save();
            WriteJson(candidate / "SkeletalMeshes/model.skmesh.json", mesh);
        } // Only a fixed-size IO buffer was held; verify the actual disk bytes.
        reference(skeleton.id, "Skeleton", manifest["skeleton"].asString());
        reference(mesh["id"].asString(), "SkeletalMesh", manifest["skeletalMesh"].asString());
        reference(rig.id, "IKRig", manifest["ikRig"].asString());
        for (std::size_t index = 0; index < source.animations.size(); ++index) {
            const auto& sequence = source.animations[index];
            const auto sequenceId = sequence.id.empty() ? StableId("animation", packageId + ":" + sequence.name + ":" + std::to_string(index)) : sequence.id;
            const auto relative = "Animations/" + Filename(sequence.name, index);
            auto json = AnimationJson(sequence); json["id"] = sequenceId; json["skeletonId"] = skeleton.id;
            WriteJson(candidate / Path(relative), json, true); manifest["animations"].append(relative);
            reference(sequenceId, "AnimationSequence", relative);
        }
        WriteJson(candidate / "Package.asset.json", manifest);
        // Validate the actual candidate bytes before making the package visible.
        std::string verificationError; const auto verified = LoadSkeletalAssetPackage(candidate, &verificationError);
        Require(verified != nullptr, "Candidate verification failed: " + verificationError);
        const bool hadEmptyDirectory = fs::exists(target);
        if (hadEmptyDirectory) Require(fs::is_empty(target) && fs::remove(target), "Output directory changed while saving.");
        try { PublishPackageDirectory(candidate, target); }
        catch (...) { if (hadEmptyDirectory && !fs::exists(target)) fs::create_directory(target); throw; }
        candidate.clear(); if (error) error->clear(); return true;
    } catch (const std::exception& exception) {
        if (!candidate.empty()) { std::error_code cleanup; fs::remove_all(candidate, cleanup); }
        if (error) *error = exception.what(); return false;
    }
}

std::shared_ptr<SkeletalAssetPackage> LoadSkeletalAssetPackage(const fs::path& manifestOrDirectory, std::string* error) {
    try {
        const auto manifestPath = fs::is_directory(manifestOrDirectory) ? manifestOrDirectory / "Package.asset.json" : manifestOrDirectory;
        const auto directory = fs::absolute(manifestPath).parent_path(); const auto manifest = ReadJson(manifestPath);
        CheckVersion(manifest, "SkeletalAssetPackage"); auto asset = std::make_shared<SkeletalAssetPackage>();
        asset->id = manifest["id"].asString(); asset->name = manifest["name"].asString(); asset->sourcePath = Path(manifest["sourcePath"].asString());
        Require(!asset->id.empty(), "Package asset ID must not be empty.");
        asset->sourceFormat = manifest["sourceFormat"].asString(); asset->sourceRigidBodies = U32(manifest["sourceRigidBodies"], "rigid body count");
        asset->sourceJoints = U32(manifest["sourceJoints"], "joint count"); asset->warnings = ReadStrings(manifest["warnings"]);
        if(manifest.isMember("rigidBodies"))asset->rigidBodies=SourcePhysicsJson::Bodies(manifest["rigidBodies"]);
        if(manifest.isMember("physicsJoints"))asset->physicsJoints=SourcePhysicsJson::Joints(manifest["physicsJoints"]);
        asset->skeleton = ReadSkeleton(ReadJson(Child(directory, manifest["skeleton"])));
        Require(!asset->skeleton.id.empty(), "Skeleton asset ID must not be empty.");
        const auto meshPath = Child(directory, manifest["skeletalMesh"]); ReadMesh(ReadJson(meshPath), meshPath.parent_path(), *asset);
        if (manifest.isMember("meshRegions")) asset->meshRegions = ReadMeshRegions(manifest["meshRegions"], asset->geometry->meshes.size());
        asset->ikRig = ReadRig(ReadJson(Child(directory, manifest["ikRig"])));
        Require(!asset->ikRig.id.empty(), "IK rig asset ID must not be empty.");
        Array(manifest["animations"], "animations"); Require(manifest["animations"].size() <= 10000, "Too many animation asset references.");
        std::unordered_set<std::string> animationPaths, animationIds;
        for (const auto& reference : manifest["animations"]) {
            const auto animationPath = Child(directory, reference);
            Require(animationPaths.insert(ReferenceKey(animationPath)).second, "Duplicate animation asset reference.");
            auto sequence = ReadAnimation(ReadJson(animationPath, MaxAnimationJsonBytes));
            Require(!sequence.id.empty() && animationIds.insert(sequence.id).second, "Missing or duplicate animation asset ID.");
            asset->animations.push_back(std::move(sequence));
        }
        Validate(*asset); if (error) error->clear(); return asset;
    } catch (const std::exception& exception) { if (error) *error = exception.what(); return nullptr; }
}

bool SaveAnimationSequenceAsset(const AnimationSequenceAsset& sequence, const SkeletalAssetPackage& target,
    const fs::path& path, std::string* error) {
    fs::path candidateDirectory;
    try {
        // Animation export intentionally validates only its target layout and
        // channels. Meshes and decoded texture pixels are neither copied nor scanned.
        ValidateAnimation(sequence, target, true);
        const auto destination = fs::absolute(path).lexically_normal();
        Require(!path.empty() && !destination.filename().empty() && destination != destination.root_path(), "An animation output filename is required.");
        const bool hadEmptyFile = fs::exists(destination);
        if (hadEmptyFile) {
            CheckRegularPath(destination);
            Require(fs::is_regular_file(destination) && fs::file_size(destination) == 0,
                "Animation output already contains data; choose a new filename.");
        }
        fs::create_directories(destination.parent_path()); CheckRegularPath(destination.parent_path());
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 20; ++attempt) {
            const auto directory = destination.parent_path() / Path(".animation-candidate-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(directory)) { candidateDirectory = directory; break; }
        }
        Require(!candidateDirectory.empty() && candidateDirectory.parent_path() == destination.parent_path(), "Could not create an isolated animation candidate.");
        auto json = AnimationJson(sequence); json["targetLayoutHash"] = AnimationLayoutHash(target);
        const auto candidate = candidateDirectory / "Sequence.anim.json"; WriteJson(candidate, json, true);
        std::string verificationError;
        const auto verified = LoadAnimationSequenceAsset(candidate, target, &verificationError);
        Require(verified.has_value(), "Animation candidate verification failed: " + verificationError);
        if (hadEmptyFile) Require(fs::is_regular_file(destination) && fs::file_size(destination) == 0 && fs::remove(destination), "Animation destination changed while saving.");
        try {
#ifdef _WIN32
            Require(MoveFileW(candidate.c_str(), destination.c_str()) != 0, "Animation publish failed; the output may already exist or be inaccessible.");
#else
            // Hard-link publication fails atomically if another writer created
            // the destination, unlike POSIX rename which would overwrite it.
            fs::create_hard_link(candidate, destination); fs::remove(candidate);
#endif
        } catch (...) {
            if (hadEmptyFile && !fs::exists(destination)) { std::ofstream restore(destination, std::ios::binary); }
            throw;
        }
        fs::remove(candidateDirectory); candidateDirectory.clear(); if (error) error->clear(); return true;
    } catch (const std::exception& exception) {
        if (!candidateDirectory.empty()) { std::error_code cleanup; fs::remove_all(candidateDirectory, cleanup); }
        if (error) *error = exception.what(); return false;
    }
}

std::optional<AnimationSequenceAsset> LoadAnimationSequenceAsset(const fs::path& path,
    const SkeletalAssetPackage& target, std::string* error) {
    try {
        const auto json = ReadJson(path, MaxAnimationJsonBytes);
        auto sequence = ReadAnimation(json); ValidateAnimation(sequence, target, true);
        if (json.isMember("targetLayoutHash")) Require(json["targetLayoutHash"].isString()
            && json["targetLayoutHash"].asString() == AnimationLayoutHash(target),
            "Animation target layout differs (bone hierarchy/names or morph order changed); rebake for this target.");
        if (error) error->clear(); return sequence;
    } catch (const std::exception& exception) { if (error) *error = exception.what(); return std::nullopt; }
}

} // namespace Model
