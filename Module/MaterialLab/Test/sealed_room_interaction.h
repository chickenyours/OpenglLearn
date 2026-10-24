#pragma once

// Shared by the interactive H animation and its all-object motion fixture.
// Stable scene indices keep raster, Surface Cache and SDF instance ownership
// aligned; moving a prop only changes its instance transform.
namespace SealedRoomMotion {
inline constexpr unsigned FirstProp=MaterialLab::SealedGiRoomLampIndex+1;
inline bool IsProp(unsigned index) {
    return index>=FirstProp&&index<MaterialLab::SealedGiRoomObjects().size();
}
inline glm::vec3 Offset(unsigned objectId,double seconds) {
    if(!IsProp(objectId))return glm::vec3(0);
    const auto prop=objectId-FirstProp;
    const double phase=double(prop)*1.0471975511965976;
    const double speed=.47+.04*double(prop);
    return {float(.30*(std::sin(seconds*speed+phase)-std::sin(phase))),0,
        float(.22*(std::sin(seconds*(speed*.83)+phase*.71)-std::sin(phase*.71)))};
}
inline void Advance(std::vector<MaterialLab::ProbePlacementObject>& objects,double fromSeconds,double toSeconds) {
    for(unsigned index=FirstProp;index<objects.size();++index)
        if(IsProp(index))objects[index].position+=Offset(index,toSeconds)-Offset(index,fromSeconds);
}
inline void Sync(MaterialLab::FrameSettings& frame,const std::vector<MaterialLab::ProbePlacementObject>& objects) {
    frame.sealedGiRoomPropMotion=true;
    frame.screenProbeStudyState.occluderOffset=0;
    const auto& initial=MaterialLab::SealedGiRoomObjects();
    for(unsigned index=FirstProp;index<initial.size();++index)
        frame.sealedGiRoomPropOffsets[index-FirstProp]=objects[index].position-initial[index].position;
}

inline glm::vec3 GroundDisplacement(float horizontal,float depth,float seconds,bool fast) {
    auto direction=glm::vec3(horizontal,0,depth);
    if(glm::dot(direction,direction)==0)return glm::vec3(0);
    return glm::normalize(direction)*(1.4f*seconds*(fast?3.f:1.f));
}
inline void KeepInsideRoom(MaterialLab::ProbePlacementObject& object) {
    auto shape=object;shape.position=glm::vec3(0);shape.movable=false;
    const auto transform=MaterialLab::ProbePlacementTransform(shape,{});
    // Exact support bounds for the transformed unit sphere or box.
    glm::vec3 extent(0);
    for(unsigned axis=0;axis<3;++axis) {
        const auto row=glm::vec3(transform[0][axis],transform[1][axis],transform[2][axis]);
        extent[axis]=object.mesh==MaterialLab::ShowcaseMesh::Sphere?glm::length(row):
            std::abs(row.x)+std::abs(row.y)+std::abs(row.z);
    }
    object.position.x=std::clamp(object.position.x,MaterialLab::SealedGiRoomMinimum.x+extent.x+.02f,
        MaterialLab::SealedGiRoomMaximum.x-extent.x-.02f);
    object.position.z=std::clamp(object.position.z,MaterialLab::SealedGiRoomMinimum.z+extent.z+.02f,
        MaterialLab::SealedGiRoomMaximum.z-extent.z-.02f);
}

inline constexpr unsigned NoSelection=std::numeric_limits<unsigned>::max();
// Called only on the F key's rising edge. Test the exact visible triangles,
// including static occluders, so a wall or a near prop prevents selecting one
// behind it. Local ray directions stay unnormalized to retain world distance.
inline unsigned Pick(const std::vector<MaterialLab::ProbePlacementObject>& objects,
    glm::dvec3 origin,glm::dvec3 direction,double maximum) {
    static const std::array<MaterialLab::MeshData,3> meshes{{
        MaterialLab::MakePlane(),MaterialLab::MakeBox(),MaterialLab::MakeSphere(48,64)}};
    unsigned nearest=NoSelection;
    for(unsigned index=0;index<objects.size();++index) {
        const auto& object=objects[index];
        const auto inverse=glm::inverse(glm::dmat4(MaterialLab::ProbePlacementTransform(object,{})));
        const auto localOrigin=glm::dvec3(inverse*glm::dvec4(origin,1));
        const auto localDirection=glm::dvec3(inverse*glm::dvec4(direction,0));
        // Conservative unit-box rejection makes even an occasional pick cheap.
        double low=0,high=maximum;bool outside=false;
        for(unsigned axis=0;axis<3;++axis) {
            if(std::abs(localDirection[axis])<1e-14) {
                if(std::abs(localOrigin[axis])>1){outside=true;break;}
            } else {
                const double a=(-1-localOrigin[axis])/localDirection[axis];
                const double b=(1-localOrigin[axis])/localDirection[axis];
                low=std::max(low,std::min(a,b));high=std::min(high,std::max(a,b));
                if(low>high){outside=true;break;}
            }
        }
        if(outside)continue;
        const auto& mesh=meshes[object.mesh==MaterialLab::ShowcaseMesh::Plane?0:object.mesh==MaterialLab::ShowcaseMesh::Box?1:2];
        for(std::size_t triangle=0;triangle+2<mesh.indices.size();triangle+=3) {
            const auto a=glm::dvec3(mesh.vertices[mesh.indices[triangle]].position);
            const auto ab=glm::dvec3(mesh.vertices[mesh.indices[triangle+1]].position)-a;
            const auto ac=glm::dvec3(mesh.vertices[mesh.indices[triangle+2]].position)-a;
            const auto q=glm::cross(localDirection,ac);const double determinant=glm::dot(ab,q);
            if(std::abs(determinant)<1e-13)continue;
            const auto relative=localOrigin-a;const double u=glm::dot(relative,q)/determinant;
            if(u<0||u>1)continue;
            const auto r=glm::cross(relative,ab);const double v=glm::dot(localDirection,r)/determinant;
            if(v<0||u+v>1)continue;
            const double distance=glm::dot(ac,r)/determinant;
            if(distance>1e-5&&distance<maximum){maximum=distance;nearest=index;}
        }
    }
    return IsProp(nearest)?nearest:NoSelection;
}
inline unsigned Pick(const std::vector<MaterialLab::ProbePlacementObject>& objects,
    const Render::PipelineCamera& camera,double cursorU,double cursorV) {
    if(cursorU<0||cursorU>1||cursorV<0||cursorV>1)return NoSelection;
    const auto inverse=glm::inverse(glm::dmat4(camera.projection)*glm::dmat4(camera.view));
    auto farPoint=inverse*glm::dvec4(cursorU*2-1,1-cursorV*2,1,1);farPoint/=farPoint.w;
    const auto direction=glm::normalize(glm::dvec3(farPoint)-glm::dvec3(camera.position));
    return Pick(objects,glm::dvec3(camera.position),direction,camera.farPlane);
}
}
