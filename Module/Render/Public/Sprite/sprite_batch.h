#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>
#include "object_ptr.h"

namespace Render {
class RHIDevice;
class RHIFrameEncoder;
class RHIFrameCommandBuffer;

struct SpriteImage {
    std::string name;
    std::filesystem::path path;
    int columns = 1;
    int rows = 1;
};

// One atlas and one painter-ordered mesh per frame. Call on the main thread,
// including RHIDevice::returnSystem callbacks. The device must outlive this batch.
// Workflow: Initialize -> wait Ready -> Begin -> Sprite/Rect/Text -> Flush into
// an active frame encoder -> encoder.End -> Submit. Wait Ready before reusing.
class SpriteBatch2D {
public:
    explicit SpriteBatch2D(ObjectWeakPtr<RHIDevice> device);
    ~SpriteBatch2D();
    SpriteBatch2D(const SpriteBatch2D&) = delete;
    SpriteBatch2D& operator=(const SpriteBatch2D&) = delete;

    // Decodes PNGs (including Unicode paths) and enqueues GPU initialization.
    // Cells are equal sized, indexed left-to-right then top-to-bottom.
    bool Initialize(const std::vector<SpriteImage>& images);
    bool Ready() const;
    bool Begin(float halfWidth, float halfHeight, glm::vec2 cameraCenter = {});
    bool Sprite(std::string_view name, glm::vec2 center, glm::vec2 size,
        float radians = 0, glm::vec4 tint = {1, 1, 1, 1},
        bool flipX = false, bool flipY = false, int frame = 0);
    // axisX/axisY are the complete world-space width/height vectors. They may
    // include shear and reflection (for example a rotated child of a squashed
    // parent). UV flips do not change this geometry. Singular bases are invalid.
    bool SpriteAffine(std::string_view name, glm::vec2 center, glm::vec2 axisX, glm::vec2 axisY,
        glm::vec4 tint = {1, 1, 1, 1}, bool flipX = false, bool flipY = false, int frame = 0);
    // Region is relative to the selected image frame: normalized top-left u/v
    // and positive width/height, entirely inside [0,1]. Atlas edge extrusion
    // remains intact; subregions never reach a neighboring packed image.
    bool SpriteRegion(std::string_view name, glm::vec2 center, glm::vec2 size,
        glm::vec4 normalizedRegion, float radians = 0, glm::vec4 tint = {1, 1, 1, 1},
        bool flipX = false, bool flipY = false, int frame = 0);
    void Rect(glm::vec2 center, glm::vec2 size, glm::vec4 tint, float radians = 0);
    // Original 5x7 bitmap ASCII font; lowercase maps to uppercase. pixel is one
    // font pixel in world units. topLeft uses y-up coordinates; newlines descend.
    void Text(std::string_view text, glm::vec2 topLeft, float pixel, glm::vec4 tint);
    bool Flush(RHIFrameEncoder& encoder);
    // Completion always runs once for accepted submissions, including failures.
    // False means no submission was accepted; caller must cancel its encoder.
    bool Submit(ObjectWeakPtr<RHIFrameCommandBuffer> frame, std::function<void()> completed = {});
    const std::string& Error() const;
    void Shutdown();

private:
    struct State;
    ObjectWeakPtr<RHIDevice> device_;
    std::shared_ptr<State> state_;
    void Quad(glm::vec2 center, glm::vec2 size, float radians, glm::vec4 region,
        glm::vec4 tint, bool flipX = false, bool flipY = false, bool nearest = false);
    bool AffineQuad(glm::vec2 center, glm::vec2 axisX, glm::vec2 axisY, glm::vec4 region,
        glm::vec4 tint, bool flipX = false, bool flipY = false, bool nearest = false);
};

} // namespace Render
