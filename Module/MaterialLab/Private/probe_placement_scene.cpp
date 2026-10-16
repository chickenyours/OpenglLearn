#include "MaterialLab/Public/probe_placement_scene.h"
#include "MaterialLab/Public/lab_geometry.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <stdexcept>

namespace MaterialLab {
const std::vector<ProbePlacementObject>& ProbePlacementObjects() {
    static const std::vector<ProbePlacementObject> objects{
        {"Flat floor", ShowcaseMesh::Plane, ProbePlacementMaterial::Floor, {0,0,0}, {5,1,5}},
        {"Flat back wall", ShowcaseMesh::Plane, ProbePlacementMaterial::Wall, {0,2.5f,-3.5f}, {5,1,2.5f}, 0, glm::radians(90.f)},
        {"Moving foreground block", ShowcaseMesh::Box, ProbePlacementMaterial::Block, {-1.55f,1.05f,-.4f}, {.9f,1.05f,.7f}, .18f, 0, {1,0,0}, true},
        {"Thin post", ShowcaseMesh::Box, ProbePlacementMaterial::Thin, {-.1f,1.15f,.5f}, {.035f,1.15f,.09f}},
        {"Curved sphere", ShowcaseMesh::Sphere, ProbePlacementMaterial::Sphere, {1.85f,.9f,-.6f}, {.9f,.9f,.9f}},
        {"Tilted panel", ShowcaseMesh::Box, ProbePlacementMaterial::Block, {.25f,.9f,-2.25f}, {.65f,.9f,.08f}, -.25f, -.18f, {0,0,1}},
        {"Step 1", ShowcaseMesh::Box, ProbePlacementMaterial::Block, {-2.5f,.15f,1.65f}, {.7f,.15f,.3f}},
        {"Step 2", ShowcaseMesh::Box, ProbePlacementMaterial::Block, {-2.5f,.30f,1.05f}, {.7f,.30f,.3f}},
        {"Step 3", ShowcaseMesh::Box, ProbePlacementMaterial::Block, {-2.5f,.45f,.45f}, {.7f,.45f,.3f}}
    };
    return objects;
}
const std::vector<ProbePlacementObject>& ProbeSdfObjects() {
    static const auto objects=[] {
        auto result=ProbePlacementObjects();
        result.push_back({"Left offscreen wall",ShowcaseMesh::Plane,ProbePlacementMaterial::Wall,{-5,2.5f,0},{2.5f,1,5},0,-glm::half_pi<float>(),{0,0,1}});
        result.push_back({"Right offscreen wall",ShowcaseMesh::Plane,ProbePlacementMaterial::Wall,{5,2.5f,0},{2.5f,1,5},0,glm::half_pi<float>(),{0,0,1}});
        result.push_back({"Overhead receiver",ShowcaseMesh::Plane,ProbePlacementMaterial::Wall,{0,5,0},{5,1,5},0,glm::pi<float>(),{1,0,0}});
        return result;
    }();return objects;
}
glm::mat4 ProbePlacementTransform(const ProbePlacementObject& object, const ProbePlacementSceneSettings& state) {
    auto position = object.position;
    if (object.movable) position.x += state.occluderOffset;
    return glm::translate(glm::mat4(1), position) *
        glm::rotate(glm::mat4(1), object.yaw, glm::vec3(0,1,0)) *
        glm::rotate(glm::mat4(1), object.tilt, object.tiltAxis) * glm::scale(glm::mat4(1), object.scale);
}
Render::PipelineCamera ProbePlacementCameraPreset(unsigned preset, float aspect) {
    if (!std::isfinite(aspect) || aspect <= 0) throw std::invalid_argument("Probe placement camera requires a positive aspect");
    const std::array<glm::vec3,3> eyes{{{0,2.6f,7}, {3.6f,2.3f,4.8f}, {-.2f,1.6f,3.3f}}};
    const std::array<glm::vec3,3> targets{{{0,1.2f,-.6f}, {0,1,-.4f}, {.1f,1,-1.2f}}};
    Render::PipelineCamera camera;
    camera.position=eyes[preset%3]; camera.nearPlane=.05f; camera.farPlane=40;
    camera.view=glm::lookAt(camera.position,targets[preset%3],glm::vec3(0,1,0));
    camera.projection=glm::perspective(glm::radians(55.f),aspect,camera.nearPlane,camera.farPlane);
    return camera;
}
static std::vector<Render::ProbeTriangle> PlacementTriangles(const ProbePlacementSceneSettings& state,const std::vector<ProbePlacementObject>& objects) {
    std::vector<Render::ProbeTriangle> triangles;
    const auto plane=MakePlane(), box=MakeBox(), sphere=MakeSphere(48,64);
    for (const auto& object : objects) {
        const auto& mesh=object.mesh==ShowcaseMesh::Sphere?sphere:object.mesh==ShowcaseMesh::Plane?plane:box;
        const auto model=ProbePlacementTransform(object,state);
        for (std::size_t i=0;i+2<mesh.indices.size();i+=3) {
            const auto a=glm::vec3(model*glm::vec4(mesh.vertices[mesh.indices[i]].position,1));
            const auto b=glm::vec3(model*glm::vec4(mesh.vertices[mesh.indices[i+1]].position,1));
            const auto c=glm::vec3(model*glm::vec4(mesh.vertices[mesh.indices[i+2]].position,1));
            if (glm::length(glm::cross(b-a,c-a))>1e-8f) triangles.push_back({a,b,c,glm::vec3(.5f)});
        }
    }
    return triangles;
}
std::vector<Render::ProbeTriangle> ProbePlacementTriangles(const ProbePlacementSceneSettings& state) {return PlacementTriangles(state,ProbePlacementObjects());}
std::vector<Render::ProbeTriangle> ProbeSdfTriangles(const ProbePlacementSceneSettings& state) {return PlacementTriangles(state,ProbeSdfObjects());}
}
