#include "Render/Public/Pipeline/mesh_distance_field.h"
#include <iostream>
#include <stdexcept>
using namespace Render;
void Check(bool x, const char *s) {
    if (!x)
        throw std::runtime_error(s);
}
int main() {
    try {
        std::array<ProbeTriangle, 2> wall{
            {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}}, {{-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}}};
        auto scene = BuildRealtimeGiScene(wall);
        auto field = BuildMeshDistanceField(*scene, 32);
        for (int i = 0; i < 1000; ++i) {
            glm::vec3 p{float(i % 37) / 37 - 0.5f, float(i % 47) / 47 - 0.5f,
                        float(i % 53) / 53 * .12f - .06f};
            float d = field->ConservativeDistance(p);
            Check(d >= 0 && d <= std::abs(p.z) + 1e-5, "Distance bound crossed a zero-thickness wall");
        }
        std::array<ProbeTriangle, 4> tetra{{{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}},
                                            {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}},
                                            {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                                            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}};
        auto solid = BuildMeshDistanceField(*BuildRealtimeGiScene(tetra), 32, true);
        auto cell = glm::uvec3(glm::round((glm::vec3(.2f) - solid->Origin()) / solid->Spacing()));
        Check(solid->Sample(cell) < -.1, "Closed mesh interior sign incorrect");
        cell = glm::uvec3(glm::round((glm::vec3(.8f) - solid->Origin()) / solid->Spacing()));
        Check(solid->Sample(cell) > .1, "Closed mesh exterior sign incorrect");
        auto empty = BuildMeshDistanceField(*BuildRealtimeGiScene({}));
        Check(empty->Width() == 1024 && empty->Height() == 1 && empty->ConservativeDistance({0, 0, 0}) == 0,
              "Empty distance field");
        bool rejected = false;
        try {
            BuildMeshDistanceField(*scene, 1);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        Check(rejected, "Invalid field resolution accepted");
        std::cout << "Distance field tests passed: conservative thin wall bounds, signed closed mesh, "
                     "empty/budget\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
