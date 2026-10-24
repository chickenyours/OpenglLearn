#pragma once

namespace Render {
// Only materials explicitly tagged eyeShellEnabled use this postprocess.
// Disabling it leaves their underlying eye/HDR image unchanged; tagged shells
// remain excluded from ordinary translucency so an A/B does not add legacy glass.
struct EyeShellSettings {
    bool enabled = true;
};
} // namespace Render
