#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "MaterialLab/Public/lab_geometry.h"

namespace {
using namespace MaterialLab;

bool Near(float a, float b, float tolerance = 1e-5f) {
    return std::abs(a - b) <= tolerance;
}

void CheckVertexFrames(const MeshData& mesh) {
    for (const auto& vertex : mesh.vertices) {
        const glm::vec3 tangent(vertex.tangent);
        for (int component = 0; component < 3; ++component) {
            assert(std::isfinite(vertex.position[component]));
            assert(std::isfinite(vertex.normal[component]));
            assert(std::isfinite(tangent[component]));
        }
        assert(Near(glm::length(vertex.normal), 1.0f));
        assert(Near(glm::length(tangent), 1.0f));
        assert(Near(glm::dot(vertex.normal, tangent), 0.0f));
        assert(vertex.tangent.w == 1.0f || vertex.tangent.w == -1.0f);
        assert(std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y));
        assert(vertex.uv.x >= 0.0f && vertex.uv.x <= 1.0f);
        assert(vertex.uv.y >= 0.0f && vertex.uv.y <= 1.0f);
    }
}

void CheckTriangles(const MeshData& mesh, bool sphere) {
    assert(!mesh.indices.empty() && mesh.indices.size() % 3 == 0);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (std::size_t offset = 0; offset < 3; ++offset)
            assert(mesh.indices[i + offset] < mesh.vertices.size());
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 ab = b.position - a.position, ac = c.position - a.position;
        const auto cross = glm::cross(ab, ac);
        assert(glm::length(cross) > 1e-8f); // Includes both pole fans.
        const glm::vec3 outward = sphere ? a.position + b.position + c.position : glm::vec3(0, 1, 0);
        assert(glm::dot(cross, outward) > 0.0f);

        const glm::vec2 uvAB = b.uv - a.uv, uvAC = c.uv - a.uv;
        const float determinant = uvAB.x * uvAC.y - uvAB.y * uvAC.x;
        assert(determinant > 0.0f);
        const glm::vec3 uvTangent = (ab * uvAC.y - ac * uvAB.y) / determinant;
        const glm::vec3 uvBitangent = (ac * uvAB.x - ab * uvAC.x) / determinant;
        const glm::vec3 tangent = glm::vec3(a.tangent) + glm::vec3(b.tangent) + glm::vec3(c.tangent);
        const glm::vec3 bitangent =
            glm::cross(a.normal, glm::vec3(a.tangent)) * a.tangent.w +
            glm::cross(b.normal, glm::vec3(b.tangent)) * b.tangent.w +
            glm::cross(c.normal, glm::vec3(c.tangent)) * c.tangent.w;
        assert(glm::dot(tangent, uvTangent) > 0.0f);
        assert(glm::dot(bitangent, uvBitangent) > 0.0f);
    }
}

void TestSphere(std::uint32_t rows, std::uint32_t segments) {
    const auto sphere = MakeSphere(rows, segments);
    assert(sphere.vertices.size() == std::size_t(rows + 1) * (segments + 1));
    assert(sphere.indices.size() == std::size_t(segments) * (rows - 1) * 6);
    CheckVertexFrames(sphere);
    CheckTriangles(sphere, true);
    for (std::uint32_t row = 0; row <= rows; ++row) {
        const auto& left = sphere.vertices[std::size_t(row) * (segments + 1)];
        const auto& right = sphere.vertices[std::size_t(row) * (segments + 1) + segments];
        assert(left.position == right.position);
        assert(left.normal == right.normal);
        assert(left.tangent == right.tangent);
        assert(left.uv.x == 0.0f && right.uv.x == 1.0f && left.uv.y == right.uv.y);
    }
    for (const auto& vertex : sphere.vertices)
        assert(Near(glm::length(vertex.position), 1.0f));
}

void TestTextures() {
    for (const auto pattern : {
        TexturePattern::White, TexturePattern::FlatNormal, TexturePattern::CopperAlbedo,
        TexturePattern::CheckerAlbedo, TexturePattern::BumpedNormal, TexturePattern::Metallic,
        TexturePattern::Roughness, TexturePattern::AmbientOcclusion
    }) {
        const auto texture = MakeTexture(pattern, 32);
        assert(texture.width == 32 && texture.height == 32);
        assert(texture.rgba.size() == 32 * 32 * 4);
        assert(texture.rgba == MakeTexture(pattern, 32).rgba);
        for (std::size_t i = 0; i < texture.rgba.size(); i += 4) {
            assert(texture.rgba[i + 3] == 255);
            if (pattern == TexturePattern::FlatNormal) {
                assert(texture.rgba[i] == 128 && texture.rgba[i + 1] == 128 && texture.rgba[i + 2] == 255);
            } else if (pattern == TexturePattern::White || pattern == TexturePattern::Metallic) {
                assert(texture.rgba[i] == 255 && texture.rgba[i + 1] == 255 && texture.rgba[i + 2] == 255);
            } else if (pattern == TexturePattern::BumpedNormal) {
                const glm::vec3 normal = glm::vec3(texture.rgba[i], texture.rgba[i + 1], texture.rgba[i + 2])
                                      * (2.0f / 255.0f) - 1.0f;
                assert(Near(glm::length(normal), 1.0f, 0.014f));
                assert(normal.z > 0.0f);
            } else if (pattern == TexturePattern::Roughness || pattern == TexturePattern::AmbientOcclusion) {
                assert(texture.rgba[i] == texture.rgba[i + 1] && texture.rgba[i] == texture.rgba[i + 2]);
                assert(texture.rgba[i] > 0);
            }
        }
        if (pattern == TexturePattern::CheckerAlbedo || pattern == TexturePattern::Roughness) {
            bool varies = false;
            for (std::size_t i = 4; i < texture.rgba.size(); i += 4)
                varies = varies || texture.rgba[i] != texture.rgba[0];
            assert(varies);
        }
    }
    assert(MakeTexture(TexturePattern::White, 1).rgba.size() == 4);
}

template <typename Operation>
void ExpectInvalid(Operation operation) {
    bool threw = false;
    try { operation(); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}
}

int main() {
    TestSphere(48, 64);
    TestSphere(7, 11);
    TestSphere(2, 3);
    const auto plane = MaterialLab::MakePlane();
    assert(plane.vertices.size() == 4 && plane.indices.size() == 6);
    CheckVertexFrames(plane);
    CheckTriangles(plane, false);
    const auto box = MaterialLab::MakeBox();
    assert(box.vertices.size() == 24 && box.indices.size() == 36);
    CheckVertexFrames(box);
    CheckTriangles(box, true); // outward from center also holds for a convex box
    TestTextures();
    ExpectInvalid([] { MaterialLab::MakeSphere(1, 8); });
    ExpectInvalid([] { MaterialLab::MakeSphere(8, 2); });
    ExpectInvalid([] { MaterialLab::MakeTexture(MaterialLab::TexturePattern::White, 0); });
    ExpectInvalid([] { MaterialLab::MakeTexture(MaterialLab::TexturePattern::White, 4097); });
    ExpectInvalid([] { MaterialLab::MakeTexture(static_cast<MaterialLab::TexturePattern>(255)); });
    bool oversized = false;
    try { MaterialLab::MakeSphere(std::numeric_limits<std::uint32_t>::max(), 3); }
    catch (const std::length_error&) { oversized = true; }
    assert(oversized);
    std::cout << "MaterialLab geometry and procedural textures passed.\n";
}
