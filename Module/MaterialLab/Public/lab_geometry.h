#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

namespace MaterialLab {

// Shader locations: position 0, normal 1, UV 2, tangent 3.
// bitangent = cross(normal, tangent.xyz) * tangent.w.
struct PbrVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};
static_assert(std::is_standard_layout_v<PbrVertex>);
static_assert(sizeof(PbrVertex) == 48);
static_assert(offsetof(PbrVertex, normal) == 12);
static_assert(offsetof(PbrVertex, uv) == 24);
static_assert(offsetof(PbrVertex, tangent) == 32);

struct MeshData {
    std::vector<PbrVertex> vertices;
    std::vector<std::uint32_t> indices;
};

namespace Detail {
inline constexpr float Pi = 3.14159265358979323846f;
inline std::uint8_t Channel(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}
}

// Unit sphere with +Y poles. Increasing U circles +X toward +Z; increasing V
// runs from the north pole to the south pole. Duplicate the seam and pole
// vertices for UV/tangent continuity, but omit both sets of zero-area pole faces.
inline MeshData MakeSphere(std::uint32_t rows = 48, std::uint32_t segments = 64) {
    if (rows < 2 || segments < 3)
        throw std::invalid_argument("Sphere requires at least 2 rows and 3 segments");
    const std::uint64_t columns = std::uint64_t(segments) + 1;
    const std::uint64_t vertexRows = std::uint64_t(rows) + 1;
    const auto maxCount = std::numeric_limits<std::uint32_t>::max();
    if (vertexRows > maxCount / columns)
        throw std::length_error("Sphere vertex count exceeds the 32-bit mesh limit");
    const std::uint64_t indexCount = std::uint64_t(segments) * (rows - 1) * 6;
    if (indexCount > maxCount)
        throw std::length_error("Sphere index count exceeds the 32-bit mesh limit");

    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(vertexRows * columns));
    mesh.indices.reserve(static_cast<std::size_t>(indexCount));
    for (std::uint32_t row = 0; row <= rows; ++row) {
        const float v = float(row) / float(rows);
        const float theta = Detail::Pi * v;
        // Exact poles avoid seams or near-degenerate triangles from sin(pi).
        const float radius = row == 0 || row == rows ? 0.0f : std::sin(theta);
        const float y = row == 0 ? 1.0f : row == rows ? -1.0f : std::cos(theta);
        for (std::uint32_t column = 0; column <= segments; ++column) {
            const float u = float(column) / float(segments);
            // Use identical arithmetic for both copies of the U seam.
            const float phi = column == segments ? 0.0f : 2.0f * Detail::Pi * u;
            const float cosine = std::cos(phi), sine = std::sin(phi);
            const glm::vec3 normal(radius * cosine, y, radius * sine);
            mesh.vertices.push_back({normal, normal, {u, v}, {-sine, 0.0f, cosine, 1.0f}});
        }
    }
    const auto stride = static_cast<std::uint32_t>(columns);
    for (std::uint32_t row = 0; row < rows; ++row) {
        for (std::uint32_t column = 0; column < segments; ++column) {
            const std::uint32_t a = row * stride + column;
            const std::uint32_t b = a + 1, c = a + stride, d = c + 1;
            if (row != 0) mesh.indices.insert(mesh.indices.end(), {a, b, c});
            if (row + 1 != rows) mesh.indices.insert(mesh.indices.end(), {b, d, c});
        }
    }
    return mesh;
}

// A 2 x 2 XZ plane facing +Y. U grows along +X, V along -Z, so handedness is +1.
inline MeshData MakePlane() {
    return {{
        {{-1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}
    }, {0, 1, 2, 0, 2, 3}};
}

// Unit box spanning [-1,1], separate face vertices preserve hard normals.
inline MeshData MakeBox() {
    MeshData mesh;
    const std::array<glm::vec3, 6> normals{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    for (const auto& n : normals) {
        const auto u = glm::normalize(glm::cross(std::abs(n.y) > 0.5f ? glm::vec3(0,0,1) : glm::vec3(0,1,0), n));
        const auto v = glm::cross(n, u);
        const auto start = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({n-u-v, n, {0,0}, glm::vec4(u,1)});
        mesh.vertices.push_back({n+u-v, n, {1,0}, glm::vec4(u,1)});
        mesh.vertices.push_back({n+u+v, n, {1,1}, glm::vec4(u,1)});
        mesh.vertices.push_back({n-u+v, n, {0,1}, glm::vec4(u,1)});
        mesh.indices.insert(mesh.indices.end(), {start,start+1,start+2,start,start+2,start+3});
    }
    return mesh;
}

struct TexturePixels {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

enum class TexturePattern : std::uint8_t {
    White, FlatNormal, CopperAlbedo, CheckerAlbedo, BumpedNormal,
    Metallic, Roughness, AmbientOcclusion, WaterNormal, WaterFlow
};

// Color patterns contain display/sRGB values; normal and scalar maps are linear
// data. Scalar maps replicate the value into RGB; all patterns have opaque alpha.
// Every pattern is deterministic and periodic, suitable for repeat sampling.
inline TexturePixels MakeTexture(TexturePattern pattern, std::uint32_t size = 128) {
    if (size == 0 || size > 4096)
        throw std::invalid_argument("Texture size must be between 1 and 4096");
    if (static_cast<std::uint8_t>(pattern) > static_cast<std::uint8_t>(TexturePattern::WaterFlow))
        throw std::invalid_argument("Unknown material lab texture pattern");
    TexturePixels texture{size, size, std::vector<std::uint8_t>(std::size_t(size) * size * 4)};
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            const float a = 2.0f * Detail::Pi * u, b = 2.0f * Detail::Pi * v;
            const float grain = std::sin(a * 19.0f + 0.7f * std::sin(b * 3.0f));
            const float wave = std::sin(a * 4.0f) * std::sin(b * 4.0f);
            glm::vec3 color(1.0f);
            switch (pattern) {
            case TexturePattern::White: break;
            case TexturePattern::FlatNormal: color = {0.5f, 0.5f, 1.0f}; break;
            case TexturePattern::CopperAlbedo: {
                const float variation = 0.92f + 0.055f * grain + 0.025f * wave;
                color = glm::vec3(0.90f, 0.52f, 0.29f) * variation;
                break;
            }
            case TexturePattern::CheckerAlbedo: {
                const bool light = ((x * 8 / size) + (y * 8 / size)) % 2 == 0;
                color = light ? glm::vec3(0.80f, 0.84f, 0.87f) : glm::vec3(0.16f, 0.21f, 0.26f);
                break;
            }
            case TexturePattern::BumpedNormal: {
                // Derivatives of a periodic low-amplitude height field in UV.
                const float du = 0.007f * 8.0f * Detail::Pi * std::cos(a * 4.0f) * std::sin(b * 4.0f)
                               + 0.002f * 24.0f * Detail::Pi * std::cos(a * 12.0f) * std::sin(b * 12.0f);
                const float dv = 0.007f * 8.0f * Detail::Pi * std::sin(a * 4.0f) * std::cos(b * 4.0f)
                               + 0.002f * 24.0f * Detail::Pi * std::sin(a * 12.0f) * std::cos(b * 12.0f);
                color = glm::normalize(glm::vec3(-du, -dv, 1.0f)) * 0.5f + 0.5f;
                break;
            }
            case TexturePattern::WaterNormal: {
                float du=.07f*std::cos(a*2+b)+.025f*std::cos(a-b*2);
                float dv=.035f*std::cos(a*2+b)-.05f*std::cos(a-b*2);
                color=glm::normalize(glm::vec3(-du,-dv,1.f))*.5f+.5f;break;
            }
            case TexturePattern::WaterFlow:
                color={.5f+.18f*std::sin(b),.5f+.12f*std::cos(a),.5f};break;
            case TexturePattern::Metallic: color = glm::vec3(1.0f); break;
            case TexturePattern::Roughness: color = glm::vec3(0.38f + 0.12f * wave + 0.025f * grain); break;
            case TexturePattern::AmbientOcclusion: color = glm::vec3(0.86f + 0.14f * (wave * 0.5f + 0.5f)); break;
            }
            const std::size_t pixel = (std::size_t(y) * size + x) * 4;
            texture.rgba[pixel] = Detail::Channel(color.r);
            texture.rgba[pixel + 1] = Detail::Channel(color.g);
            texture.rgba[pixel + 2] = Detail::Channel(color.b);
            texture.rgba[pixel + 3] = 255;
        }
    }
    return texture;
}

} // namespace MaterialLab
