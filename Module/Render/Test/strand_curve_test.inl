// Runs alone with --curve-only, or before the existing complete strand suite.
// All expected curve math below uses independent cubic Hermite coefficients,
// not the production Barry-Goldman recurrence. Decimal80 cross-checks and
// committed fixtures live in output/Diana/Quality16/quality_cpu_audit.json.
{
    using namespace PipelineDetail;
    const auto curveReference=[](const std::array<glm::dvec3,4>& p,double t){
        const auto a0=p[1]-p[0],a1=p[2]-p[1],a2=p[3]-p[2];
        const double a=std::sqrt(glm::length(a0)),b=std::sqrt(glm::length(a1)),c=std::sqrt(glm::length(a2));
        const auto m0=a1+b*(a0/a-(p[2]-p[0])/(a+b));
        const auto m1=a1+b*(a2/c-(p[3]-p[1])/(b+c));
        const auto c2=-3.*p[1]-2.*m0+3.*p[2]-m1,c3=2.*p[1]+m0-2.*p[2]+m1;
        const auto position=p[1]+t*(m0+t*(c2+t*c3));
        const auto standard=(m0+t*(2.*c2+3.*t*c3))/b;
        const auto literal=standard+t*(1.-t)*(a*a1-b*a0)/(b*(a+b));
        return std::array<glm::dvec3,3>{position,literal,standard};
    };
    const auto curveControls=[](const auto& points,unsigned segment){
        const glm::dvec3 p1(points[segment].positionRadius),p2(points[segment+1].positionRadius);
        return std::array<glm::dvec3,4>{segment?glm::dvec3(points[segment-1].positionRadius):2.*p1-p2,
            p1,p2,segment+2<points.size()?glm::dvec3(points[segment+2].positionRadius):2.*p2-p1};
    };
    const auto curveUpload=[](RHIFrameEncoder& encoder,auto buffer,const void* data,std::uint32_t bytes,std::uint32_t slot){
        const auto* p=static_cast<const std::byte*>(data);
        for(std::uint32_t offset=0;offset<bytes;offset+=256)
            Require(encoder.UpdateUniformBufferBytes(buffer,p+offset,std::min(256u,bytes-offset),offset),"Upload curve probe constants");
        Require(encoder.BindUniformBuffer(buffer,slot),"Bind curve probe constants");
    };
    const auto curveRead=[&](RenderResourceHandle<RHITextureSpec> texture){
        Require(texture.IsValid(),"Missing source curve diagnostic texture");
        return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(texture);});
    };
    // Hand-known producer thresholds catch ceil/Euclidean substitutions.
    Require(StrandCurveNativeSubdivisionCount({0,0,.5f},{.5625f,0,.5f},{64,64})==2,
        "Native18pixel threshold must yield2children");
    Require(StrandCurveNativeSubdivisionCount({0,0,.5f},{.55f,0,.5f},{64,64})==1,
        "Native producer was replaced with ceil(length/9)");
    Require(StrandCurveNativeSubdivisionCount({0,0,.5f},{.28125f,.28125f,.5f},{64,64})==2,
        "Native producer lost diagonal L1 length in favor of Euclidean distance");
    Require(StrandCurveNativeSubdivisionCount({-1,-1,.5f},{1,1,.5f},{512,512})==15,
        "Native curve producer did not clamp subdivision count to15");
    Require(!StrandCurveNativeVisible({0,0,.5f},{.001f,.001f,.5f},{64,64})&&
        StrandCurveNativeVisible({0,0,.5f},{.01f,.01f,.5f},{64,64}),"Native rounded-degeneracy predicate changed");

    const std::array<std::uint32_t,6> curveLines{0,1,1,2,2,3};
    std::array<StrandVertex,4> curvePoints{{
        {{-.8f,-.2f,0,.03f},{0,1,0,0}},{{-.1f,-.1f,.2f,.04f},{0,1,0,.2f}},
        {{.3f,.65f,.1f,.05f},{0,1,0,.8f}},{{.7f,.7f,.3f,.06f},{0,1,0,1}}
    }};
    auto curveOld=curvePoints;
    curveOld[0].positionRadius=glm::vec4(-.74f,-.18f,-.03f,.03f);
    curveOld[1].positionRadius=glm::vec4(-.04f,-.14f,.19f,.04f);
    curveOld[2].positionRadius=glm::vec4(.37f,.59f,.13f,.05f);
    curveOld[3].positionRadius=glm::vec4(.72f,.68f,.29f,.06f);
    const std::array<StrandColorCoordinate,4> curveCoordinates{{{{.5f,.25f}},{{0,.25f}},{{1,.25f}},{{.5f,.25f}}}};
    StrandMesh curveMesh(device);
    Require(curveMesh.Update(curveOld,curveLines,curveCoordinates)&&curveMesh.Update(curvePoints,curveLines,curveCoordinates,curveOld),curveMesh.LastError());
    StrandRenderItem curveItem;curveItem.geometry=curveMesh.Snapshot();curveItem.material.sourceCurveGeometry=true;
    curveItem.previousTransformValid=true;curveItem.guides=true;
    curveItem.material.rootColor=curveItem.material.tipColor={.6f,.25f,.1f};curveItem.material.opacity=.8f;
    StrandPassView curveView;curveView.cameraPosition={0,0,4};curveView.previousCameraPosition={2,0,4};curveView.motionHistoryValid=true;
    StrandCurvePass curveInputPass(device);curveInputPass.Begin(1);curveInputPass.Prepare(0,curveItem,curveView,Size,Size,false);
    Require(curveInputPass.Statistics().originalSegments==3&&curveInputPass.Statistics().curvePieces==7,
        "Native CPU scheduling did not emit2+4+1realchildren (or emitted a15x degenerate budget)");
    auto curveAtlas=curveInputPass.DebugTextures();auto curveAtlasValues=curveRead(curveAtlas.atlas);
    const auto curveTexel=[](const auto& values,std::size_t index){const auto p=index*4;return glm::vec4(values[p],values[p+1],values[p+2],values[p+3]);};
    for(unsigned segment=0;segment<3;++segment){
        const glm::uvec4 expected(segment?segment-1:segment,segment,segment+1,segment+2<4?segment+2:segment+1);
        Require(glm::uvec4(curveTexel(curveAtlasValues,curveAtlas.offsets.z+segment))==expected,
            "Source four-control atlas topology/boundary extrapolation differs");
    }
    ResourceBuilder curveProbeBuilder(device);
    const auto curveProbeColor=curveProbeBuilder.Texture(14,4,RHITextureFormat::RGBA32F);
    const auto curveProbeTarget=curveProbeBuilder.Target(curveProbeColor,{});
    const auto curveProbeGrid=curveProbeBuilder.Uniform(16);
    const auto curveProbeView=curveProbeBuilder.Uniform(320),curveProbeObject=curveProbeBuilder.Uniform(64);
    const auto curveProbeMaterial=curveProbeBuilder.Uniform(224+256*16),curveProbeMotionObject=curveProbeBuilder.Uniform(80);
    const auto curveProbePipeline=[&](const std::string& vs,const std::string& fs){
        CreateGraphicShaderDesc desc;desc.vertexShaderSource=curveProbeBuilder.Source(ShaderSourceType::Vertex,vs);
        desc.fragmentShaderSource=curveProbeBuilder.Source(ShaderSourceType::Fragment,fs);
        const auto program=curveProbeBuilder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(desc,cb);});
        PipelineSpec spec;spec.shaderProgram=program;spec.depthTest=spec.depthWrite=false;spec.cullMode=CullMode::None;
        spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        return curveProbeBuilder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
    };
    const std::string curveProbeGridGLSL=R"GLSL(
layout(std140,binding=14) uniform CurveProbeGrid {uvec4 curveProbeGrid;};
vec4 CurveProbeClip(){float x=(float(gl_InstanceID)+float(gl_VertexID&1))/float(curveProbeGrid.x);
    float y=float((gl_VertexID>>1)&1);return vec4(x*2.0-1.0,y*2.0-1.0,0,1);}
)GLSL";
    const auto curveInputPipeline=curveProbePipeline(std::string("#version 450 core\n")+StrandCurveGLSL()+curveProbeGridGLSL+R"GLSL(
layout(location=0) noperspective out vec3 probePosition;
layout(location=1) noperspective out vec3 probeDirection;
layout(location=2) noperspective out vec3 probePrevious;
layout(location=3) noperspective out vec3 probeAttributes;
void main(){StrandCurveVertex node=StrandCurveInput();probePosition=node.positionRadius.xyz;
 probeDirection=node.tangentArc.xyz;probePrevious=node.previousPositionRadius.xyz;
 probeAttributes=vec3(node.t,node.tangentArc.w,abs(node.positionRadius.w));gl_Position=CurveProbeClip();}
)GLSL",R"GLSL(#version 450 core
layout(location=0) noperspective in vec3 probePosition;
layout(location=1) noperspective in vec3 probeDirection;
layout(location=2) noperspective in vec3 probePrevious;
layout(location=3) noperspective in vec3 probeAttributes;
layout(location=0) out vec4 outColor;
void main(){int row=int(gl_FragCoord.y);outColor=vec4(row==0?probePosition:row==1?probeDirection:row==2?probePrevious:probeAttributes,1);}
)GLSL");
    auto curveMotionSource=StrandMotionVertexShader(true);
    curveMotionSource.insert(curveMotionSource.find("void main()"),curveProbeGridGLSL+
        "layout(location=9) noperspective out vec4 curveProbeCurrentClip;\n");
    curveMotionSource.insert(curveMotionSource.rfind('}'),"    curveProbeCurrentClip=gl_Position;gl_Position=CurveProbeClip();\n");
    const auto curveMotionPipeline=curveProbePipeline(curveMotionSource,R"GLSL(#version 450 core
layout(location=6) in vec4 strandMotionPreviousClip;
layout(location=9) noperspective in vec4 curveProbeCurrentClip;
layout(location=0) out vec4 outColor;
void main(){outColor=vec4((strandMotionPreviousClip.xyz/strandMotionPreviousClip.w-
    curveProbeCurrentClip.xyz/curveProbeCurrentClip.w)*.5,1);}
)GLSL");
    const auto curveProbeOwner=curveProbeBuilder.Finish();
    struct alignas(16) CurveAuditView {glm::mat4 vp{1};glm::vec4 camera{0,0,4,1},viewport{64,64,0,0};
        glm::mat4 oldVP{1},unjittered{1},oldUnjittered{1};glm::vec4 oldCamera{2,0,4,0},motion{1,1,0,0};};
    static_assert(sizeof(CurveAuditView)==320);
    struct alignas(16) CurveAuditObject {glm::mat4 model{1};glm::vec4 flags{1,0,0,0};};
    static_assert(sizeof(CurveAuditObject)==80);
    unsigned curveProbeDraws=0;float curveMaximumGeometryError=0,curveMaximumMotionError=0;
    const auto curveProbeDraw=[&](bool motion,float time){
        curveView.shutterSample=time;curveInputPass.Begin(1);curveInputPass.Prepare(0,curveItem,curveView,Size,Size,false);
        Require(curveInputPass.Statistics().curvePieces==7,"Probe time unexpectedly changed the hand-known2+4+1child counts");
        auto encoder=device.BeginFrame({0,14,4});Require(encoder.KeepAlive(curveProbeOwner),"Pin source curve numeric probe");
        RHICommand::SetRenderTarget clear;clear.target=curveProbeTarget;clear.width=14;clear.height=4;
        clear.clearFlags=RHICommand::ClearColor;Require(encoder.SetRenderTarget(clear),"Clear curve probe");
        Require(encoder.BindPipeline(motion?curveMotionPipeline:curveInputPipeline),"Bind source curve probe pipeline");
        const glm::uvec4 grid(7,0,0,0);curveUpload(encoder,curveProbeGrid,&grid,sizeof(grid),14);
        if(motion){CurveAuditView constants;constants.motion.z=1.f-time;
            curveUpload(encoder,curveProbeView,&constants,sizeof(constants),0);
            const glm::mat4 identity(1);curveUpload(encoder,curveProbeObject,&identity,sizeof(identity),1);
            std::array<glm::vec4,14+256> material{};material[3]={0,1,.8f,0};material[4]={1,12,0,1};material[7]={0,1,0,0};
            curveUpload(encoder,curveProbeMaterial,material.data(),sizeof(material),2);
            const CurveAuditObject old;curveUpload(encoder,curveProbeMotionObject,&old,sizeof(old),8);}
        curveInputPass.Bind(encoder,0);curveInputPass.Draw(encoder,0);Submit(device,encoder);++curveProbeDraws;
        return curveRead(curveProbeColor);
    };
    const auto curveInputValues=curveProbeDraw(false,1);
    double curveStandardNegativeControl=0;
    for(unsigned piece=0;piece<7;++piece){
        const auto record=glm::uvec4(curveTexel(curveAtlasValues,curveAtlas.offsets.w+piece));
        Require(record.x<3&&record.y==std::array<unsigned,3>{2,4,1}[record.x]&&record.z<record.y,
            "Curve child records lost original compact ordinal/count/child provenance");
        const auto cp=curveControls(curvePoints,record.x),old=curveControls(curveOld,record.x);
        const auto a=curveReference(cp,double(record.z)/record.y),b=curveReference(cp,double(record.z+1)/record.y);
        const auto pa=curveReference(old,double(record.z)/record.y),pb=curveReference(old,double(record.z+1)/record.y);
        for(unsigned pixel=0;pixel<2;++pixel){const double weight=(double(pixel)+.5)/2.;
            const double t=(double(record.z)+weight)/record.y;
            const std::array<glm::dvec3,4> expected{glm::mix(a[0],b[0],weight),glm::mix(a[1],b[1],weight),glm::mix(pa[0],pb[0],weight),
                glm::dvec3(t,glm::mix(double(curvePoints[record.x].tangentArc.w),double(curvePoints[record.x+1].tangentArc.w),t),
                    glm::mix(double(curvePoints[record.x].positionRadius.w),double(curvePoints[record.x+1].positionRadius.w),t))};
            for(unsigned row=0;row<4;++row){const auto p=std::size_t(row*14+piece*2+pixel)*4;
                const glm::dvec3 actual(curveInputValues[p],curveInputValues[p+1],curveInputValues[p+2]);
                const auto error=glm::length(actual-expected[row]);curveMaximumGeometryError=std::max(curveMaximumGeometryError,float(error));
                Require(error<.00003*(1.+glm::length(expected[row])),"GPU CurveInput differs from independent nonuniform Hermite/literal-direction reference");
                if(row==1)curveStandardNegativeControl=std::max(curveStandardNegativeControl,glm::length(actual-glm::mix(a[2],b[2],weight)));}
        }
    }
    Require(curveStandardNegativeControl>.01,"Literal source direction and standard-derivative negative control cannot be distinguished");
    // Decode every native child/count pair1..15 through the ACTUAL consumer.
    // Only the scheduling input is explicit here; the CPU producer's floor/L1
    // rule is checked separately above and in the committed threshold audit.
    {
        constexpr unsigned total=120,width=total*2;
        std::vector<glm::vec4> atlas(curveAtlas.offsets.w+total);
        for(unsigned i=0;i<curveAtlas.offsets.w;++i)atlas[i]=curveTexel(curveAtlasValues,i);
        unsigned next=curveAtlas.offsets.w;
        for(unsigned count=1;count<=15;++count)for(unsigned child=0;child<count;++child)
            atlas[next++]=glm::vec4(1,count,child,0);
        ResourceBuilder allBuilder(device);CreateRHITextureSpec desc;desc.width=std::uint32_t(atlas.size());desc.height=1;
        desc.data=atlas.data();desc.mipmaps=false;desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
        desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::ClampToEdge;
        const auto texture=allBuilder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
        const auto constants=allBuilder.Uniform(96);const auto color=allBuilder.Texture(width,4,RHITextureFormat::RGBA32F);
        const auto target=allBuilder.Target(color,{});
        struct CurveAuditVertex {StrandVertex current;glm::vec2 coordinates{0};StrandVertex previous;};
        static_assert(sizeof(CurveAuditVertex)==72);const std::array<CurveAuditVertex,4> vertices{};
        const std::array<std::uint32_t,6> indices{0,1,2,1,3,2};CreateMeshBufferDesc mesh;
        mesh.vertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        mesh.vertexData=vertices.data();mesh.vertexCount=4;mesh.vertexByteSize=sizeof(vertices);
        mesh.indexData=indices.data();mesh.indexCount=6;mesh.indexByteSize=sizeof(indices);
        const auto quad=allBuilder.Create<VertexBufferSpec>([&](auto cb){device.async_CreateMeshBuffer(mesh,cb);});const auto owner=allBuilder.Finish();
        struct alignas(16) Layout {glm::uvec4 atlas,offsets;glm::mat4 inverse{1};};
        static_assert(sizeof(Layout)==96);const Layout layout{{std::uint32_t(atlas.size()),1,4,3},curveAtlas.offsets};
        auto encoder=device.BeginFrame({0,width,4});Require(encoder.KeepAlive(owner)&&encoder.KeepAlive(curveProbeOwner),"Pin all-count curve consumer probe");
        RHICommand::SetRenderTarget clear;clear.target=target;clear.width=width;clear.height=4;clear.clearFlags=RHICommand::ClearColor;
        Require(encoder.SetRenderTarget(clear)&&encoder.BindPipeline(curveInputPipeline)&&encoder.BindMesh(quad)&&encoder.BindTexture(texture,8),"Bind all-count curve consumer probe");
        const glm::uvec4 grid(total,0,0,0);curveUpload(encoder,curveProbeGrid,&grid,sizeof(grid),14);curveUpload(encoder,constants,&layout,sizeof(layout),13);
        RHICommand::DrawIndexed draw;draw.indexCount=6;draw.instanceCount=total;
        Require(encoder.DrawIndexed(draw),"Draw all native child/count pairs");Submit(device,encoder);++curveProbeDraws;
        const auto values=curveRead(color);const auto cp=curveControls(curvePoints,1),old=curveControls(curveOld,1);
        unsigned piece=0;
        for(unsigned count=1;count<=15;++count)for(unsigned child=0;child<count;++child,++piece){
            const auto a=curveReference(cp,double(child)/count),b=curveReference(cp,double(child+1)/count);
            const auto pa=curveReference(old,double(child)/count),pb=curveReference(old,double(child+1)/count);
            for(unsigned pixel=0;pixel<2;++pixel){const double weight=(double(pixel)+.5)/2.,t=(double(child)+weight)/count;
                const std::array<glm::dvec3,4> expected{glm::mix(a[0],b[0],weight),glm::mix(a[1],b[1],weight),glm::mix(pa[0],pb[0],weight),
                    glm::dvec3(t,glm::mix(double(curvePoints[1].tangentArc.w),double(curvePoints[2].tangentArc.w),t),
                        glm::mix(double(curvePoints[1].positionRadius.w),double(curvePoints[2].positionRadius.w),t))};
                for(unsigned row=0;row<4;++row){const auto p=std::size_t(row*width+piece*2+pixel)*4;
                    const double error=glm::length(glm::dvec3(values[p],values[p+1],values[p+2])-expected[row]);
                    Require(error<.00003*(1.+glm::length(expected[row])),"Curve consumer failed a native1..15child endpoint/attribute identity");}
            }
        }
    }
    for(float time:{1.f,.125f,.375f,.625f,.875f}){
        const auto values=curveProbeDraw(true,time);
        for(unsigned piece=0;piece<7;++piece){const auto r=glm::uvec4(curveTexel(curveAtlasValues,curveAtlas.offsets.w+piece));
            const auto cp=curveControls(curvePoints,r.x),old=curveControls(curveOld,r.x);
            const auto c0=curveReference(cp,double(r.z)/r.y)[0],c1=curveReference(cp,double(r.z+1)/r.y)[0];
            const auto p0=curveReference(old,double(r.z)/r.y)[0],p1=curveReference(old,double(r.z+1)/r.y)[0];
            for(unsigned x=0;x<2;++x){const auto expected=glm::mix(p0-c0,p1-c1,(double(x)+.5)/2.)*(.5*double(time));
                for(unsigned y=0;y<4;++y){const auto p=std::size_t(y*14+piece*2+x)*4;
                    const glm::dvec3 actual(values[p],values[p+1],values[p+2]);
                    const auto error=glm::length(actual-expected);curveMaximumMotionError=std::max(curveMaximumMotionError,float(error));
                    Require(error<.000004,"Curve MotionVS did not project previousCenter plus CURRENT filtered offset at the declared shutter time");}}
        }
    }
    curveInputPass.Shutdown();

    ResourceBuilder curveSceneBuilder(device);
    const auto curveColor=curveSceneBuilder.Texture(Size,Size,RHITextureFormat::RGBA32F),curveDepth=curveSceneBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto curveOpaque=curveSceneBuilder.Texture(Size,Size,RHITextureFormat::Depth32F),curveShadow=curveSceneBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto curveTarget=curveSceneBuilder.Target(curveColor,curveDepth),curveOpaqueTarget=curveSceneBuilder.Target({},curveOpaque),curveShadowTarget=curveSceneBuilder.Target({},curveShadow);
    const auto curveSceneOwner=curveSceneBuilder.Finish();StrandRenderPass curveScenePass(device);
    curveView=StrandPassView{};curveView.cameraPosition=curveView.previousCameraPosition={0,0,4};
    curveView.frontLayerTransparency=true;curveView.pairedLayerTransparency=true;curveView.frontDepthGroups=3;
    curveView.opaqueDepth=curveOpaque;curveView.outputStraightColor=true;curveView.outputTemporalData=true;
    curveView.lighting.lightColors.fill(glm::vec4(0));curveView.lighting.ambientAndExposure={1,1,1,0};
    float curveOpaqueValue=1;unsigned curveSceneDraws=0;
    const auto curveDraw=[&]{auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(curveSceneOwner),"Pin curve integration scene");
        RHICommand::SetRenderTarget clear;clear.target=curveOpaqueTarget;clear.width=clear.height=Size;
        clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=curveOpaqueValue;Require(encoder.SetRenderTarget(clear),"Clear independent curve opaque depth");
        clear.target=curveTarget;clear.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;clear.clearDepth=1;clear.clearColor=glm::vec4(0);
        Require(encoder.SetRenderTarget(clear),"Clear curve scene");curveScenePass.Record(encoder,std::span(&curveItem,1),{curveTarget,curveColor,curveDepth,Size,Size,1},curveView);
        Submit(device,encoder);++curveSceneDraws;return curveRead(curveColor);};
    curveItem.material.sourceCurveGeometry=false;const auto curveLegacy=curveDraw();
    Require(MaximumDifference(curveLegacy,curveDraw())==0,"Explicit curve-off changed default source ribbon pixels");
    curveItem.material.sourceCurveGeometry=true;const auto curveBeauty=curveDraw();
    Require(curveScenePass.CurveStatistics().curvePieces==7&&curveScenePass.CurveStatistics().visibleSegments==3,
        "Production beauty/reveal pipeline did not use the native2/4/1 child list");
    Require(MaximumDifference(curveLegacy,curveBeauty)>.01f,"Curved integration fixture has no measurable departure from straight chords");
    {
        const auto coverage=curveRead(curveScenePass.TemporalTextures().coverage),depth=curveRead(curveScenePass.TemporalTextures().nearestDepth);
        unsigned covered=0;for(std::size_t p=0;p<curveBeauty.size();p+=4){
            Require(std::abs(coverage[p]-curveBeauty[p+3])<.001f,"Curve beauty alpha and temporal/reveal coverage disagree");
            if(coverage[p]>.2f){Require(depth[p/4]<1,"Curve coverage is absent from nearest hair depth");++covered;}}
        Require(covered>100,"Curve integration fixture lost most of its expected visible fibers");
    }
    curveItem.material.opacity=1;curveDraw();
    {
        const auto t=curveScenePass.TemporalTextures();const auto coverage=curveRead(t.coverage),nearest=curveRead(t.nearestDepth),merged=curveRead(t.compositeDepth);
        unsigned mergedPixels=0;for(std::size_t p=0;p<coverage.size();p+=4)if(coverage[p]>.96f){
            Require(coverage[p+1]>.5f&&std::abs(merged[p/4]-nearest[p/4])<2e-6f,"High-coverage curve depth failed the source.95 merge contract");++mergedPixels;}
        Require(mergedPixels>60,"High-coverage curve depth test is vacuous");
    }
    curveItem.material.opacity=.8f;const auto cachedCurveAtlas=curveScenePass.CurveDebugTextures().atlas;
    curveOpaqueValue=.2f;const auto occluded=curveDraw();
    Require(curveScenePass.CurveStatistics().curvePieces==7&&curveScenePass.CurveDebugTextures().atlas==cachedCurveAtlas&&curveScenePass.CurveStatistics().uploads==0,
        "Opaque depth incorrectly changed the pure-hardware source child list or rebuilt its unchanged atlas");
    for(std::size_t p=3;p<occluded.size();p+=4)Require(occluded[p]==0,"Curve beauty ignored the independent opaque depth");
    curveOpaqueValue=1;curveItem.material.sourceCurveGeometry=false;
    Require(MaximumDifference(curveLegacy,curveDraw())==0,"Curve shader variants contaminated restored default source ribbons");
    curveItem.material.sourceCurveGeometry=true;
    // Raster subdivision is not a new physical groom. The raw source density
    // must remain byte-identical, even when the projected child count changes.
    {
        const auto savedItem=curveItem;curveItem.guides=false;curveItem.material.spatialVolume=true;
        curveItem.material.volumeResolution=8;curveItem.material.volumeVoxelSize=.1f;
        curveItem.material.volumeOpaqueBlockers=curveItem.material.volumeProxyBlockers=false;
        curveItem.material.sourceCurveGeometry=false;curveDraw();
        const auto textures=curveScenePass.VolumeDebugTextures();const auto density=curveRead(textures.density);
        const auto paths=curveRead(textures.paths);const auto volume=curveScenePass.VolumeStatistics();
        Require(volume.passes==4&&volume.updated==1&&volume.segments==3,
            "Curve density fixture did not voxelize the three original physical segments");
        double mass=0;for(std::size_t i=0;i<density.size();i+=4){
            Require(std::isfinite(density[i])&&density[i]>=0,"Curve density fixture contains invalid physical density");mass+=density[i];}
        Require(mass>0,"Curve density identity comparison would be vacuous for an empty field");
        const auto checkDensity=[&]{const auto current=curveScenePass.VolumeDebugTextures();
            Require(current.density==textures.density&&current.paths==textures.paths&&
                curveScenePass.VolumeStatistics().passes==0&&curveScenePass.VolumeStatistics().updated==0&&
                curveScenePass.VolumeStatistics().segments==3,
                "Curve child count/variant changed the raw density owner or repeated physical voxelization");
            Require(MaximumDifference(density,curveRead(current.density))==0&&MaximumDifference(paths,curveRead(current.paths))==0,
                "Curve child count/variant changed physical density or its unchanged optical paths");};
        curveItem.material.sourceCurveGeometry=true;curveDraw();
        Require(curveScenePass.CurveStatistics().curvePieces==7,"Density test did not enable the seven-child curve variant");checkDensity();
        curveView.viewProjection=curveView.currentUnjitteredViewProjection=glm::scale(glm::mat4(1),glm::vec3(.5f,.5f,1));
        curveDraw();Require(curveScenePass.CurveStatistics().curvePieces==4,"Density test did not change raster subdivision count");checkDensity();
        curveView.viewProjection=curveView.currentUnjitteredViewProjection=glm::mat4(1);curveItem=savedItem;
    }
    // Per-child rendering must preserve source shade identity and hidden
    // predecessor inheritance, without changing that segment's P0 control.
    {
        auto hidden=curvePoints;hidden[0].positionRadius={-3,-.5f,0,.04f};hidden[1].positionRadius={-2,-.3f,0,.04f};
        hidden[2].positionRadius={.2f,.5f,0,.04f};hidden[3].positionRadius={.6f,.6f,0,.04f};
        StrandMesh hiddenMesh(device);Require(hiddenMesh.Update(hidden,curveLines,curveCoordinates),hiddenMesh.LastError());
        const auto savedItem=curveItem;curveItem.geometry=hiddenMesh.Snapshot();curveItem.guides=false;
        curveItem.material.sourceSegmentShading=curveItem.material.sourceColor=true;
        auto gradient=std::make_shared<StrandTextureTable>();gradient->width=2;gradient->height=3;
        for(unsigned row=0;row<3;++row){gradient->texels.emplace_back(.125f,.125f,.125f,1);gradient->texels.emplace_back(.875f,.875f,.875f,1);}
        curveItem.material.gradientTable=gradient;curveView.debugMode=StrandDebugMode::BaseColor;
        const auto image=curveDraw();const auto debug=curveScenePass.CurveDebugTextures();const auto atlas=curveRead(debug.atlas);
        Require(curveScenePass.CurveStatistics().visibleSegments==2,"Hidden predecessor was not removed by the hardware endpoint predicate");
        Require(glm::uvec4(curveTexel(atlas,debug.offsets.z+1))==glm::uvec4(0,1,2,3),
            "Hidden predecessor incorrectly replaced the original P0 by endpoint extrapolation");
        for(unsigned i=0;i<debug.pieces;++i)Require(unsigned(curveTexel(atlas,debug.offsets.w+i).x)>=1,
            "Curve child list replaced original compact ordinal by visible output ordinal");
        const auto shades=curveRead(curveScenePass.SegmentShadingDebugTextures().color);
        for(unsigned c=0;c<3;++c)Require(std::abs(shades[4+c]-.875f)<.0001f&&std::abs(shades[8+c]-.875f)<.0001f,
            "Hidden-predecessor Shade[start] did not copy this segment's END shade");
        const auto stats=curveScenePass.SegmentShadingStatistics();
        Require(stats.passes==1&&stats.points<=4&&debug.pieces>stats.points,
            "Native source shading was repeated for every curve child instead of original source work");
        unsigned checked=0;for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size/2;++x){const auto p=std::size_t(y*Size+x)*4;
            if(image[p+3]<.5f)continue;++checked;for(unsigned c=0;c<3;++c)Require(std::abs(image[p+c]-.875f)<.002f,
                "Curve child color used expanded-vertex shade indices instead of original endpoint colors");}
        Require(checked>20,"Hidden-predecessor color integration test has no visible entering curve");
        curveItem=savedItem;curveView.debugMode=StrandDebugMode::Beauty;
    }
    // A different child count still has valid history of the ORIGINAL controls.
    auto translatedOld=curvePoints;for(auto& p:translatedOld)p.positionRadius+=glm::vec4(-.125f,.0625f,-.05f,0);
    Require(curveMesh.Update(translatedOld,curveLines,curveCoordinates)&&curveMesh.Update(curvePoints,curveLines,curveCoordinates,translatedOld),curveMesh.LastError());
    curveItem.geometry=curveMesh.Snapshot();curveItem.previousTransformValid=true;
    curveView.outputMotionData=curveView.motionHistoryValid=true;curveDraw();
    const auto curveCheckMotion=[&](bool scaled){const auto targets=curveScenePass.TemporalTextures();const auto motion=curveRead(targets.motion),coverage=curveRead(targets.coverage),nearest=curveRead(targets.nearestDepth);
        unsigned checked=0;for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x){const auto p=std::size_t(y*Size+x)*4;if(coverage[p]<.3f)continue;
            const glm::vec2 uv=(glm::vec2(x,y)+.5f)/float(Size);
            const glm::vec2 expected=(scaled?2.f*uv-glm::vec2(.5f):uv)+glm::vec2(-.0625f,.03125f);
            if(glm::any(glm::lessThan(expected,glm::vec2(0)))||glm::any(glm::greaterThan(expected,glm::vec2(1))))continue;
            Require(motion[p+3]>.99f,"Changing curve child count invalidated matched original-point motion history");
            Require(std::abs(motion[p]-expected.x)<.00007f&&std::abs(motion[p+1]-expected.y)<.00007f&&
                std::abs(motion[p+2]-(nearest[p/4]-.025f))<.00007f,"Production curve motion does not reproject known previous source centers/current offset");++checked;}
        Require(checked>40,"Curve motion reprojection fixture has insufficient coverage");};
    curveCheckMotion(false);
    curveView.viewProjection=curveView.currentUnjitteredViewProjection=glm::scale(glm::mat4(1),glm::vec3(.5f,.5f,1));
    curveDraw();Require(curveScenePass.CurveStatistics().curvePieces==4,"Half-scale native curve list did not change from7to4children");curveCheckMotion(true);
    curveView.viewProjection=curveView.currentUnjitteredViewProjection=glm::mat4(1);
    curveView.outputTemporalData=curveView.outputMotionData=false;
    {
        curveDraw();StrandShutterPass shutter(device);
        auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(curveSceneOwner),"Pin source curve shutter scene");
        shutter.Record(encoder,std::span(&curveItem,1),{curveTarget,curveColor,curveDepth,Size,Size,1},curveView,1,false);Submit(device,encoder);
        const auto textures=shutter.SampleTextures();std::array<std::vector<float>,4> samples;
        for(unsigned i=0;i<4;++i){samples[i]=curveRead(textures[i]);curveView.shutterSample=(float(i)+.5f)*.25f;
            Require(MaximumDifference(samples[i],curveDraw())<.003f,"Four-time curve shutter reused the wrong child list/pose or time constants");}
        const auto averaged=curveRead(shutter.PremultipliedColor());
        for(std::size_t p=0;p<averaged.size();p+=4){double alpha=0;glm::dvec3 premul(0);
            for(const auto& sample:samples){alpha+=sample[p+3]*.25;premul+=glm::dvec3(sample[p],sample[p+1],sample[p+2])*(sample[p+3]*.25);}
            Require(std::abs(averaged[p+3]-alpha)<3e-6,"Curve shutter accumulated alpha-over instead of a time mean");
            for(unsigned c=0;c<3;++c)Require(std::abs(averaged[p+c]-premul[c])<3e-6,"Curve shutter did not average premultiplied color");}
        shutter.Shutdown();curveView.shutterSample=1;
    }
    // A camera-hidden caster must use its own light-space curve list.
    curveItem.guides=false;curveItem.material.castDirectionalShadows=true;curveItem.model=glm::translate(glm::mat4(1),glm::vec3(3,0,0));
    curveDraw();Require(curveScenePass.CurveStatistics().visibleSegments==0,"Camera-hidden shadow fixture is not actually offscreen");
    {
        StrandShadowPass caster(device);auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(curveSceneOwner),"Pin curve caster target");
        RHICommand::SetRenderTarget clear;clear.target=curveShadowTarget;clear.width=clear.height=Size;clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=1;
        Require(encoder.SetRenderTarget(clear),"Clear source curve caster depth");
        const glm::mat4 baseLight=glm::translate(glm::mat4(1),glm::vec3(-3,0,0));
        const std::array<glm::mat4,4> lightViews{baseLight,
            glm::scale(glm::mat4(1),glm::vec3(.75f,.75f,1))*baseLight,
            glm::scale(glm::mat4(1),glm::vec3(.5f,.5f,1))*baseLight,
            glm::translate(glm::mat4(1),glm::vec3(.125f,-.0625f,0))*baseLight};
        Require(caster.Record(encoder,std::span(&curveItem,1),lightViews[0],{0,0,1},Size)==1,
            "Curve CSM adapter did not record the independent light-space caster");
        Require(caster.CurveStatistics().uploads==1,"First light-space curve cache entry was not uploaded exactly once");Submit(device,encoder);
        const auto shadow=curveRead(curveShadow);unsigned touched=0;for(float z:shadow){Require(std::isfinite(z),"Curve caster generated nonfinite depth");touched+=z<.999f;}
        Require(touched>30,"Camera visibility incorrectly removed the light-visible curved caster");
        const auto recordShadow=[&](const glm::mat4& vp){
            auto frame=device.BeginFrame({0,Size,Size});Require(frame.KeepAlive(curveSceneOwner),"Pin cached curve caster target");
            Require(frame.SetRenderTarget(clear)&&caster.Record(frame,std::span(&curveItem,1),vp,{0,0,1},Size)==1,
                "Record cached light-space curve caster");Submit(device,frame);return curveRead(curveShadow);};
        std::array<std::vector<float>,4> cascadeReference;cascadeReference[0]=shadow;
        for(unsigned i=1;i<4;++i){cascadeReference[i]=recordShadow(lightViews[i]);
            Require(caster.CurveStatistics().uploads==1&&caster.CurveStatistics().visibleSegments==3,
                "A distinct first-use CSM view failed to allocate its own curve scheduling cache");}
        for(int i=3;i>=0;--i){const auto image=recordShadow(lightViews[unsigned(i)]);
            Require(caster.CurveStatistics().uploads==0&&MaximumDifference(image,cascadeReference[unsigned(i)])==0,
                "Four exact light VPs failed to reuse their own immutable child lists/depth images in reverse order");}
        Require(MaximumDifference(cascadeReference[0],cascadeReference[2])>.01f,
            "Distinct cascade projection comparison is vacuous");
        const auto originalModel=curveItem.model;const auto originalGeometry=curveItem.geometry;
        curveItem.model=originalModel*glm::scale(glm::mat4(1),glm::vec3(.5f,.5f,1));
        const auto fewerPieces=recordShadow(lightViews[0]);
        Require(caster.CurveStatistics().uploads==1&&caster.CurveStatistics().curvePieces==4&&
            MaximumDifference(fewerPieces,cascadeReference[0])>.01f,
            "An unchanged light VP failed to refresh a genuinely different projected child list");
        Require(MaximumDifference(fewerPieces,recordShadow(lightViews[0]))==0&&caster.CurveStatistics().uploads==0,
            "Changed CSM child list did not settle to a reusable cache entry");
        curveItem.model=originalModel;
        Require(MaximumDifference(recordShadow(lightViews[0]),cascadeReference[0])==0&&caster.CurveStatistics().uploads==1,
            "Restoring original projected children reused the wrong CSM list");
        auto shiftedCaster=curvePoints;for(auto& point:shiftedCaster)point.positionRadius.z+=.125f;
        StrandMesh changedCaster(device);Require(changedCaster.Update(shiftedCaster,curveLines,curveCoordinates),changedCaster.LastError());
        curveItem.geometry=changedCaster.Snapshot();const auto changedReference=recordShadow(lightViews[0]);
        Require(caster.CurveStatistics().uploads==1&&caster.CurveStatistics().curvePieces==7&&
            MaximumDifference(changedReference,cascadeReference[0])>.03f,
            "Replacing the raw source with unchanged child counts failed to refresh the CSM atlas");
        curveItem.geometry=originalGeometry;
        Require(MaximumDifference(recordShadow(lightViews[0]),cascadeReference[0])==0&&caster.CurveStatistics().uploads==1,
            "Restoring original source failed to refresh the CSM atlas");
        // Several queued cascades and multiple generations of ONE atlas slot
        // must retain their own data until submission, rather than all reading
        // the last CPU-prepared texture. References above were submitted alone.
        ResourceBuilder queuedBuilder(device);
        std::array<RenderResourceHandle<RHITextureSpec>,4> queuedDepth;
        std::array<RenderResourceHandle<RenderTargetSpec>,4> queuedTarget;
        for(unsigned i=0;i<4;++i){queuedDepth[i]=queuedBuilder.Texture(Size,Size,RHITextureFormat::Depth32F);
            queuedTarget[i]=queuedBuilder.Target({},queuedDepth[i]);}
        const auto queuedOwner=queuedBuilder.Finish();auto queued=device.BeginFrame({0,Size,Size});
        Require(queued.KeepAlive(queuedOwner),"Pin queued CSM targets");
        const std::array<unsigned,4> queuedVP{0,2,0,0};
        for(unsigned i=0;i<4;++i){curveItem.geometry=i==2?changedCaster.Snapshot():originalGeometry;
            auto destination=clear;destination.target=queuedTarget[i];
            Require(queued.SetRenderTarget(destination)&&caster.Record(queued,std::span(&curveItem,1),lightViews[queuedVP[i]],{0,0,1},Size)==1,
                "Queue independent source curve cascades");
            Require(caster.CurveStatistics().uploads==(i>=2?1u:0u),
                "Queued CSM source generations did not reuse/refresh the intended exact-VP slot");}
        // An explicit cache release before submission also probes ownership:
        // every resource referenced by a recorded draw must remain pinned.
        caster.Reset();Submit(device,queued);
        for(unsigned i=0;i<4;++i){const auto& expected=i==2?changedReference:cascadeReference[queuedVP[i]];
            Require(MaximumDifference(curveRead(queuedDepth[i]),expected)==0,
                "Queued CSM cascade/source-generation resources aliased the final recorded view or expired before submission");}
        curveItem.geometry=originalGeometry;
    }
    curveScenePass.Shutdown();
    std::cout<<"Native curve isolated: "<<curveProbeDraws<<" numeric pose/motion probes + "<<curveSceneDraws
        <<" scene draws; geometry max="<<curveMaximumGeometryError<<", native-offset max="<<curveMaximumMotionError
        <<"; nonuniform/literal tangent,2+4+1 children,visibility/shade inheritance,default restore,coverage/depth,raw-density identity,count-changing motion,shutter and independent CSM passed\n";
}
