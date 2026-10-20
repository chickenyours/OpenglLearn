// This fixture classifies visible receivers independently of the safe-origin
// shader. It reuses the double-precision triangle oracle above, rather than
// accepting a probe's own validity/coverage as the definition of a valid wall.
// Keep this include after surface_cache_geometry_quality_test.inl.
void SurfaceCacheOriginQuality(MaterialLabModule& lab,const Options& options) {
    using SurfaceCacheGeometryFixture::Oracle;
    auto fixture=options;fixture.sealedGiRoom=true;fixture.sdfBanquet=false;
    fixture.sdfClipmaps=false;fixture.surfaceCacheReference=false;
    auto frame=MakeSurfaceCacheGiFrame(fixture);
    frame.effects.outputSize={0,0};frame.present=false;frame.screenProbeStudyHud=false;
    auto& gi=frame.effects.surfaceCacheGi;auto& placement=frame.effects.screenProbePlacement;
    auto& trace=placement.trace;
    gi.enabled=true;gi.useScreenProbes=true;gi.view=2;gi.safeProbeOrigins=true;
    gi.probeDiagnostics=false;gi.probeOriginDiagnostics=false;gi.collectProbeCoverage=true;
    gi.temporal.enabled=false;gi.spatial.enabled=false;
    placement.enabled=true;placement.adaptive=true;placement.cornerRefinement=true;
    placement.curvatureRefinement=true;placement.baseSpacing=32;placement.candidateCount=8;
    placement.freeze=false;placement.frameIndex=0;
    trace.enabled=true;trace.cosineHemisphere=true;trace.rayResolution=4;
    trace.showPanel=false;trace.samplingSeed=17;trace.maxSteps=256;
    trace.sdf.enabled=true;trace.sdf.maxSteps=128;trace.sdf.automaticQuality=false;
    std::vector<SealedGiRoomMaterial> materials;
    const auto& objects=SealedGiRoomObjects();
    for(unsigned object=0;object<objects.size();++object)materials.push_back(SealedGiRoomObjectMaterial(object));
    Oracle oracle;oracle.Initialize(objects,materials);
    std::vector<glm::dvec3> minimum(objects.size(),glm::dvec3(1e30)),maximum(objects.size(),glm::dvec3(-1e30));
    for(const auto& triangle:oracle.triangles){minimum[triangle.object]=glm::min(minimum[triangle.object],triangle.minimum);
        maximum[triangle.object]=glm::max(maximum[triangle.object],triangle.maximum);}
    const auto inside=[&](glm::dvec3 point,int ignore=-1){
        for(unsigned object=0;object<objects.size();++object){
            if(int(object)==ignore||!oracle.closed[object])continue;
            bool inBounds=true;for(unsigned axis=0;axis<3;++axis)
                inBounds=inBounds&&point[axis]>minimum[object][axis]+2e-6&&point[axis]<maximum[object][axis]-2e-6;
            if(inBounds&&oracle.Inside(point,object))return true;
        }return false;
    };
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());
        frame.effects.cameraCut=false;placement.reset=false;};
    struct PlaneError {
        std::uint64_t count=0,inside=0,outside=0,nearZero=0;
        double squared=0,absoluteMaximum=0,minimum=0,maximum=0;
        void Add(double value){++count;squared+=value*value;absoluteMaximum=std::max(absoluteMaximum,std::abs(value));
            minimum=std::min(minimum,value);maximum=std::max(maximum,value);
            inside+=value< -1e-8;outside+=value>1e-8;nearZero+=std::abs(value)<=1e-8;}
        void Merge(const PlaneError& other){count+=other.count;inside+=other.inside;outside+=other.outside;nearZero+=other.nearZero;
            squared+=other.squared;absoluteMaximum=std::max(absoluteMaximum,other.absoluteMaximum);
            minimum=std::min(minimum,other.minimum);maximum=std::max(maximum,other.maximum);}
        double Rms() const{return count?std::sqrt(squared/double(count)):0;}
    };
    struct Counters {
        std::uint64_t active=0,uniform=0,adaptive=0,valid=0,invalid=0,clipped=0,uncertain=0;
        std::uint64_t wallInterior=0,wallInteriorValid=0,wallInteriorInvalid=0,wallBoundary=0,wallBoundaryInvalid=0;
        std::uint64_t insideSource=0,insideValidOrigin=0,unclassified=0,raySamples=0,status16=0,invalidRaySamples=0,invalidRayContract=0;
        std::array<std::uint64_t,6> reasons{},interiorReasons{};
        std::array<std::uint64_t,18> statuses{};
        PlaneError plane,interiorPlane;
    };
    std::uint64_t allInterior=0,allInteriorInvalid=0,allInsideValid=0,allInvalidRayContract=0;
    PlaneError allPlane,allInteriorPlane;
    const auto start=std::chrono::steady_clock::now();
    constexpr unsigned poseCount=16;
    const std::array<const char*,poseCount> poseNames{{"flat_wall","wall_corner","wall_corner_first_move","oval_prism_crevice",
        "wall_corner_highres","front_corner_highres","left_wall_near_grazing_forward","left_wall_near_grazing_reverse",
        "ceiling_near_grazing","back_wall_near_grazing","front_wall_near_grazing","floor_near_grazing",
        "front_left_corner_close_highres","ceiling_right_corner_close","oval_close_turn","wall_corner_third_move"}};
    for(unsigned pose=0;pose<poseCount;++pose){
        const bool highResolution=pose==4||pose==5||pose==12;
        const bool smallResolution=pose==6||pose==7||pose==9||pose==11||pose==14;
        frame.width=highResolution?2560u:smallResolution?640u:1280u;
        frame.height=highResolution?1440u:smallResolution?400u:800u;
        const char* name=poseNames[pose];
        auto camera=SealedGiRoomCameraPreset(pose==0?0:1,frame.OutputAspect());
        if(pose==0){camera.position={-.8f,2.4f,.8f};camera.view=glm::lookAt(camera.position,glm::vec3(-4,2.4f,-.5f),glm::vec3(0,1,0));}
        if(pose==2||pose==4||pose==15){const auto inverse=glm::inverse(camera.view);const auto right=glm::normalize(glm::vec3(inverse[0]));
            const auto forward=glm::normalize(-glm::vec3(inverse[2]));const float step=pose==15?3.f:1.f;camera.position+=right*(.0125f*step);
            const auto direction=glm::vec3(glm::rotate(glm::mat4(1),.004f*step,glm::vec3(0,1,0))*glm::vec4(forward,0));
            camera.view=glm::lookAt(camera.position,camera.position+direction,glm::vec3(0,1,0));}
        if(pose==3){camera.position={2.60f,1.75f,.90f};camera.view=glm::lookAt(camera.position,glm::vec3(1.18f,1.20f,-1.15f),glm::vec3(0,1,0));}
        if(pose==5){camera.position={-.4f,1.25f,.7f};camera.view=glm::lookAt(camera.position,glm::vec3(-4,.95f,3.5f),glm::vec3(0,1,0));}
        const auto look=[&](glm::vec3 eye,glm::vec3 target){camera.position=eye;camera.view=glm::lookAt(eye,target,glm::vec3(0,1,0));};
        if(pose==6)look({-3.94f,2.7f,1.5f},{-4,2.7f,-3.0f});
        if(pose==7)look({-3.94f,2.7f,-1.5f},{-4,2.7f,3.4f});
        if(pose==8)look({.4f,4.43f,2.6f},{.2f,4.5f,-3.2f});
        if(pose==9)look({2.6f,2.9f,-3.44f},{-3.8f,2.95f,-3.5f});
        if(pose==10)look({-2.6f,2.25f,3.44f},{3.8f,2.0f,3.5f});
        if(pose==11)look({-3.2f,.08f,2.7f},{2,.01f,-3.3f});
        if(pose==12)look({-3.80f,1.25f,3.25f},{-4,1.3f,3.5f});
        if(pose==13)look({3.82f,4.32f,3.26f},{4,4.5f,3.5f});
        if(pose==14)look({2.35f,1.25f,-1.2f},{1.1f,1.2f,-2.5f});
        Check(!inside(glm::dvec3(camera.position)),"Origin fixture camera entered independent closed geometry");
        frame.camera=camera;placement.reset=true;frame.effects.cameraCut=true;render();
        while(lab.ProbeSdfQualityStatistics().pending){render();
            Check(std::chrono::steady_clock::now()-start<std::chrono::seconds(150),"Origin fixture SDF preparation timed out");}
        // All following downloads refer to this one completed render. There is
        // no render, camera edit or source replacement between the downloads.
        const auto sources=lab.ReadbackScreenProbeSources();const auto probes=lab.ReadbackScreenProbePlacement();
        const auto origins=lab.ReadbackSurfaceCacheProbeOrigins();const auto rays=lab.ReadbackScreenProbeTrace(false);
        const std::size_t pixels=std::size_t(frame.width)*frame.height,slots=probes.uniform.size()+probes.spawned.size();
        Check(sources.sourceViewPositions.size()==pixels&&sources.sourceWorldNormals.size()==pixels,"Origin fixture lost receiver sources");
        Check(probes.sourceWidth==frame.width&&probes.sourceHeight==frame.height&&probes.frameIndex==placement.frameIndex,
            "Origin fixture placement/source frame mismatch");
        Check(origins.size()==slots&&slots>0&&rays.probeSlots==slots&&rays.rayResolution==4&&rays.radianceStatus.size()==slots*16,
            "Origin fixture lost probe-major origin/ray indexing");
        const auto inverseView=glm::dmat4(glm::inverse(camera.view));const auto viewProjection=glm::dmat4(camera.projection*camera.view);
        Counters result;
        for(std::size_t probe=0;probe<slots;++probe){const bool uniform=probe<probes.uniform.size();
            const auto index=uniform?probe:probe-probes.uniform.size();
            const auto pp=uniform?probes.uniform[index]:probes.spawned[index];
            const auto nn=uniform?probes.uniformNormals[index]:probes.spawnedNormals[index];
            const auto origin=origins[probe];for(unsigned channel=0;channel<4;++channel)
                Check(std::isfinite(pp[channel])&&std::isfinite(nn[channel])&&std::isfinite(origin[channel]),"Origin fixture has nonfinite probe data");
            if(pp.w<.5f){Check(origin.w==0,"Inactive probe retained safe-origin metadata");continue;}
            ++result.active;result.uniform+=uniform;result.adaptive+=!uniform;
            const int reason=int(std::lround(origin.w));Check(reason>=-5&&reason<=3&&reason!=0,"Origin fixture has an invalid origin classification");
            result.valid+=reason>0;result.invalid+=reason<0;result.clipped+=reason==2;result.uncertain+=reason==3;
            if(reason<0)++result.reasons[unsigned(-reason)];
            const auto p=glm::dvec3(pp),n=glm::normalize(glm::dvec3(nn));
            const auto direction=glm::normalize(p-glm::dvec3(camera.position));
            const auto primary=oracle.Nearest(glm::dvec3(camera.position),direction,glm::length(p-glm::dvec3(camera.position))+.02);
            const int owner=primary.triangle>=0?int(oracle.triangles[unsigned(primary.triangle)].object):-1;
            const bool sourceInside=inside(p,owner);result.insideSource+=sourceInside;
            if(reason>0)result.insideValidOrigin+=inside(glm::dvec3(origin));
            bool interior=false,wall=false;double planeError=0;glm::dvec2 screen(-1);
            if(primary.triangle>=0){const auto& triangle=oracle.triangles[unsigned(primary.triangle)];
                const auto exactPoint=glm::dvec3(camera.position)+direction*primary.t;
                planeError=glm::dot(p-exactPoint,triangle.n);result.plane.Add(planeError);
                // The exact primary triangle determines the owner. Geometric
                // plane/normal tests then reject wrong-source classifications;
                // safe-origin metadata never decides membership of this ROI.
                wall=owner>=1&&owner<=8&&glm::dot(triangle.n,n)>.99&&glm::dot(triangle.n,-direction)>.01&&
                    std::abs(glm::dot(p-exactPoint,triangle.n))<.002;
                if(wall&&!sourceInside){const auto clip=viewProjection*glm::dvec4(p,1);screen=(glm::dvec2(clip)/clip.w*.5+.5)*glm::dvec2(frame.width,frame.height)-.5;
                    const int x=int(std::lround(screen.x)),y=int(std::lround(screen.y));
                    interior=x>=9&&y>=9&&x+9<int(frame.width)&&y+9<int(frame.height);
                    // Require a complete 8-pixel screen-space geometry margin.
                    // Same-plane triangle diagonals remain ordinary interior.
                    for(int dy=-1;dy<=1&&interior;++dy)for(int dx=-1;dx<=1&&interior;++dx){if(dx==0&&dy==0)continue;
                        for(int radius=1;radius<=8;++radius){const int xx=x+dx*radius,yy=y+dy*radius;
                            const auto address=std::size_t(yy)*frame.width+unsigned(xx);const auto vp=sources.sourceViewPositions[address],normal=sources.sourceWorldNormals[address];
                            if(vp.w<.5f||normal.w<.5f||glm::dot(glm::dvec3(normal),n)<.99){interior=false;break;}
                            const auto world=glm::dvec3(inverseView*glm::dvec4(glm::dvec3(vp),1));
                            if(std::abs(glm::dot(world-exactPoint,triangle.n))>.002){interior=false;break;}
                        }
                    }
                }
            }else ++result.unclassified;
            if(interior){++result.wallInterior;result.wallInteriorValid+=reason>0;result.wallInteriorInvalid+=reason<0;result.interiorPlane.Add(planeError);
                if(reason<0)++result.interiorReasons[unsigned(-reason)];}
            else if(wall){++result.wallBoundary;result.wallBoundaryInvalid+=reason<0;}
            if(reason<0&&result.invalid<=12){std::cout<<std::setprecision(10)<<"SURFACE CACHE ORIGIN INVALID pose="<<name<<" probe="<<probe
                <<" uniform="<<uniform<<" owner="<<owner<<" wall="<<wall<<" interior="<<interior<<" source_inside_other="<<sourceInside
                <<" reason="<<reason<<" p="<<p.x<<","<<p.y<<","<<p.z<<" n="<<n.x<<","<<n.y<<","<<n.z
                <<" screen="<<screen.x<<","<<screen.y;
                if(primary.triangle>=0)std::cout<<" primary_error="<<glm::length(glm::dvec3(camera.position)+direction*primary.t-p);
                std::cout<<std::setprecision(6)<<std::endl;}
            for(unsigned ray=0;ray<16;++ray){const auto value=rays.radianceStatus[probe*16+ray];
                for(unsigned channel=0;channel<4;++channel)Check(std::isfinite(value[channel]),"Origin fixture has nonfinite ray data");
                const int status=int(std::lround(value.w));++result.raySamples;result.status16+=status==16;
                if(status>=0&&status<int(result.statuses.size()))++result.statuses[unsigned(status)];
                if(reason<0){++result.invalidRaySamples;result.invalidRayContract+=status!=16||glm::length(glm::vec3(value))>1e-8f;}
            }
        }
        const auto minimumWallSamples=std::max(std::size_t(20),pixels*100/(1280*800));
        Check(result.wallInterior>minimumWallSamples,"Origin fixture has insufficient independent ordinary-wall interior samples");
        std::cout<<"SURFACE CACHE ORIGIN pose="<<name<<" size="<<frame.width<<"x"<<frame.height<<" seed="<<trace.samplingSeed
            <<" directions=16 temporal=0 spatial=0 active="<<result.active<<" uniform="<<result.uniform<<" adaptive="<<result.adaptive
            <<" valid="<<result.valid<<" invalid="<<result.invalid<<" clipped="<<result.clipped<<" uncertain="<<result.uncertain
            <<" wall_interior="<<result.wallInterior<<" wall_interior_valid="<<result.wallInteriorValid<<" wall_interior_invalid="<<result.wallInteriorInvalid
            <<" wall_boundary="<<result.wallBoundary<<" wall_boundary_invalid="<<result.wallBoundaryInvalid
            <<" source_inside_other="<<result.insideSource<<" valid_origin_inside="<<result.insideValidOrigin
            <<" unclassified="<<result.unclassified<<" ray_samples="<<result.raySamples<<" status16="<<result.status16
            <<" invalid_ray_samples="<<result.invalidRaySamples<<" invalid_ray_contract="<<result.invalidRayContract
            <<" plane_samples="<<result.plane.count<<" plane_rms="<<result.plane.Rms()<<" plane_abs_max="<<result.plane.absoluteMaximum
            <<" plane_signed_min="<<result.plane.minimum<<" plane_signed_max="<<result.plane.maximum
            <<" plane_inside="<<result.plane.inside<<" plane_outside="<<result.plane.outside<<" plane_zero="<<result.plane.nearZero
            <<" interior_plane_rms="<<result.interiorPlane.Rms()<<" interior_plane_abs_max="<<result.interiorPlane.absoluteMaximum
            <<" interior_plane_signed_min="<<result.interiorPlane.minimum<<" interior_plane_signed_max="<<result.interiorPlane.maximum
            <<" interior_plane_inside="<<result.interiorPlane.inside<<" interior_plane_outside="<<result.interiorPlane.outside<<" interior_plane_zero="<<result.interiorPlane.nearZero;
        for(unsigned reason=1;reason<=5;++reason)std::cout<<" reason"<<reason<<"="<<result.reasons[reason]<<" interior_reason"<<reason<<"="<<result.interiorReasons[reason];
        for(unsigned status=0;status<result.statuses.size();++status)std::cout<<" status"<<status<<"="<<result.statuses[status];
        std::cout<<std::endl;
        allInterior+=result.wallInterior;allInteriorInvalid+=result.wallInteriorInvalid;allInsideValid+=result.insideValidOrigin;
        allInvalidRayContract+=result.invalidRayContract;
        allPlane.Merge(result.plane);allInteriorPlane.Merge(result.interiorPlane);
    }
    std::cout<<"SURFACE CACHE ORIGIN COMPLETE poses="<<poseCount<<" wall_interior="<<allInterior<<" wall_interior_invalid="<<allInteriorInvalid
        <<" valid_origin_inside="<<allInsideValid<<" invalid_ray_contract="<<allInvalidRayContract
        <<" plane_samples="<<allPlane.count<<" plane_rms="<<allPlane.Rms()<<" plane_abs_max="<<allPlane.absoluteMaximum
        <<" plane_inside="<<allPlane.inside<<" plane_outside="<<allPlane.outside<<" plane_zero="<<allPlane.nearZero
        <<" interior_plane_rms="<<allInteriorPlane.Rms()<<" interior_plane_abs_max="<<allInteriorPlane.absoluteMaximum
        <<" interior_plane_inside="<<allInteriorPlane.inside<<" interior_plane_outside="<<allInteriorPlane.outside<<" interior_plane_zero="<<allInteriorPlane.nearZero
        <<" elapsed_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<std::endl;
    Check(allInvalidRayContract==0,"Rejected origins must remain explicit unavailable samples");
    Check(allInsideValid==0,"A valid safe origin entered independent closed triangle geometry");
    Check(allInteriorInvalid==0,"Ordinary wall interior probes were incorrectly rejected");
}
