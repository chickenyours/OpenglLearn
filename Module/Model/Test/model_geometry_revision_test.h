#pragma once
#include "Model/Public/model_module.h"
#include <iostream>
#include <stdexcept>

namespace Model::TestData {
// Uses an existing started module/device, but owns a separate two-triangle
// resource. No mutation of the editor's character, geometry or materials.
inline void RunModelGeometryRevisionGpuOracle(ModelModule& module){
    const auto check=[](bool okay,const char* message){if(!okay)throw std::runtime_error(message);};
    check(module.IsStarted(),"Geometry revision audit requires a started Model module");
    auto source=std::make_shared<StaticModel>();source->materials.emplace_back();
    for(unsigned i=0;i<2;++i){
        StaticMesh mesh;mesh.name="Revision triangle "+std::to_string(i);mesh.hasSecondUV=true;
        for(const glm::dvec3 p:std::array<glm::dvec3,3>{{{0,0,0},{1,0,0},{0,1,0}}}){
            StaticVertex vertex;vertex.position=p+glm::dvec3(double(i)*2,0,0);vertex.normal={0,0,1};
            mesh.vertices.push_back(vertex);mesh.bounds.Include(vertex.position);source->bounds.Include(vertex.position);
        }
        mesh.indices={0,1,2};source->meshes.push_back(std::move(mesh));source->instances.push_back({"Revision node",i,glm::dmat4(1)});
    }
    const auto resource=module.Upload(source,{true});
    if(!resource)throw std::runtime_error("Geometry revision fixture upload failed: "+module.LastError());
    const auto initial=resource->GeometryRevision();check(initial>0,"Initial geometry revision must be nonzero");
    StaticModel deformed=*source;std::string error;
    const auto update=[&]{if(!resource->UpdateGeometry(deformed,&error))throw std::runtime_error("Geometry revision fixture update failed: "+error);};
    Render::RenderFrame before;check(resource->AppendTo(before),"Append geometry revision fixture before updates");
    update();check(resource->GeometryRevision()==initial,"Identical successful vertex upload changed geometry revision");
    deformed.meshes.back().vertices.back().position.z=.03125;update();
    check(resource->GeometryRevision()==initial+1,"Changed final-mesh position did not advance geometry revision once");
    update();check(resource->GeometryRevision()==initial+1,"Repeated deformed pose advanced geometry revision");
    deformed.meshes.front().vertices.front().uv.x=.25f;update();
    check(resource->GeometryRevision()==initial+2,"Changed uploaded UV did not advance geometry revision");
    deformed.meshes.back().vertices[1].uv1.y=.5f;update();
    check(resource->GeometryRevision()==initial+3,"Changed opt-in UV1 did not advance geometry revision");
    // After rebasing, this double-position change rounds to the identical GPU
    // float. Raw source revisions or hashes would spuriously invalidate caches.
    deformed.meshes.front().vertices.front().position.x=1e-12;update();
    check(resource->GeometryRevision()==initial+3,"Sub-float source change invalidated identical uploaded packed geometry");
    auto invalid=deformed;invalid.meshes.front().vertices.front().position.z=.0625;
    invalid.meshes.back().indices[0]=2;
    check(!resource->UpdateGeometry(invalid,&error)&&!error.empty(),"Invalid final-mesh topology was accepted");
    check(resource->GeometryRevision()==initial+3,"Pre-upload validation failure advanced geometry revision");
    update();check(resource->GeometryRevision()==initial+3,"Rejected batch replaced the last successful packed state");
    Render::RenderFrame after;check(resource->AppendTo(after),"Append geometry revision fixture after updates");
    check(before.items.size()==2&&after.items.size()==2,"Revision fixture scene instances changed");
    for(unsigned i=0;i<2;++i)check(before.items[i].mesh==after.items[i].mesh,"Geometry revision tracking replaced an in-place mesh handle");
    std::cout<<"Model geometry revision: initial/identical/position/UV/UV1/float-packing/validation and stable mesh handles passed\n";
}
} // namespace Model::TestData
