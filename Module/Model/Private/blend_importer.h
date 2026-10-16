#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Model {

// Internal interchange bridge. Conversion is performed in a fresh Blender
// background process, never in the editor and never by executing a shell.
struct BlendImportOptions {
    std::filesystem::path blenderExecutable;
    std::filesystem::path cacheDirectory;
    std::filesystem::path scriptPath;
    unsigned timeoutSeconds = 180;
    // StaticModelLoader needs the authored visible pose, including default
    // morphs, baked into ordinary geometry. Skeletal import keeps this false.
    bool bakeStaticPose = false;
};

struct PreparedBlendSource {
    std::filesystem::path sourcePath;
    std::filesystem::path glbPath;
    std::filesystem::path metadataPath;
    std::filesystem::path logPath;
    std::vector<std::string> warnings;
};

// Each conversion creates a new cache revision: linked libraries and external
// images can change independently of the .blend, so stale conversions are not
// reused. Source files are opened read-only and are never saved or rewritten.
std::optional<PreparedBlendSource> PrepareBlendSource(const std::filesystem::path& source,
    const BlendImportOptions& options = {}, std::string* error = nullptr);

} // namespace Model
