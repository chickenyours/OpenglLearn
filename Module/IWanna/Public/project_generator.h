#pragma once
#include <filesystem>

namespace IWanna {
// Creates a self-contained content project. The destination must not exist.
std::filesystem::path CreateBlankProject(const std::filesystem::path& destination,
                                         const std::filesystem::path& templateAssets,
                                         const std::filesystem::path& runtimeDirectory);
}
