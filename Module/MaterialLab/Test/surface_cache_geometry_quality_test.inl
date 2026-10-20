// Geometry-only transport fixture. The independent CPU reference intersects
// double-precision triangles and reads the known emitter material directly;
// it does not call the production BVH/SDF, Surface Cache lookup, origin policy,
// probe placement, or reconstruction. These intentionally simple emitters
// isolate geometry/placement errors from direct-light shadow/card resolution.
namespace SurfaceCacheGeometryFixture {
struct Triangle {
    glm::dvec3 a,b,c,n,minimum,maximum;
    unsigned object=0;
};
struct Hit { double t=1e30;int triangle=-1; };
struct Oracle {
    struct Node {glm::dvec3 minimum{1e30},maximum{-1e30};unsigned begin=0,end=0;int left=-1,right=-1;};
    std::vector<Triangle> triangles;
    std::vector<unsigned> order;
    std::vector<Node> nodes;
    std::vector<bool> closed;
    std::vector<SealedGiRoomMaterial> materials;
    int Build(unsigned begin,unsigned end) {
        const int index=int(nodes.size());nodes.emplace_back();
        glm::dvec3 minimum(1e30),maximum(-1e30),centroidMinimum(1e30),centroidMaximum(-1e30);
        for(unsigned i=begin;i<end;++i){const auto& t=triangles[order[i]];
            minimum=glm::min(minimum,t.minimum);maximum=glm::max(maximum,t.maximum);
            const auto c=(t.a+t.b+t.c)/3.;centroidMinimum=glm::min(centroidMinimum,c);centroidMaximum=glm::max(centroidMaximum,c);}
        nodes[index].minimum=minimum;nodes[index].maximum=maximum;nodes[index].begin=begin;nodes[index].end=end;
        if(end-begin>8){const auto extent=centroidMaximum-centroidMinimum;
            const unsigned axis=extent.x>extent.y?(extent.x>extent.z?0u:2u):(extent.y>extent.z?1u:2u),middle=(begin+end)/2;
            std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](unsigned a,unsigned b){
                const auto& x=triangles[a];const auto& y=triangles[b];
                return x.a[axis]+x.b[axis]+x.c[axis]<y.a[axis]+y.b[axis]+y.c[axis];});
            const int left=Build(begin,middle),right=Build(middle,end);nodes[index].left=left;nodes[index].right=right;}
        return index;
    }
    void Initialize(const std::vector<ProbePlacementObject>& objects,const std::vector<SealedGiRoomMaterial>& values) {
        materials=values;closed.resize(objects.size());const auto box=MakeBox(),sphere=MakeSphere(48,64),plane=MakePlane();
        for(unsigned object=0;object<objects.size();++object){const auto& input=objects[object];
            closed[object]=input.mesh!=ShowcaseMesh::Plane;
            const auto& mesh=input.mesh==ShowcaseMesh::Sphere?sphere:input.mesh==ShowcaseMesh::Plane?plane:box;
            const auto transform=glm::dmat4(ProbePlacementTransform(input,{}));
            for(std::size_t i=0;i+2<mesh.indices.size();i+=3){Triangle t;
                t.a=glm::dvec3(transform*glm::dvec4(mesh.vertices[mesh.indices[i]].position,1));
                t.b=glm::dvec3(transform*glm::dvec4(mesh.vertices[mesh.indices[i+1]].position,1));
                t.c=glm::dvec3(transform*glm::dvec4(mesh.vertices[mesh.indices[i+2]].position,1));
                const auto area=glm::cross(t.b-t.a,t.c-t.a);if(glm::length(area)<1e-12)continue;
                t.n=glm::normalize(area);t.object=object;t.minimum=glm::min(t.a,glm::min(t.b,t.c));t.maximum=glm::max(t.a,glm::max(t.b,t.c));
                triangles.push_back(t);}
        }
        order.resize(triangles.size());for(unsigned i=0;i<order.size();++i)order[i]=i;
        Check(!order.empty(),"Geometry fixture has no independent triangles");Build(0,unsigned(order.size()));
    }
    static bool Box(const Node& node,glm::dvec3 origin,glm::dvec3 direction,double maximum) {
        double low=0,high=maximum;
        for(unsigned axis=0;axis<3;++axis){
            if(std::abs(direction[axis])<1e-15){if(origin[axis]<node.minimum[axis]||origin[axis]>node.maximum[axis])return false;continue;}
            double a=(node.minimum[axis]-origin[axis])/direction[axis],b=(node.maximum[axis]-origin[axis])/direction[axis];
            if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);if(low>high)return false;}
        return true;
    }
    void Traverse(int index,glm::dvec3 origin,glm::dvec3 direction,double minimum,Hit& hit) const {
        const auto& node=nodes[unsigned(index)];if(!Box(node,origin,direction,hit.t))return;
        if(node.left>=0){Traverse(node.left,origin,direction,minimum,hit);Traverse(node.right,origin,direction,minimum,hit);return;}
        for(unsigned i=node.begin;i<node.end;++i){const auto triangle=order[i];const auto& t=triangles[triangle];
            const auto ab=t.b-t.a,ac=t.c-t.a,q=glm::cross(direction,ac);const double determinant=glm::dot(ab,q);
            if(std::abs(determinant)<1e-13)continue;const auto offset=origin-t.a;const double u=glm::dot(offset,q)/determinant;
            if(u<0||u>1)continue;const auto r=glm::cross(offset,ab);const double v=glm::dot(direction,r)/determinant;
            if(v<0||u+v>1)continue;const double distance=glm::dot(ac,r)/determinant;
            if(distance>minimum&&distance<hit.t){hit.t=distance;hit.triangle=int(triangle);}}
    }
    Hit Nearest(glm::dvec3 origin,glm::dvec3 direction,double maximum=30,double minimum=1e-7) const {
        Hit hit;hit.t=maximum;Traverse(0,origin,direction,minimum,hit);return hit;
    }
    bool Inside(glm::dvec3 p,unsigned object,double epsilon=2e-6) const {
        if(!closed[object])return false;bool found=false;
        // All fixture solids are convex. The true triangle planes, rather than
        // an analytic smooth sphere or distance texture, define containment.
        for(const auto& t:triangles)if(t.object==object){found=true;if(glm::dot(p-t.a,t.n)>=-epsilon)return false;}
        return found;
    }
    double MaximumSignedDistance(glm::dvec3 p,unsigned object) const {
        double result=-1e30;for(const auto& triangle:triangles)if(triangle.object==object)
            result=std::max(result,glm::dot(p-triangle.a,triangle.n));return result;
    }
    bool InsideAny(glm::dvec3 p,int ignore=-1) const {
        for(unsigned object=0;object<closed.size();++object)if(int(object)!=ignore&&Inside(p,object))return true;return false;
    }
};
struct Scene {
    std::string name;
    std::shared_ptr<std::vector<ProbePlacementObject>> objects=std::make_shared<std::vector<ProbePlacementObject>>();
    std::shared_ptr<std::vector<SealedGiRoomMaterial>> materials=std::make_shared<std::vector<SealedGiRoomMaterial>>();
    Render::PipelineCamera camera;
    bool curved=false,intersecting=false;
};
Scene MakeScene(unsigned index,float aspect) {
    Scene scene;using Mesh=ShowcaseMesh;using Material=ProbePlacementMaterial;
    const auto add=[&](const char* name,Mesh mesh,glm::vec3 p,glm::vec3 s,glm::vec3 emission,float tilt=0,glm::vec3 axis=glm::vec3(0,0,1)){
        scene.objects->push_back({name,mesh,Material::Block,p,s,0,tilt,axis,false});
        scene.materials->push_back({glm::vec3(.8f),.85f,emission});};
    if(index<5){const std::array<float,5> angles{20,45,60,90,120};const float half=glm::radians(angles[index]*.5f),length=1.1f,thickness=.09f;
        scene.name="v"+std::to_string(unsigned(angles[index]));const glm::vec3 apex(0,.25f,0);
        const glm::vec3 leftT(-std::sin(half),std::cos(half),0),rightT(std::sin(half),std::cos(half),0);
        const glm::vec3 leftN(std::cos(half),std::sin(half),0),rightN(-std::cos(half),std::sin(half),0);
        add("Left groove slab",Mesh::Box,apex+leftT*length-leftN*thickness,{length,thickness,1.3f},{.24f,.04f,.02f},glm::half_pi<float>()+half);
        add("Right groove slab",Mesh::Box,apex+rightT*length-rightN*thickness,{length,thickness,1.3f},{.02f,.12f,.24f},glm::half_pi<float>()-half);
        scene.camera.position={0,3.1f,4.1f};scene.camera.view=glm::lookAt(scene.camera.position,{0,1,0},{0,1,0});
    }else{const std::array<float,3> gap{.08f,0,-.12f};scene.name=index==5?"sphere_box_gap":index==6?"sphere_box_contact":"sphere_box_intersection";
        scene.curved=true;scene.intersecting=index==7;
        add("Curved emitter",Mesh::Sphere,{-.45f,.85f,0},{.85f,.85f,.85f},{.20f,.055f,.015f});
        add("Box emitter",Mesh::Box,{.95f+gap[index-5],.8f,0},{.55f,.8f,.72f},{.015f,.08f,.24f});
        add("Ground emitter",Mesh::Box,{0,-.09f,0},{2.7f,.09f,2.0f},{.045f,.05f,.035f});
        scene.camera.position={.4f,1.7f,4.0f};scene.camera.view=glm::lookAt(scene.camera.position,{.05f,.85f,0},{0,1,0});}
    // An open, one-sided sheet faces away from the camera/receivers. Rays that
    // reach its back are deliberately readable black, even with emission.
    add("Back-facing diagnostic sheet",Mesh::Plane,{0,1,-1.8f},{2.5f,1,2.5f},{.4f,.3f,.2f},-glm::half_pi<float>(),{1,0,0});
    scene.camera.nearPlane=.02f;scene.camera.farPlane=20;
    scene.camera.projection=glm::perspective(glm::radians(48.f),aspect,scene.camera.nearPlane,scene.camera.farPlane);
    return scene;
}
double RadicalInverse(unsigned value) {
    double result=0,weight=.5;while(value){if(value&1u)result+=weight;value>>=1;weight*=.5;}return result;
}
bool Readable(int value){return value==1||value==5||value==9||value==10||value==13;}
}

void SurfaceCacheGeometryQuality(MaterialLabModule& lab,const Options& options) {
    using namespace SurfaceCacheGeometryFixture;
    auto fixture=options;fixture.sealedGiRoom=false;fixture.sdfBanquet=false;fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);frame.width=256;frame.height=192;frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.probeDiagnostics=false;gi.collectProbeCoverage=true;
    gi.temporal.enabled=false;gi.spatial.enabled=false;gi.rayBias=.06f;gi.maxDistance=20;
    placement.enabled=true;placement.adaptive=true;placement.baseSpacing=32;placement.candidateCount=16;
    placement.freeze=false;placement.reset=true;placement.frameIndex=0;placement.reconstructionVisibility=true;
    trace.enabled=true;trace.cosineHemisphere=true;trace.showPanel=false;trace.rayResolution=8;trace.maxSteps=512;
    trace.sdf.enabled=true;trace.sdf.automaticQuality=false;trace.sdf.meshQuality=1;
    // The finite Global SDF's outer boundary is not an occluder or an unknown
    // emitter in this bounded geometry test. Traverse local fields over the
    // whole interval so escaped rays terminate as explicit readable black.
    trace.sdf.useMesh=true;trace.sdf.useGlobal=false;trace.sdf.meshTraceDistance=gi.maxDistance;trace.sdf.maxSteps=512;
    auto& effects=frame.effects;effects.areaLights={};effects.spotLight.enabled=false;effects.shadows.sunIntensity=0;
    effects.shadows.enabled=false;effects.sky.enabled=false;effects.reflection.enabled=false;effects.lighting.ambientAndExposure={0,0,0,1};
    for(auto& color:effects.lighting.lightColors)color=glm::vec4(0);
    const std::size_t pixels=std::size_t(frame.width)*frame.height;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());effects.cameraCut=false;placement.reset=false;};
    const auto finite=[&](const auto& values,const char* what){Check(!values.empty(),std::string("Missing geometry fixture ")+what+": "+lab.LastError());
        for(const auto& value:values)for(unsigned channel=0;channel<4;++channel)Check(std::isfinite(value[channel]),"Geometry fixture produced nonfinite values");};
    struct Reference {std::size_t pixel;glm::dvec3 value;unsigned object;bool frontSolid;};
    struct Summary {std::uint64_t active=0,adaptive=0,sourceInside=0,legacyCross=0,actualCross=0,clipped=0,uncertain=0,invalid=0;
        std::uint64_t solidReceiverInvalid=0,openSheetInvalid=0,unknownOwnerInvalid=0,invalidFrontSource=0,invalidBackSource=0,invalidSourceInsideOther=0,invalidRays=0;
        std::array<std::uint64_t,128> invalidByOwner{};
        std::array<std::uint64_t,6> invalidReasons{},solidInvalidReasons{};
        std::uint64_t front=0,back=0,miss=0,knownBlack=0,unknown=0,blackViolation=0,holes=0,referenceSamples=0,curvedAdaptive=0,curvedReferenceSamples=0;
        std::array<std::uint64_t,18> statuses{};
        double mse=0,dark=0,bright=0,curvedMse=0,frontSolidMse=0,affineMse=0,sphereAffineMse=0,sphereResidualGradientRms=0,sphereResidualGradientP99=0,constantMaximum=0;
        std::uint64_t sphereAffinePixels=0,sphereGradientPairs=0,frontSolidReferenceSamples=0;};
    std::uint64_t allLegacyCross=0,allClipped=0,allFront=0,allBack=0,allMiss=0,allCurvatureAdded=0;
    std::shared_ptr<std::vector<SealedGiRoomMaterial>> lastMutableMaterials;
    for(unsigned sceneIndex=0;sceneIndex<8;++sceneIndex){const auto scene=MakeScene(sceneIndex,frame.OutputAspect());
        lastMutableMaterials=scene.materials;
        frame.probeSceneOverride=scene.objects;frame.probeSceneMaterialsOverride=scene.materials;frame.camera=scene.camera;
        placement.reset=true;effects.cameraCut=true;gi.reconstructionDebug=0;gi.safeProbeOrigins=true;placement.curvatureRefinement=false;
        render();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Geometry fixture SDF preparation timed out");}
        const auto prepared=lab.PreparedProbeSdfScene();Check(prepared&&prepared->Instances().size()==scene.objects->size(),"Custom raster/SDF scene object order diverged");
        Oracle oracle;oracle.Initialize(*scene.objects,*scene.materials);
        const auto source=lab.ReadbackScreenProbeSources();finite(source.sourceViewPositions,"receiver positions");finite(source.sourceWorldNormals,"receiver normals");
        Check(source.sourceViewPositions.size()==pixels&&source.sourceWorldNormals.size()==pixels,"Geometry fixture source extent changed");
        const auto inverseView=glm::dmat4(glm::inverse(scene.camera.view));
        std::vector<unsigned char> sphereReceivers(pixels,0);
        if(scene.curved)for(std::size_t pixel=0;pixel<pixels;++pixel){const auto vp=source.sourceViewPositions[pixel];if(vp.w<.5f)continue;
            const auto p=glm::dvec3(inverseView*glm::dvec4(glm::dvec3(vp),1));
            const auto direction=glm::normalize(p-glm::dvec3(scene.camera.position));
            const auto hit=oracle.Nearest(glm::dvec3(scene.camera.position),direction,glm::length(p-glm::dvec3(scene.camera.position))+.002);
            if(hit.triangle>=0&&oracle.triangles[unsigned(hit.triangle)].object==0)sphereReceivers[pixel]=1;}
        std::vector<Reference> reference;std::uint64_t sampledInside=0;
        for(unsigned y=4;y<frame.height;y+=8)for(unsigned x=4;x<frame.width;x+=8){const auto pixel=std::size_t(y)*frame.width+x;
            const auto vp=source.sourceViewPositions[pixel],wn=source.sourceWorldNormals[pixel];if(vp.w<.5f||wn.w<.5f)continue;
            const auto p=glm::dvec3(inverseView*glm::dvec4(glm::dvec3(vp),1)),n=glm::normalize(glm::dvec3(wn));
            const auto primaryDirection=glm::normalize(p-glm::dvec3(scene.camera.position));
            const auto primary=oracle.Nearest(glm::dvec3(scene.camera.position),primaryDirection,glm::length(p-glm::dvec3(scene.camera.position))+.002);
            if(primary.triangle<0)continue;const unsigned receiver=oracle.triangles[unsigned(primary.triangle)].object;
            if(oracle.InsideAny(p,int(receiver))){++sampledInside;continue;}
            // Independent geometric epsilon, fixed in world units and unrelated
            // to the production .06 stress bias or safe-origin settings.
            const auto origin=p+n*1e-6,axis=std::abs(n.z)<.99?glm::dvec3(0,0,1):glm::dvec3(0,1,0);
            const auto tangent=glm::normalize(glm::cross(axis,n)),bitangent=glm::cross(n,tangent);glm::dvec3 total(0);
            constexpr unsigned count=256;
            for(unsigned ray=0;ray<count;++ray){const double u=(double(ray)+.5)/count,phi=glm::two_pi<double>()*RadicalInverse(ray);
                const auto direction=tangent*(std::sqrt(u)*std::cos(phi))+bitangent*(std::sqrt(u)*std::sin(phi))+n*std::sqrt(1-u);
                const auto hit=oracle.Nearest(origin,direction,20);
                if(hit.triangle>=0){const auto& t=oracle.triangles[unsigned(hit.triangle)];if(glm::dot(t.n,-direction)>0)total+=glm::dvec3(oracle.materials[t.object].emission);}}
            const bool frontSolid=oracle.closed[receiver]&&glm::dot(oracle.triangles[unsigned(primary.triangle)].n,-primaryDirection)>0;
            reference.push_back({pixel,glm::dvec3(oracle.materials[receiver].albedo)*total/double(count),receiver,frontSolid});
        }
        Check(reference.size()>40,"Geometry fixture did not expose enough independently verified receiver samples");
        std::array<Summary,3> results;
        for(unsigned mode=0;mode<3;++mode){gi.safeProbeOrigins=mode!=0;placement.curvatureRefinement=mode==2;gi.reconstructionDebug=0;
            trace.samplingSeed=11;placement.reset=true;render();
            const auto probes=lab.ReadbackScreenProbePlacement();const auto rays=lab.ReadbackScreenProbeTrace(false);
            const auto raw=lab.ReadbackSurfaceCacheRawGi(),coverage=lab.ReadbackSurfaceCacheProbeCoverage();finite(raw,"raw GI");finite(coverage,"probe coverage");
            Check(raw.size()==pixels&&coverage.size()==pixels,"Geometry fixture reconstruction extent changed");
            const auto origins=mode?lab.ReadbackSurfaceCacheProbeOrigins():std::vector<glm::vec4>{};
            const auto slots=probes.uniform.size()+probes.spawned.size();
            if(mode){finite(origins,"actual GPU probe origins");Check(origins.size()==slots,"Safe origin atlas does not preserve logical probe-major indexing");}
            Check(rays.probeSlots==slots&&rays.radianceStatus.size()==slots*64&&rays.rayDirectionDepthFetches.size()==slots*64,"Geometry fixture ray atlas shape changed");
            auto& result=results[mode];
            for(std::size_t probe=0;probe<slots;++probe){const bool uniform=probe<probes.uniform.size();const auto index=uniform?probe:probe-probes.uniform.size();
                const auto pp=uniform?probes.uniform[index]:probes.spawned[index],nd=uniform?probes.uniformNormals[index]:probes.spawnedNormals[index];
                if(pp.w<.5f){if(mode)Check(origins[probe].w==0,"Inactive probe retained safe origin metadata");continue;}
                ++result.active;result.adaptive+=!uniform;const auto p=glm::dvec3(pp),n=glm::normalize(glm::dvec3(nd));
                const auto cameraDirection=glm::normalize(p-glm::dvec3(scene.camera.position));const auto primary=oracle.Nearest(glm::dvec3(scene.camera.position),cameraDirection,glm::length(p-glm::dvec3(scene.camera.position))+.002);
                const int owner=primary.triangle>=0?int(oracle.triangles[unsigned(primary.triangle)].object):-1;
                result.curvedAdaptive+=scene.curved&&!uniform&&owner==0;
                const bool sourceInside=oracle.InsideAny(p,owner);result.sourceInside+=sourceInside;
                // Legacy position is the explicitly documented old contract;
                // only the enabled safe branch below uses actual GPU metadata.
                double legacyBias=gi.rayBias;const auto clip=glm::dmat4(scene.camera.projection*scene.camera.view)*glm::dvec4(p,1);
                if(clip.w>0){const auto ndc=glm::dvec3(clip)/clip.w;
                    auto corner=glm::dmat4(glm::inverse(scene.camera.projection))*glm::dvec4(ndc.x+1./frame.width,ndc.y+1./frame.height,ndc.z,1);corner/=corner.w;
                    const auto cornerWorld=glm::dvec3(inverseView*glm::dvec4(glm::dvec3(corner),1));
                    legacyBias=std::max(legacyBias,2.*std::abs(glm::dot(cornerWorld-p,n)));}
                const auto legacy=p+n*legacyBias;const bool legacyCross=!sourceInside&&oracle.InsideAny(legacy);
                result.legacyCross+=legacyCross;glm::dvec3 origin=legacy;int status=1;
                if(mode){origin=glm::dvec3(origins[probe]);status=int(std::lround(origins[probe].w));
                    Check(status>=-5&&status<=3&&status!=0,"Unexpected or inactive safe origin classification for an active probe");
                    result.clipped+=status==2;result.uncertain+=status==3;result.invalid+=status<0;
                    if(status<=0){
                        result.invalidSourceInsideOther+=sourceInside;
                        const bool validOwner=owner>=0&&unsigned(owner)<oracle.closed.size();
                        const bool solid=validOwner&&oracle.closed[unsigned(owner)];
                        if(status<0){++result.invalidReasons[unsigned(-status)];if(solid)++result.solidInvalidReasons[unsigned(-status)];}
                        result.solidReceiverInvalid+=solid;result.openSheetInvalid+=validOwner&&!solid;result.unknownOwnerInvalid+=!validOwner;
                        if(validOwner)++result.invalidByOwner[unsigned(owner)];
                        const double facing=primary.triangle>=0?glm::dot(oracle.triangles[unsigned(primary.triangle)].n,-cameraDirection):0;
                        result.invalidFrontSource+=facing>0;result.invalidBackSource+=primary.triangle>=0&&facing<=0;
                        if((solid&&result.solidReceiverInvalid<=10)||(!solid&&result.openSheetInvalid<=2)){
                            const auto delta=origin-p;
                            std::cout<<std::setprecision(12)<<"SURFACE CACHE GEOMETRY INVALID scene="<<scene.name<<" mode="<<mode<<" probe="<<probe
                                <<" owner="<<owner<<" solid="<<solid<<" source_inside_other="<<sourceInside<<" primary_facing="<<facing
                                <<" p="<<p.x<<","<<p.y<<","<<p.z<<" normal="<<n.x<<","<<n.y<<","<<n.z
                                <<" source_camera="<<scene.camera.position.x<<","<<scene.camera.position.y<<","<<scene.camera.position.z
                                <<" view_direction="<<cameraDirection.x<<","<<cameraDirection.y<<","<<cameraDirection.z
                                <<" origin="<<origin.x<<","<<origin.y<<","<<origin.z<<" status="<<status<<" legacy_bias="<<legacyBias
                                <<" displacement="<<delta.x<<","<<delta.y<<","<<delta.z;
                            if(primary.triangle>=0){const auto point=glm::dvec3(scene.camera.position)+cameraDirection*primary.t;
                                const auto& triangle=oracle.triangles[unsigned(primary.triangle)];
                                std::cout<<" primary_tri="<<primary.triangle<<" primary_error="<<glm::length(point-p)<<" primary_distance="<<primary.t
                                    <<" primary_normal="<<triangle.n.x<<","<<triangle.n.y<<","<<triangle.n.z;}
                            if(validOwner)std::cout<<" source_own_max_plane="<<oracle.MaximumSignedDistance(p,unsigned(owner));
                            std::cout<<std::setprecision(6)<<std::endl;}
                        // Rejected origins remain explicit unavailable samples,
                        // not omitted measurements or fabricated known black.
                        for(unsigned ray=0;ray<64;++ray){const auto value=rays.radianceStatus[probe*64+ray];const int rayStatus=int(std::lround(value.w));
                            ++result.invalidRays;if(rayStatus>=0&&rayStatus<int(result.statuses.size()))++result.statuses[unsigned(rayStatus)];
                            result.unknown+=rayStatus>0;
                            Check(rayStatus>0&&!Readable(rayStatus)&&glm::length(glm::vec3(value))<1e-8f,"Rejected safe probe origin failed to remain explicit unavailable zero-energy lighting");}
                        continue;}}
                if(!sourceInside&&oracle.InsideAny(origin)){
                    ++result.actualCross;
                    if(mode&&result.actualCross<=8){
                        const auto projected=glm::dmat4(scene.camera.projection*scene.camera.view)*glm::dvec4(p,1);
                        const auto screen=(glm::dvec2(projected)/projected.w*.5+.5)*glm::dvec2(frame.width,frame.height)-.5;
                        const auto delta=origin-p;const auto positive=oracle.Nearest(p,n,1),negative=oracle.Nearest(p,-n,1);
                        std::cout<<std::setprecision(12)<<"SURFACE CACHE GEOMETRY INSIDE scene="<<scene.name<<" mode="<<mode<<" probe="<<probe
                            <<" owner="<<owner<<" source_inside_other="<<sourceInside<<" origin_inside_other="<<oracle.InsideAny(origin,owner)
                            <<" origin_inside_own="<<(owner>=0&&oracle.Inside(origin,unsigned(owner)))
                            <<" p="<<p.x<<","<<p.y<<","<<p.z<<" normal="<<n.x<<","<<n.y<<","<<n.z
                            <<" origin="<<origin.x<<","<<origin.y<<","<<origin.z<<" status="<<status<<" legacy_bias="<<legacyBias
                            <<" displacement="<<delta.x<<","<<delta.y<<","<<delta.z<<" normal_step="<<glm::dot(delta,n)
                            <<" screen="<<screen.x<<","<<screen.y<<" view_depth="<<nd.w;
                        if(primary.triangle>=0){const auto& triangle=oracle.triangles[unsigned(primary.triangle)];
                            const auto primaryPoint=glm::dvec3(scene.camera.position)+cameraDirection*primary.t;
                            std::cout<<" primary_tri="<<primary.triangle<<" primary_t="<<primary.t<<" primary_error="<<glm::length(primaryPoint-p)
                                <<" primary_point="<<primaryPoint.x<<","<<primaryPoint.y<<","<<primaryPoint.z
                                <<" primary_normal="<<triangle.n.x<<","<<triangle.n.y<<","<<triangle.n.z;}
                        for(unsigned object=0;object<oracle.closed.size();++object)if(oracle.closed[object]){
                            const double pSigned=oracle.MaximumSignedDistance(p,object),oSigned=oracle.MaximumSignedDistance(origin,object);
                            std::cout<<" object"<<object<<"_source_max_plane="<<pSigned<<" object"<<object<<"_origin_max_plane="<<oSigned;}
                        const auto printNormalHit=[&](const char* name,const Hit& hit){std::cout<<" "<<name<<"_tri="<<hit.triangle<<" "<<name<<"_t="<<hit.t;
                            if(hit.triangle>=0){const auto& triangle=oracle.triangles[unsigned(hit.triangle)];
                                std::cout<<" "<<name<<"_owner="<<triangle.object<<" "<<name<<"_normal_dot="<<glm::dot(triangle.n,n);}};
                        printNormalHit("plus_normal",positive);printNormalHit("minus_normal",negative);
                        std::cout<<std::setprecision(6)<<std::endl;
                    }
                }
                for(unsigned ray=0;ray<64;++ray){const auto address=probe*64+ray;const auto value=rays.radianceStatus[address];
                    const int rayStatus=int(std::lround(value.w));if(rayStatus>=0&&rayStatus<int(result.statuses.size()))++result.statuses[unsigned(rayStatus)];
                    if(!Readable(rayStatus)){result.unknown+=rayStatus>0;continue;}
                    result.knownBlack+=glm::length(glm::vec3(value))<1e-8f;
                    const auto direction=glm::normalize(glm::dvec3(rays.rayDirectionDepthFetches[address]));
                    const auto hit=oracle.Nearest(origin,direction,20,1e-5);bool shouldBlack=false;
                    if(hit.triangle<0){++result.miss;shouldBlack=true;}
                    else if(glm::dot(oracle.triangles[unsigned(hit.triangle)].n,-direction)<=0){++result.back;shouldBlack=true;}
                    else ++result.front;
                    if(shouldBlack&&glm::length(glm::vec3(value))>2e-5f){
                        ++result.blackViolation;
                        if(result.blackViolation<=12){const auto gpuHit=rays.hitWorldDistance[address],gpuMetadata=rays.hitPixelStepsMip[address];
                            std::cout<<"SURFACE CACHE GEOMETRY BLACK MISMATCH scene="<<scene.name<<" mode="<<mode<<" probe="<<probe<<" ray="<<ray
                                <<" status="<<rayStatus<<" source_owner="<<owner<<" source_inside="<<sourceInside
                                <<" origin_status="<<status<<" origin="<<origin.x<<","<<origin.y<<","<<origin.z
                                <<" source="<<p.x<<","<<p.y<<","<<p.z<<" direction="<<direction.x<<","<<direction.y<<","<<direction.z
                                <<" gpu_rgb="<<value.x<<","<<value.y<<","<<value.z
                                <<" gpu_hit="<<gpuHit.x<<","<<gpuHit.y<<","<<gpuHit.z<<","<<gpuHit.w
                                <<" gpu_meta="<<gpuMetadata.x<<","<<gpuMetadata.y<<","<<gpuMetadata.z<<","<<gpuMetadata.w
                                <<" cpu_triangle="<<hit.triangle<<" cpu_distance="<<hit.t;
                            if(hit.triangle>=0){const auto& triangle=oracle.triangles[unsigned(hit.triangle)];
                                std::cout<<" cpu_owner="<<triangle.object<<" cpu_facing="<<glm::dot(triangle.n,-direction)
                                    <<" cpu_normal="<<triangle.n.x<<","<<triangle.n.y<<","<<triangle.n.z;}
                            std::cout<<std::endl;
                        }
                    }
                }
            }
            for(std::size_t i=0;i<pixels;++i)if(source.sourceViewPositions[i].w>.5f&&coverage[i].w<.5f)++result.holes;
            for(const auto& expected:reference){const auto delta=glm::dvec3(raw[expected.pixel])-expected.value;
                if(expected.frontSolid){result.frontSolidMse+=glm::dot(delta,delta);++result.frontSolidReferenceSamples;}
                if(scene.curved&&expected.object==0){result.curvedMse+=glm::dot(delta,delta);++result.curvedReferenceSamples;}
                for(unsigned channel=0;channel<3;++channel){result.mse+=delta[channel]*delta[channel];
                    result.dark+=std::min(0.,delta[channel])*std::min(0.,delta[channel]);result.bright+=std::max(0.,delta[channel])*std::max(0.,delta[channel]);}}
            result.referenceSamples=reference.size();result.mse/=3.*double(reference.size());result.dark/=3.*double(reference.size());result.bright/=3.*double(reference.size());
            result.curvedMse/=3.*double(std::max(std::uint64_t(1),result.curvedReferenceSamples));
            result.frontSolidMse/=3.*double(std::max(std::uint64_t(1),result.frontSolidReferenceSamples));
            Check(result.frontSolidReferenceSamples>40,"Geometry fixture did not retain independently front-facing solid receivers");
            std::cout<<"SURFACE CACHE GEOMETRY PRECHECK scene="<<scene.name<<" mode="<<mode<<" active="<<result.active
                <<" legacy_origin_inside="<<result.legacyCross<<" actual_origin_inside="<<result.actualCross
                <<" clipped="<<result.clipped<<" uncertain="<<result.uncertain<<" invalid="<<result.invalid
                <<" invalid_solid="<<result.solidReceiverInvalid<<" invalid_sheet="<<result.openSheetInvalid<<" invalid_owner_unknown="<<result.unknownOwnerInvalid
                <<" invalid_front="<<result.invalidFrontSource<<" invalid_back="<<result.invalidBackSource<<" invalid_source_other="<<result.invalidSourceInsideOther
                <<" front="<<result.front<<" back="<<result.back<<" miss="<<result.miss<<" known_black="<<result.knownBlack
                <<" black_ray_violation="<<result.blackViolation<<" unavailable="<<result.unknown<<" status14="<<result.statuses[14]
                <<" status15="<<result.statuses[15]<<" status16="<<result.statuses[16]<<" status17="<<result.statuses[17]<<" mse="<<result.mse<<std::endl;
            if(mode){std::cout<<"SURFACE CACHE GEOMETRY INVALID OWNERS scene="<<scene.name<<" mode="<<mode;
                for(unsigned object=0;object<oracle.closed.size();++object)std::cout<<" owner"<<object<<"="<<result.invalidByOwner[object];
                std::cout<<" invalid_rays="<<result.invalidRays<<std::endl;}
            if(mode){std::cout<<"SURFACE CACHE GEOMETRY INVALID REASONS scene="<<scene.name<<" mode="<<mode;
                for(unsigned reason=1;reason<=5;++reason)std::cout<<" reason-"<<reason<<"="<<result.invalidReasons[reason]<<" solid_reason-"<<reason<<"="<<result.solidInvalidReasons[reason];
                std::cout<<std::endl;}
            Check(result.active>5&&result.knownBlack>0,"Geometry fixture did not exercise active probes and readable black rays");
            // Legacy mode deliberately records preexisting screen false hits.
            // The new path must satisfy the independent black-ray contract;
            // RGB's 2e-5 numerical tolerance is applied above, not to counts.
            if(mode)Check(result.blackViolation==0,"Safe-origin gather emitted light along independently black/back-facing rays");
            if(mode)Check(result.actualCross==0,"Safe probe origin entered a neighboring convex solid from an exterior source");
            gi.reconstructionDebug=1;render();const auto constant=lab.ReadbackSurfaceCacheRawGi();finite(constant,"constant reconstruction");
            for(std::size_t i=0;i<pixels;++i)if(source.sourceViewPositions[i].w>.5f&&coverage[i].w>=.5f)
                result.constantMaximum=std::max(result.constantMaximum,std::abs(double(constant[i].x)-1.));
            Check(result.constantMaximum<3e-5,"Geometry-aware refinement lost constant-field normalization");
            gi.reconstructionDebug=3;render();const auto affine=lab.ReadbackSurfaceCacheRawGi();finite(affine,"analytic curved field");
            std::vector<double> residuals(pixels,std::numeric_limits<double>::quiet_NaN());
            std::uint64_t affineCount=0;for(std::size_t i=0;i<pixels;++i)if(source.sourceViewPositions[i].w>.5f&&coverage[i].w>=.5f){
                const auto p=glm::dvec3(inverseView*glm::dvec4(glm::dvec3(source.sourceViewPositions[i]),1));const double expected=std::clamp(.45+.04*(p.x+2*p.y+.5*p.z),.05,.95);
                const double error=double(affine[i].x)-expected;result.affineMse+=error*error;++affineCount;
                if(sphereReceivers[i]){residuals[i]=error;result.sphereAffineMse+=error*error;++result.sphereAffinePixels;}}
            result.affineMse/=double(std::max(std::uint64_t(1),affineCount));
            result.sphereAffineMse/=double(std::max(std::uint64_t(1),result.sphereAffinePixels));
            if(scene.curved){std::vector<double> gradients;double squares=0;
                for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto from=std::size_t(y)*frame.width+x;
                    if(!std::isfinite(residuals[from]))continue;
                    for(unsigned axis=0;axis<2;++axis){if((axis==0&&x+1==frame.width)||(axis==1&&y+1==frame.height))continue;
                        const auto to=from+(axis==0?1:frame.width);if(!std::isfinite(residuals[to]))continue;
                        const auto a=glm::normalize(glm::vec3(source.sourceWorldNormals[from])),b=glm::normalize(glm::vec3(source.sourceWorldNormals[to]));
                        if(glm::dot(a,b)<std::cos(glm::radians(10.f)))continue;
                        const auto p=glm::vec3(source.sourceViewPositions[from]),q=glm::vec3(source.sourceViewPositions[to]);
                        if(std::abs(p.z-q.z)>.03f*std::min(std::abs(p.z),std::abs(q.z))+.001f)continue;
                        const double gradient=std::abs(residuals[to]-residuals[from]);gradients.push_back(gradient);squares+=gradient*gradient;}}
                Check(result.sphereAffinePixels>100&&!gradients.empty(),"Curvature diagnostics did not retain compatible same-sphere receiver pairs");
                std::sort(gradients.begin(),gradients.end());result.sphereGradientPairs=gradients.size();
                result.sphereResidualGradientRms=std::sqrt(squares/double(gradients.size()));
                result.sphereResidualGradientP99=gradients[std::min(gradients.size()-1,std::size_t(std::ceil(.99*double(gradients.size())))-1)];}
            std::cout<<"SURFACE CACHE GEOMETRY scene="<<scene.name<<" mode="<<(mode==0?"legacy":mode==1?"safe":"safe_curvature")
                <<" active="<<result.active<<" adaptive="<<result.adaptive<<" source_inside="<<result.sourceInside<<" legacy_origin_inside="<<result.legacyCross
                <<" actual_origin_inside="<<result.actualCross<<" clipped="<<result.clipped<<" uncertain="<<result.uncertain<<" invalid="<<result.invalid
                <<" invalid_solid="<<result.solidReceiverInvalid<<" invalid_sheet="<<result.openSheetInvalid<<" invalid_owner_unknown="<<result.unknownOwnerInvalid
                <<" invalid_front="<<result.invalidFrontSource<<" invalid_back="<<result.invalidBackSource<<" invalid_source_other="<<result.invalidSourceInsideOther
                <<" front="<<result.front<<" back="<<result.back<<" miss="<<result.miss<<" known_black="<<result.knownBlack<<" unavailable="<<result.unknown
                <<" black_ray_violation="<<result.blackViolation<<" holes="<<result.holes<<" reference_pixels="<<result.referenceSamples<<" reference_spp=256"
                <<" mse="<<result.mse<<" dark_mse="<<result.dark<<" bright_mse="<<result.bright<<" curved_adaptive="<<result.curvedAdaptive
                <<" front_solid_reference_pixels="<<result.frontSolidReferenceSamples<<" front_solid_mse="<<result.frontSolidMse
                <<" curved_reference_pixels="<<result.curvedReferenceSamples<<" curved_mse="<<result.curvedMse<<" affine_mse="<<result.affineMse
                <<" sphere_affine_pixels="<<result.sphereAffinePixels<<" sphere_affine_mse="<<result.sphereAffineMse
                <<" sphere_gradient_pairs="<<result.sphereGradientPairs<<" sphere_residual_gradient_rms="<<result.sphereResidualGradientRms
                <<" sphere_residual_gradient_p99="<<result.sphereResidualGradientP99
                <<" constant_max_error="<<result.constantMaximum<<" sampled_source_inside="<<sampledInside<<std::endl;
        }
        allLegacyCross+=results[0].actualCross;allClipped+=results[2].clipped;allFront+=results[2].front;allBack+=results[2].back;allMiss+=results[2].miss;
        if(scene.curved&&results[2].curvedAdaptive>results[1].curvedAdaptive)allCurvatureAdded+=results[2].curvedAdaptive-results[1].curvedAdaptive;
        if(!options.screenshot.empty()){gi.reconstructionDebug=0;render();auto path=std::filesystem::path(options.screenshot);
            path.replace_filename(path.stem().string()+"_"+scene.name+path.extension().string());Save(lab.Readback(),path.string());}
    }
    Check(allLegacyCross>0,"Acute geometry fixture did not exercise any legacy origin crossing; safety was not tested");
    Check(allClipped>0,"Safe origin fixture did not execute clipping");
    Check(allFront>0&&allBack>0&&allMiss>0,"Geometry fixture omitted front, back-facing, or empty known-black rays");
    Check(allCurvatureAdded>0,"Curvature refinement never added a probe on the curved fixtures");
    // Five quarter-pixel translations use the same small curved fixture and
    // analytic field. No history, spatial filter, Monte Carlo field, or enlarged
    // scene can hide a placement pulse in this structural measurement.
    const auto motionScene=MakeScene(5,frame.OutputAspect());Oracle motionOracle;
    motionOracle.Initialize(*motionScene.objects,*motionScene.materials);
    frame.probeSceneOverride=motionScene.objects;frame.probeSceneMaterialsOverride=motionScene.materials;lastMutableMaterials=motionScene.materials;
    gi.safeProbeOrigins=true;gi.reconstructionDebug=3;placement.reconstructionVisibility=false;
    const auto originalCamera=motionScene.camera;const auto cameraInverse=glm::inverse(originalCamera.view);
    const auto right=glm::normalize(glm::vec3(cameraInverse[0])),forward=glm::normalize(-glm::vec3(cameraInverse[2]));
    const float focusDistance=glm::length(originalCamera.position-glm::vec3(.05f,.85f,0));
    const float focusWorldPixel=2.f*std::tan(glm::radians(24.f))*focusDistance/float(frame.height);
    std::uint64_t motionRampSamples=0;
    for(unsigned mode=0;mode<2;++mode){placement.curvatureRefinement=mode!=0;
        std::vector<double> previousResidual;std::vector<glm::vec4> previousNormals,previousView,previousActual;
        double allSquares=0,allActualSquares=0,maximum=0;std::vector<double> allAbsolute,allActualAbsolute;std::uint64_t allPairs=0;
        for(unsigned phase=0;phase<5;++phase){auto camera=originalCamera;camera.position+=right*(focusWorldPixel*.25f*float(phase));
            camera.view=glm::lookAt(camera.position,camera.position+forward,glm::vec3(0,1,0));frame.camera=camera;
            placement.reset=true;effects.cameraCut=true;gi.reconstructionDebug=0;render();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
            while(lab.ProbeSdfQualityStatistics().pending){render();Check(std::chrono::steady_clock::now()<deadline,"Curvature motion fixture SDF preparation timed out");}
            const auto actual=lab.ReadbackSurfaceCacheRawGi();finite(actual,"curvature motion actual GI");
            gi.reconstructionDebug=3;render();const auto current=lab.ReadbackSurfaceCacheRawGi();const auto source=lab.ReadbackScreenProbeSources();finite(current,"curvature motion affine field");
            const auto probes=lab.ReadbackScreenProbePlacement();std::uint64_t active=0,partial=0;
            for(const auto& value:probes.spawned){active+=value.w>.5f;partial+=value.w>4.0001f&&value.w<4.9999f;}motionRampSamples+=partial;
            const auto inverse=glm::dmat4(glm::inverse(camera.view));std::vector<double> residual(pixels,std::numeric_limits<double>::quiet_NaN());
            double mse=0,phaseSquares=0,phaseActualSquares=0;std::uint64_t count=0,pairs=0;
            for(std::size_t pixel=0;pixel<pixels;++pixel){const auto vp=source.sourceViewPositions[pixel];if(vp.w<.5f||current[pixel].w<=0)continue;
                const auto p=glm::dvec3(inverse*glm::dvec4(glm::dvec3(vp),1)),direction=glm::normalize(p-glm::dvec3(camera.position));
                const auto hit=motionOracle.Nearest(glm::dvec3(camera.position),direction,glm::length(p-glm::dvec3(camera.position))+.002);
                if(hit.triangle<0||motionOracle.triangles[unsigned(hit.triangle)].object!=0)continue;
                const double expected=std::clamp(.45+.04*(p.x+2*p.y+.5*p.z),.05,.95),error=double(current[pixel].x)-expected;
                residual[pixel]=error;mse+=error*error;++count;
                if(previousResidual.empty()||!std::isfinite(previousResidual[pixel]))continue;
                const auto n=glm::normalize(glm::vec3(source.sourceWorldNormals[pixel])),oldN=glm::normalize(glm::vec3(previousNormals[pixel]));
                if(glm::dot(n,oldN)<std::cos(glm::radians(10.f))||std::abs(vp.z-previousView[pixel].z)>.03f*std::min(std::abs(vp.z),std::abs(previousView[pixel].z))+.001f)continue;
                const double delta=error-previousResidual[pixel];phaseSquares+=delta*delta;allSquares+=delta*delta;
                const auto actualDelta=glm::dvec3(actual[pixel])-glm::dvec3(previousActual[pixel]);const double actualSquare=glm::dot(actualDelta,actualDelta);
                phaseActualSquares+=actualSquare;allActualSquares+=actualSquare;allActualAbsolute.push_back(std::sqrt(actualSquare/3.));
                maximum=std::max(maximum,std::abs(delta));allAbsolute.push_back(std::abs(delta));++pairs;++allPairs;}
            Check(count>100,"Quarter-pixel motion lost the curved receiver domain");
            std::cout<<"SURFACE CACHE CURVATURE MOTION mode="<<(mode?"curvature":"base")<<" phase="<<phase<<" camera_translation_at_focus_pixels="<<.25*phase
                <<" adaptive="<<active<<" partial_activation_probes="<<partial<<" sphere_pixels="<<count<<" affine_mse="<<mse/double(count)
                <<" residual_phase_pairs="<<pairs<<" residual_phase_delta_mse="<<phaseSquares/double(std::max(std::uint64_t(1),pairs))
                <<" actual_gi_frame_delta_rms="<<std::sqrt(phaseActualSquares/(3.*double(std::max(std::uint64_t(1),pairs))))
                <<" visibility=0 analytic_stochastic_field=0 actual_gi_fixed_seed=11"<<std::endl;
            previousResidual=std::move(residual);previousNormals=source.sourceWorldNormals;previousView=source.sourceViewPositions;previousActual=actual;
        }
        Check(allPairs>100,"Quarter-pixel motion did not retain same-sphere compatible receivers");std::sort(allAbsolute.begin(),allAbsolute.end());
        const auto p99=allAbsolute[std::min(allAbsolute.size()-1,std::size_t(std::ceil(.99*double(allAbsolute.size())))-1)];std::sort(allActualAbsolute.begin(),allActualAbsolute.end());
        const auto actualP99=allActualAbsolute[std::min(allActualAbsolute.size()-1,std::size_t(std::ceil(.99*double(allActualAbsolute.size())))-1)];
        std::cout<<"SURFACE CACHE CURVATURE MOTION TOTAL mode="<<(mode?"curvature":"base")<<" phases=5 step_at_focus_pixels=.25 pairs="<<allPairs
            <<" residual_phase_delta_rms="<<std::sqrt(allSquares/double(allPairs))<<" residual_phase_delta_p99="<<p99<<" residual_phase_delta_max="<<maximum
            <<" actual_gi_frame_delta_rms="<<std::sqrt(allActualSquares/(3.*double(allPairs)))<<" actual_gi_frame_delta_p99="<<actualP99
            <<" actual_delta_includes_physical_motion=1 fixed_seed=11"<<std::endl;
    }
    // Check actual spawned metadata, then compare a selected-probe impulse
    // with a brute-force mathematical normalization including that activation.
    // A counterfactual full-weight normalization must observably disagree.
    frame.camera=originalCamera;placement.curvatureRefinement=true;gi.reconstructionDebug=3;
    ScreenProbePlacementReadback rampProbes;unsigned selected=std::numeric_limits<unsigned>::max();float selectedActivation=0;
    for(const auto& budget:std::array<glm::vec2,3>{{{10,.75f},{20,1.5f},{35,3.f}}}){
        placement.curvatureNormalAngleDegrees=budget.x;placement.curvaturePlaneErrorPixels=budget.y;placement.reset=true;render();rampProbes=lab.ReadbackScreenProbePlacement();
        for(unsigned index=0;index<rampProbes.spawned.size();++index){const auto value=rampProbes.spawned[index];
            if(value.w<=4.05f||value.w>=4.95f)continue;
            const auto direction=glm::normalize(glm::dvec3(value)-glm::dvec3(originalCamera.position));
            const auto hit=motionOracle.Nearest(glm::dvec3(originalCamera.position),direction,glm::length(glm::dvec3(value)-glm::dvec3(originalCamera.position))+.002);
            if(hit.triangle>=0&&motionOracle.triangles[unsigned(hit.triangle)].object==0){selected=unsigned(rampProbes.uniform.size())+index;selectedActivation=value.w-4.f;break;}}
        if(selected!=std::numeric_limits<unsigned>::max())break;
    }
    Check(selected!=std::numeric_limits<unsigned>::max(),"Curvature ramp fixture never exercised a real spawned 0<activation<1 representative");
    trace.selectedProbe=selected;gi.reconstructionDebug=4;render();const auto impulse=lab.ReadbackSurfaceCacheRawGi();const auto rampSource=lab.ReadbackScreenProbeSources();finite(impulse,"curvature ramp impulse");
    struct WeightedProbe {glm::dvec3 p,n;glm::dvec2 screen;double depth=0,activation=1;unsigned index=0;bool adaptive=false;};
    std::vector<WeightedProbe> uniform(rampProbes.uniform.size()),adaptive;
    const auto viewProjection=glm::dmat4(originalCamera.projection*originalCamera.view);
    const auto sample=[&](glm::vec4 p,glm::vec4 nd,unsigned index,bool isAdaptive){WeightedProbe result;
        result.p=glm::dvec3(p);result.n=glm::normalize(glm::dvec3(nd));result.depth=nd.w;result.index=index;result.adaptive=isAdaptive;
        result.activation=isAdaptive&&p.w>=4?std::clamp(double(p.w)-4.,0.,1.):1.;
        const auto clip=viewProjection*glm::dvec4(result.p,1);result.screen=(glm::dvec2(clip)/clip.w*.5+.5)*glm::dvec2(frame.width,frame.height)-.5;return result;};
    for(unsigned index=0;index<uniform.size();++index)if(rampProbes.uniform[index].w>.5f)uniform[index]=sample(rampProbes.uniform[index],rampProbes.uniformNormals[index],index,false);
    for(unsigned index=0;index<rampProbes.spawned.size();++index)if(rampProbes.spawned[index].w>.5f)
        adaptive.push_back(sample(rampProbes.spawned[index],rampProbes.spawnedNormals[index],unsigned(uniform.size())+index,true));
    glm::dvec2 selectedScreen(-1e9);for(const auto& probe:adaptive)if(probe.index==selected)selectedScreen=probe.screen;
    const auto inverse=glm::dmat4(glm::inverse(originalCamera.view));const glm::dvec2 step(double(placement.baseSpacing)/4.,double(placement.baseSpacing)/2.);
    const auto basis=[](double x){const double a=std::abs(x);return a<.5?.75-a*a:.5*std::pow(std::max(0.,1.5-a),2);};
    const auto smooth=[](double first,double last,double value){const double t=std::clamp((value-first)/(last-first),0.,1.);return t*t*(3.-2.*t);};
    const auto coarseFraction=[&](unsigned coordinate,unsigned base,unsigned extent){
        if(!placement.continuousInterpolation)
            return std::clamp((double(coordinate)-double(base*placement.baseSpacing)+1.)/double(placement.baseSpacing+2),0.,1.);
        // Placement phase is explicitly zero. The final viewport-clipped
        // interval can be shorter than one nominal probe spacing.
        const unsigned first=base*placement.baseSpacing,last=std::min((base+1)*placement.baseSpacing,extent-1);
        return std::clamp((double(coordinate)-double(first))/double(std::max(last-first,1u)),0.,1.);
    };
    double maximumContract=0,maximumCounterfactualDifference=0,maximumResponse=0;std::uint64_t rampContractPixels=0;
    for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){const auto pixel=std::size_t(y)*frame.width+x;
        if(std::abs(selectedScreen.x-double(x))>1.5*step.x+1||std::abs(selectedScreen.y-double(y))>1.5*step.y+1)continue;
        const auto vp=rampSource.sourceViewPositions[pixel],normal=rampSource.sourceWorldNormals[pixel];if(vp.w<.5f||normal.w<.5f)continue;
        const auto p=glm::dvec3(inverse*glm::dvec4(glm::dvec3(vp),1)),n=glm::normalize(glm::dvec3(normal));const double depth=std::max(.0001,std::abs(double(vp.z)));
        const auto geometry=[&](const WeightedProbe& probe){if(probe.depth<=0)return 0.;const double alignment=glm::dot(n,probe.n);if(alignment<=.6)return 0.;
            const auto delta=probe.p-p;const double divisor=std::max(.0001,std::min(depth,probe.depth)),limit=double(placement.relativeDepthThreshold);
            const double strictPlane=std::max(std::abs(glm::dot(delta,n)),std::abs(glm::dot(delta,probe.n))),strictRelative=strictPlane/divisor;
            const double strict=strictRelative>limit?0.:std::exp2(-6.643856189774725*strictRelative/limit)*std::pow(alignment,8.);
            if(!placement.curvatureAwareWeights)return strict;
            // Independently evaluate the analytic impulse's geometric model in
            // double precision. Debug mode 4 has unit hit support; only the
            // 10-to-20-degree normal support blends strict and curved reuse.
            const double curvedPlane=std::abs(glm::dot(delta,glm::normalize(n+probe.n))),curvedRelative=curvedPlane/divisor;
            const double curved=curvedRelative>limit?0.:std::exp2(-6.643856189774725*curvedRelative/limit)*smooth(.6,.9,alignment)*
                (1.-smooth(.75*limit,limit,curvedRelative));
            const double angularSupport=smooth(std::cos(glm::radians(20.)),std::cos(glm::radians(10.)),alignment);
            return strict+(curved-strict)*angularSupport;};
        double mass=0,fullMass=0,numerator=0,fullNumerator=0;
        const unsigned baseX=std::min(x/placement.baseSpacing,rampProbes.uniformGridSize.x-2),baseY=std::min(y/placement.baseSpacing,rampProbes.uniformGridSize.y-2);
        const double fx=coarseFraction(x,baseX,frame.width),fy=coarseFraction(y,baseY,frame.height);
        for(unsigned corner=0;corner<4;++corner){const auto& probe=uniform[baseX+corner%2+(baseY+corner/2)*rampProbes.uniformGridSize.x];
            const double weight=(corner%2?fx:1-fx)*(corner/2?fy:1-fy)*geometry(probe);mass+=weight;fullMass+=weight;}
        for(const auto& probe:adaptive){const auto delta=(probe.screen-glm::dvec2(x,y))/step;const double weight=basis(delta.x)*basis(delta.y)*geometry(probe);
            mass+=weight*probe.activation;fullMass+=weight;if(probe.index==selected){numerator=weight*probe.activation;fullNumerator=weight;}}
        if(mass<.0002||fullMass<.0002||numerator<=0)continue;
        const double expected=numerator/mass,counterfactual=fullNumerator/fullMass;
        maximumContract=std::max(maximumContract,std::abs(double(impulse[pixel].x)-expected));
        maximumCounterfactualDifference=std::max(maximumCounterfactualDifference,std::abs(expected-counterfactual));
        maximumResponse=std::max(maximumResponse,double(impulse[pixel].x));++rampContractPixels;
    }
    std::cout<<"SURFACE CACHE CURVATURE RAMP selected="<<selected<<" spawned_activation="<<selectedActivation<<" contract_pixels="<<rampContractPixels
        <<" impulse_contract_max_error="<<maximumContract<<" full_weight_counterfactual_difference="<<maximumCounterfactualDifference
        <<" maximum_response="<<maximumResponse<<" motion_partial_activation_samples="<<motionRampSamples<<std::endl;
    Check(rampContractPixels>20&&maximumResponse>1e-4&&maximumCounterfactualDifference>1e-3,"Curvature activation metadata never observably affected reconstruction");
    Check(maximumContract<3e-4,"Curvature impulse did not use the actual continuous spawned activation in its normalized weights");
    trace.selectedProbe=std::numeric_limits<std::uint32_t>::max();
    // Off means known black, not unavailable. It must also invalidate lighting
    // after an override material edit, proving the arrays are content-keyed.
    Check(lastMutableMaterials.get()==frame.probeSceneMaterialsOverride.get(),"Geometry fixture lost its mutable test input owner");
    // Keep pointer identity unchanged: this verifies the renderer hashes actual
    // material contents rather than accidentally passing via pointer changes.
    for(auto& material:*lastMutableMaterials)material.emission=glm::vec3(0);
    gi.reconstructionDebug=0;render();const auto dark=lab.ReadbackSurfaceCacheRawGi();finite(dark,"zero-emission material edit");
    double maximum=0;for(const auto& value:dark)maximum=std::max(maximum,glm::length(glm::dvec3(value)));
    Check(maximum<1e-7,"Test-scene material edit reused stale emissive cards or injected GI");
    std::cout<<"SURFACE CACHE GEOMETRY PASS layouts=8 legacy_crossings="<<allLegacyCross<<" clipped="<<allClipped
        <<" curvature_added="<<allCurvatureAdded<<" front="<<allFront<<" back="<<allBack<<" miss="<<allMiss<<" zero_emission_max="<<maximum<<std::endl;
}
