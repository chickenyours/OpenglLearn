#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/lumen_gi_shaders.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Material/pbr_material.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>
using namespace Render;
using namespace Render::PipelineDetail;
void Check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void OnThread(RHIDevice &d, F f) {
    bool done = false;
    d.async_ExecuteCode(f, [&] { done = true; });
    while (!done) {
        d.returnSystem.DrainCallbacks();
        if (!done)
            d.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
    }
}
const char *Vertex = R"(#version 450 core
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;out vec2 vUV;
void main(){gl_Position=vec4(position,0,1);vUV=uv;})";
int main(int argc, char **argv) {
    try {
        bool vulkan = argc > 1 && std::string(argv[1]) == "--vulkan",
             software = argc > 2 && std::string(argv[2]) == "--software-rays";
        Check(glfwInit(), "GLFW");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({32, 32, "Reflection transport regression", false,
                                          vulkan ? BackendType::Vulkan : BackendType::Opengl});
        Check(window.Activate(), "Window");
        RHIDevice device;
        if (vulkan) {
            Check(RegisterVulkanBackend(), "Vulkan backend");
            auto c = *window.GetRenderContextAsVulkan();
            c.hardwareRayTracing = !software;
            c.validation = true;
            device.Run(BackendType::Vulkan, c);
        } else
            device.Run(BackendType::Opengl, *window.GetRenderContextAsOpengl());
        {
            ResourceBuilder b(device);
            auto mesh = b.FullscreenMesh(), unused = mesh;
            auto reflection = b.FullscreenPipeline(Vertex, LumenReflectionShader());
            auto rt = b.Uniform(sizeof(RealtimeGiConstants)), lm = b.Uniform(sizeof(LumenGiConstants)),
                 gi = b.Uniform(sizeof(IndirectLightingConstants)),
                 sky = b.Uniform(sizeof(SkyLightConstants));
            auto points = b.Uniform(176), areas = b.Uniform(256),
                 surfaceBuffer=b.Uniform(sizeof(SurfacePassConstants));
            SurfacePassConstants surface;
            std::array<glm::vec4, 11> pointData{};
            std::array<glm::vec4, 16> areaData{};
            const auto texture = [&](unsigned w, unsigned h, const std::vector<glm::vec4> &data,
                                     RHIFilterMode filter=RHIFilterMode::Nearest,
                                     RHIAddressMode address=RHIAddressMode::Repeat) {
                CreateRHITextureSpec t;
                t.width = w;
                t.height = h;
                t.mipmaps = false;
                t.textureDataStoreType = t.textureUseType = RHITextureFormat::RGBA32F;
                t.data = data.data();
                t.filterMode=filter;
                t.addressMode=address;
                return b.Create<RHITextureSpec>([&](auto cb) { device.async_CreateTexture(t, cb); });
            };
            auto white = texture(SkyAtlasWidth, SkyAtlasHeight,
                                 std::vector<glm::vec4>(SkyAtlasWidth * SkyAtlasHeight, glm::vec4(1)));
            auto positions = texture(32, 32, std::vector<glm::vec4>(1024, glm::vec4(0, 0, -2, 1)));
            auto normals = texture(32, 32, std::vector<glm::vec4>(1024, glm::vec4(0, 0, 1, 1.5f)));
            auto depth = b.Texture(32, 32, RHITextureFormat::Depth32F),
                 color = b.Texture(32, 32, RHITextureFormat::RGBA32F);
            auto target = b.Target(color, {});
            RenderResourceHandle<RHITextureSpec> attributes{}, cacheTexture{}, incidentTexture{};
            RenderResourceHandle<RHITextureSpec> screenSource{};
            RenderResourceHandle<RHITextureSpec> resolveAttributes{},probeTexture{};
            // Match the production BRDF LUT sampler. NoV == 1 is a clamped
            // endpoint, not a repeat seam that samples grazing-incidence data.
            auto brdfTexture=texture(SkyBrdfLutSize,SkyBrdfLutSize,SkyBrdfIntegrationLut(),
                                     RHIFilterMode::Linear,RHIAddressMode::ClampToEdge);
            auto empty = BuildRealtimeGiScene({});
            auto geometry = texture(empty->Width(), empty->Height(), empty->Pixels());
            auto scene = empty;
            auto field = BuildMeshDistanceField(*scene, 32);
            auto sdf = texture(field->Width(), field->Height(), field->Pixels());
            RealtimeGiConstants rc;
            rc.geometry = {int(scene->NodeCount() * 2), 1024, int(scene->NodeCount()),
                           int(scene->TriangleCount())};
            LumenGiConstants lc;
            lc.inverseView = glm::translate(glm::mat4(1), glm::vec3(0, 0, 2));
            lc.camera = {0, 0, 2, .85f};
            lc.gather = {32, 32, 16, 0};
            lc.settings.w = 0;
            lc.reflection.x = 128;
            IndirectLightingConstants ic;
            ic.view = glm::translate(glm::mat4(1), glm::vec3(0, 0, -2));
            SkyLightConstants sc;
            sc.options = {1, 1, 1, 1};
            sc.transform = {1, 0, 0, 1};
            auto draw = [&](float roughness, unsigned coefficient,
                            RenderResourceHandle<PipelineSpec> pipeline) {
                std::vector<glm::vec4> data(1024, glm::vec4(0, 0, 1, 1 + roughness));
                bool uploaded = false;
                UpdateRHITextureDesc update{
                    0, 0, 32, 32, RHITextureFormat::RGBA32F, data.data(), data.size() * 16};
                device.async_UpdateTexture(normals, update, [&](bool ok) {
                    Check(ok, "Normal/roughness upload");
                    uploaded = true;
                });
                while (!uploaded) {
                    device.returnSystem.DrainCallbacks();
                    if (!uploaded)
                        device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
                }
                lc.reflection.w = float(coefficient);
                auto e = device.BeginFrame({0, 32, 32});
                e.SetRenderTarget({target, 32, 32});
                e.BindMesh(mesh);
                e.BindPipeline(pipeline);
                e.UpdateUniformBuffer(points, pointData);
                e.BindUniformBuffer(points, 3);
                e.UpdateUniformBuffer(areas, areaData);
                e.BindUniformBuffer(areas, 8);
                for (auto [buffer, binding, pointer, bytes] :
                     {std::tuple{rt, 15u, (const void *)&rc, uint32_t(sizeof(rc))},
                      std::tuple{lm, 10u, (const void *)&lc, uint32_t(sizeof(lc))},
                      std::tuple{gi, 9u, (const void *)&ic, uint32_t(sizeof(ic))},
                      std::tuple{surfaceBuffer,SurfacePassBinding,(const void *)&surface,uint32_t(sizeof(surface))},
                      std::tuple{sky, 11u, (const void *)&sc, uint32_t(sizeof(sc))}}) {
                    for (unsigned offset = 0; offset < bytes; offset += 256)
                        e.UpdateUniformBufferBytes(buffer, (const std::byte *)pointer + offset,
                                                   std::min(256u, bytes - offset), offset);
                    e.BindUniformBuffer(buffer, binding);
                }
                e.BindTexture(normals, 1);
                e.BindTexture(screenSource.IsValid()?screenSource:geometry,0); // World-only programs use geometry here.
                e.BindTexture(depth, 2);
                e.BindTexture(positions, 3);
                e.BindTexture(attributes, 4);
                e.BindTexture(cacheTexture, 5);
                e.BindTexture(geometry, 6);
                if(resolveAttributes.IsValid())e.BindTexture(resolveAttributes,7);
                e.BindTexture(sdf, 8);
                e.BindTexture(incidentTexture,9);
                e.BindTexture(white, 10);
                e.BindTexture(brdfTexture,11);
                if(probeTexture.IsValid())e.BindTexture(probeTexture,15);
                e.DrawIndexed({3});
                e.End(false);
                bool done = false;
                Check(device.async_SubmitFrameCommands(e.GetCommandBuffer(), [&] { done = true; }), "Submit");
                while (!done) {
                    device.returnSystem.DrainCallbacks();
                    if (!done)
                        device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
                }
                std::vector<float> pixels;
                OnThread(device, [&] {
                    if (vulkan)
                        pixels = device.BackendDiagnostics()->ReadTexture(color);
                    else {
                        auto *spec = device.GetResourcePool().TextureTable.Get(color);
                        pixels.resize(4096);
                        glGetTextureImage(spec->rhi_id, 0, GL_RGBA, GL_FLOAT, int(pixels.size() * 4),
                                          pixels.data());
                        Check(glGetError() == GL_NO_ERROR, "Reflection GL error");
                    }
                });
                return pixels;
            };
            for (float roughness : {.05f, .35f, .7f, .71f, .95f, 1.f}) {
                auto a = draw(roughness, 0, reflection), c = draw(roughness, 1, reflection);
                double measured = 0;
                for (unsigned i = 0; i < 1024; ++i)
                    measured += a[i * 4] + c[i * 4];
                measured /= 1024;
                // Independent high-resolution integration at normal incidence.
                // GGX NDF and VNDF coincide here; below-horizon samples contribute 0.
                double reference = 0, alpha = roughness * roughness;
                for (unsigned i = 0; i < 65536; ++i) {
                    double u = (i + .5) / 65536, cosH2 = (1 - u) / (1 + (alpha * alpha - 1) * u),
                           cosL = 2 * cosH2 - 1;
                    if (cosL <= 0)
                        continue;
                    double lambda =
                        .5 * (std::sqrt(1 + alpha * alpha * (1 - cosL * cosL) / (cosL * cosL)) - 1);
                    reference += 1 / (1 + lambda);
                }
                reference /= 65536;
                std::cout << "GGX roughness=" << roughness << " energy=" << measured
                          << " reference=" << reference << '\n';
                Check(std::abs(measured - reference) < .018, "Reflection BRDF/PDF energy mismatch");
            }
            // Production screen-ray traversal retains 32x32 geometric depth
            // while HDR hit color can use a smaller, non-square source. An
            // independent bilinear/footprint reference catches full-depth
            // texel indices accidentally applied to that smaller color image.
            const auto savedProjection=ic.projection;const auto savedTrace=lc.trace;
            ic.projection=glm::perspective(glm::radians(50.f),1.f,.1f,100.f);lc.trace.x=.8f;
            auto screenSampling=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){vec2 uv=mix(vec2(.15),vec2(.85),vUV);
vec3 origin=vec3((uv*2-1)*1.9/vec2(giProjection[0][0],giProjection[1][1]),-1.9);
vec3 radiance,hit;bool found=LmScreenRay(origin,normalize(origin),radiance,hit);
outColor=vec4(radiance,found?1:0);})");
            auto screenCoordinates=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){vec2 uv=mix(vec2(.15),vec2(.85),vUV);
vec3 origin=vec3((uv*2-1)*1.9/vec2(giProjection[0][0],giProjection[1][1]),-1.9);
vec3 radiance,hit;bool found=LmScreenRay(origin,normalize(origin),radiance,hit);ivec2 pixel=ivec2(-1);
if(found)GIProject((inverse(lmInverseView)*vec4(hit,1)).xyz,pixel);
outColor=vec4(vec2(pixel),vUV);})");
            for(const auto size:{glm::uvec2(32,32),glm::uvec2(8,8),glm::uvec2(13,7)})for(unsigned kind=0;kind<3;++kind) {
                std::vector<glm::vec4> sourcePixels(size.x*size.y);
                for(unsigned y=0;y<size.y;++y)for(unsigned x=0;x<size.x;++x) {
                    const float u=(float(x)+.5f)/size.x,v=(float(y)+.5f)/size.y;
                    sourcePixels[y*size.x+x]=kind==0?glm::vec4(2.3f,.7f,1.4f,1):
                        kind==1?glm::vec4(.2f+2*u,.3f+1.5f*v,.4f+.6f*u+.8f*v,1):
                        glm::vec4((x+y)%2?2.5f:.1f,x%3?.4f:1.7f,y%2?1.1f:.2f,1);
                }
                screenSource=texture(size.x,size.y,sourcePixels,RHIFilterMode::Linear,RHIAddressMode::ClampToEdge);
                const auto sampled=draw(.5f,0,screenSampling);
                const auto bilinear=[&](double u,double v) {
                    const double sx=u*size.x-.5,sy=v*size.y-.5;
                    const int x=int(std::floor(sx)),y=int(std::floor(sy));const double fx=sx-x,fy=sy-y;
                    const auto at=[&](int px,int py) {return glm::dvec3(sourcePixels[
                        std::clamp(py,0,int(size.y)-1)*size.x+std::clamp(px,0,int(size.x)-1)]);};
                    const std::array colors{at(x,y),at(x+1,y),at(x,y+1),at(x+1,y+1)};
                    const std::array weights{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
                    glm::dvec3 value(0),error(0),lo=colors[0],hi=colors[0];
                    for(const auto& color:colors){lo=glm::min(lo,color);hi=glm::max(hi,color);}
                    const auto midpoint=(lo+hi)*.5;
                    double quantizedWeightSum=0;
                    for(unsigned i=0;i<4;++i) {
                        value+=colors[i]*weights[i];
                        // Fixed-function filtering is not double precision.
                        // The observed NVIDIA result matches nearest 1/256
                        // bilinear product weights exactly. This is an explicit
                        // fixture allowance, not a universal API guarantee:
                        // Vulkan core permits four subtexel bits (eight in 1.4
                        // / Roadmap 2022), and GL exposes no matching minimum.
                        // https://docs.vulkan.org/refpages/latest/refpages/source/Required_Limits.html
                        // https://docs.nvidia.com/cuda/archive/12.5.0/cuda-c-programming-guide/index.html#linear-filtering
                        const double quantized=std::round(weights[i]*256)/256;
                        quantizedWeightSum+=quantized;
                        error+=glm::abs(colors[i]-midpoint)*std::abs(quantized-weights[i]);
                    }
                    // The chosen dyadic UVs conserve the weight sum under this
                    // model. Subtracting the midpoint then bounds only local
                    // color differences: |sum(deltaW*(color-midpoint))|.
                    Check(std::abs(quantizedWeightSum-1)<1e-12,"Filter precision fixture does not conserve DC weights");
                    return std::pair{value,error};
                };
                for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
                    const int px=int(std::floor((.15+.7*(double(x)+.5)/32)*32));
                    const int py=int(std::floor((.15+.7*(double(y)+.5)/32)*32));
                    glm::dvec3 expected(0),filterError(0);
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                        const auto [value,error]=bilinear((px+dx+.5)/32.,(py+dy+.5)/32.);
                        const double weight=double((dx==0?2:1)*(dy==0?2:1))/16.;
                        expected+=value*weight;filterError+=error*weight;
                    }
                    const unsigned at=(y*32+x)*4;
                    Check(sampled[at+3]>.99f,"Screen source extent changed full-depth ray hit detection");
                    // Constant/affine signals keep the original tight check.
                    // Checker tolerances vary by pixel/channel with the actual
                    // four local texels and are zero when weights are exact.
                    for(unsigned c=0;c<3;++c)if(std::abs(sampled[at+c]-expected[c])>=.001+(kind==2?filterError[c]:0)) {
                        const auto coordinates=draw(.5f,0,screenCoordinates);
                        std::cerr<<"Screen source mismatch size="<<size.x<<'x'<<size.y<<" kind="<<kind
                                 <<" output="<<x<<','<<y<<" channel="<<c<<" actual="<<sampled[at+c]
                                 <<" expected="<<expected[c]<<" expected hit pixel="<<px<<','<<py
                                 <<" filter error bound="<<(kind==2?filterError[c]:0)
                                 <<" GPU hit pixel="<<coordinates[at]<<','<<coordinates[at+1]
                                 <<" GPU UV="<<coordinates[at+2]<<','<<coordinates[at+3]<<'\n';
                        Check(false,"Cross-size HDR screen-hit color disagrees with independent normalized-UV sampling");
                    }
                }
            }
            screenSource={};ic.projection=savedProjection;lc.trace=savedTrace;
            std::cout<<"Screen-hit color: full, quarter and odd non-square HDR sources preserve UV/footprint sampling\n";
            std::array<ProbeTriangle, 2> emitter{{{{-100, -100, 1},
                                                   {100, -100, 1},
                                                   {100, 100, 1},
                                                   glm::vec3(0),
                                                   {8, 0, 0},
                                                   true,
                                                   glm::vec3(0),
                                                   1},
                                                  {{-100, -100, 1},
                                                   {100, 100, 1},
                                                   {-100, 100, 1},
                                                   glm::vec3(0),
                                                   {8, 0, 0},
                                                   true,
                                                   glm::vec3(0),
                                                   1}}};
            scene = BuildRealtimeGiScene(emitter);
            geometry = texture(scene->Width(), scene->Height(), scene->Pixels());
            field = BuildMeshDistanceField(*scene, 32);
            sdf = texture(field->Width(), field->Height(), field->Pixels());
            rc.geometry = {int(scene->NodeCount() * 2), 1024, int(scene->NodeCount()),
                           int(scene->TriangleCount())};
            lc.atlas = {1024, 0, 2, 2};
            sc.options.x = 0;
            // Supply an actual Surface Cache: this fixture emits red from both
            // triangles; the offscreen ray must retain it even at roughness > .7.
            std::vector<glm::vec4> attributePixels(1024, glm::vec4(0));
            attributePixels[0] = {0, 1, 0, 0};
            attributePixels[1] = {1, 1, 0, 0};
            attributes = texture(1024, 1, attributePixels);
            std::vector<glm::vec4> cache(1024, glm::vec4(0));
            cache[0] = cache[1] = {8, 0, 0, 1};
            cacheTexture = texture(1024, 1, cache);
            // Extend the production source with fixed cache bindings via the same
            // fixture encoder. Geometry ray results are tested separately below.
            auto rayShader = b.FullscreenPipeline(Vertex, LumenScreenPreamble() + R"(
void main(){vec3 n;int triangle;bool front;float distance=30;bool hit=LmIntersect(vec3(0,0,0),normalize(vec3((gl_FragCoord.x-16)*.05,0,1)),distance,n,triangle,front);outColor=vec4(hit?distance:0,float(triangle),front?1:0,1);})");
            lc.fieldOrigin = glm::vec4(field->Origin(), 0);
            lc.fieldSpacing = glm::vec4(field->Spacing(), 0);
            lc.fieldCounts = glm::ivec4(glm::ivec3(field->Counts()), 1024);
            auto df = draw(.95f, 0, rayShader);
            lc.fieldCounts.x = 0;
            auto exact = draw(.95f, 0, rayShader);
            for (size_t i = 0; i < df.size(); ++i)
                Check(std::abs(df[i] - exact[i]) < 1e-4, "Distance field refinement missed a thin wall");
            // Use the production world-hit shader with an actual emissive cache.
            auto emitted = b.FullscreenPipeline(Vertex, LumenReflectionShader());
            // Reverse winding to exercise a front-facing hit and Surface Cache.
            for (auto &t : emitter)
                std::swap(t.b, t.c);
            scene = BuildRealtimeGiScene(emitter);
            geometry = texture(scene->Width(), scene->Height(), scene->Pixels());
            rc.geometry = {int(scene->NodeCount() * 2), 1024, int(scene->NodeCount()), 2};
            auto red = draw(.95f, 0, emitted);
            double mean = 0;
            for (unsigned i = 0; i < 1024; ++i)
                mean += red[i * 4];
            mean /= 1024;
            Check(mean > .8, "Very rough surfaces lost offscreen geometry reflections");
            for (auto &t : emitter) {
                t.emission = glm::vec3(0);
                t.specularF0 = glm::vec3(.9f);
                t.roughness = .2f;
            }
            scene = BuildRealtimeGiScene(emitter);
            geometry = texture(scene->Width(), scene->Height(), scene->Pixels());
            std::fill(cache.begin(), cache.end(), glm::vec4(0));
            cacheTexture = texture(1024, 1, cache);
            rc.sunDirectionIntensity = {0, 0, 1, 1};
            rc.sunColor = glm::vec4(1);
            auto metal = draw(.05f, 0, emitted);
            double metalMean = 0;
            for (unsigned i = 0; i < 1024; ++i)
                metalMean += metal[i * 4];
            metalMean /= 1024;
            Check(metalMean > 1, "Offscreen metal hit lighting lost specular material response");
            std::cout << "Offscreen metal direct specular=" << metalMean << '\n';
            std::cout << "Offscreen rough red reflection=" << mean
                      << "; distance-field/BVH agreement passed\n";
            // Raster reconstruction can straddle its receiver by a fraction
            // of a millimetre. Exercise both signs, independently of TAA and
            // image denoising, and retain clearance to a close contact plane.
            std::vector<ProbeTriangle> receivers(2);
            receivers[0].a={-1,-1,0};receivers[0].b={1,-1,0};receivers[0].c={1,1,0};
            receivers[1].a={-1,-1,0};receivers[1].b={1,1,0};receivers[1].c={-1,1,0};
            const auto receiverScene=[&]{
                scene=BuildRealtimeGiScene(receivers);geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
                rc.geometry={int(scene->NodeCount()*2),1024,int(scene->NodeCount()),int(scene->TriangleCount())};
            };
            receiverScene();lc.trace.y=.015f;
            auto originShader=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){vec3 p=vec3(0,0,(gl_FragCoord.x-16)*.00001),N=normalize(vec3((gl_FragCoord.y-16)*.025,0,1));
vec3 origin=LmRayOrigin(p,N),n;int triangle;bool front;float distance=1;
bool hit=RtIntersect(origin,N,distance,n,triangle,front);outColor=vec4(origin.z,hit?distance:0,0,1);})");
            auto escaped=draw(.5f,0,originShader);
            for(unsigned i=0;i<1024;++i)Check(escaped[i*4]>0&&escaped[i*4+1]==0,"Reconstructed receiver trapped ray origin behind its own surface");
            for(unsigned i=0;i<2;++i){auto t=receivers[i];t.a.z=t.b.z=t.c.z=.004f;std::swap(t.b,t.c);receivers.push_back(t);}
            receiverScene();auto contact=draw(.5f,0,originShader);
            for(unsigned i=0;i<1024;++i)Check(contact[i*4]>0&&contact[i*4]<.004f&&contact[i*4+1]>0,"Receiver origin crossed a nearby contact surface");
            std::cout << "Receiver reconstruction precision and contact clearance passed\n";
            // Test the production next-event estimator against independent area
            // integration, rather than a second copy of the projected-angle code.
            std::array<ProbeTriangle,2> rectangle{};
            rectangle[0].a={-1,-1,1};rectangle[0].b={1,1,1};rectangle[0].c={1,-1,1};
            rectangle[1].a={-1,-1,1};rectangle[1].b={-1,1,1};rectangle[1].c={1,1,1};
            for(auto& t:rectangle){t.diffuseReflectance=glm::vec3(0);t.emission=glm::vec3(1);}
            const auto emitterScene=[&]{
                auto asset=BuildLumenGiScene(rectangle,.5f);scene=asset->Geometry();
                geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
                attributes=texture(LumenSurfaceWidth,asset->AttributeHeight(),asset->Attributes());
                rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),int(scene->TriangleCount())};
                lc.atlas={int(LumenSurfaceWidth),int(asset->SurfelCount()),int(asset->EmissiveTriangleCount()),int(scene->TriangleCount())};
                lc.fieldCounts.x=0;return asset;
            };
            auto nextEvent=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){outColor=vec4(LmEmissiveIncident(vec3(0),vec3(0,0,1),-1),1);})");
            auto horizonEvent=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){outColor=vec4(LmEmissiveIncident(vec3(0),normalize(vec3(1,0,.2)),-1),1);})");
            const auto energyMean=[](const auto& pixels){double value=0;for(unsigned i=0;i<1024;++i)value+=pixels[i*4];return value/1024;};
            emitterScene();auto emissionEnergy=energyMean(draw(.5f,0,nextEvent));
            const auto areaReference=[](glm::dvec3 normal){
                double result=0;constexpr unsigned resolution=512;
                for(unsigned y=0;y<resolution;++y)for(unsigned x=0;x<resolution;++x){
                    glm::dvec3 delta{-1+2*(double(x)+.5)/resolution,-1+2*(double(y)+.5)/resolution,1};
                    const double squared=glm::dot(delta,delta);result+=std::max(0.0,glm::dot(normal,delta))/(squared*squared);
                }
                return result*(4.0/(resolution*resolution*3.141592653589793));
            };
            const double emissionReference=areaReference({0,0,1});
            Check(std::abs(emissionEnergy-emissionReference)<.001,"Emissive next-event energy disagrees with independent area integration");
            const auto horizonEnergy=energyMean(draw(.5f,0,horizonEvent));
            Check(std::abs(horizonEnergy-areaReference(glm::normalize(glm::dvec3(1,0,.2))))<.001,"Emitter horizon clipping changed energy");
            for(auto& t:rectangle)std::swap(t.b,t.c);emitterScene();
            Check(energyMean(draw(.5f,0,nextEvent))<1e-6,"One-sided emitter illuminated its back side");
            for(auto& t:rectangle)t.twoSidedEmission=true;emitterScene();
            Check(std::abs(energyMean(draw(.5f,0,nextEvent))-emissionReference)<.001,"Two-sided emitter lost its back-facing energy");
            for(auto& t:rectangle)t.analyticEmission=true;emitterScene();
            Check(energyMean(draw(.5f,0,nextEvent))<1e-6,"Analytic owned emitter was integrated twice");
            std::cout<<"Emissive projected energy="<<emissionEnergy<<" reference="<<emissionReference<<"; horizon, sidedness and analytic ownership passed\n";
            // Production rough reflection reuse must retain the GGX DFG energy
            // for a white incident world cache, independently of traced rays.
            receivers.resize(2);receiverScene();auto receiverAsset=BuildLumenGiScene(receivers,.5f);scene=receiverAsset->Geometry();
            geometry=texture(scene->Width(),scene->Height(),scene->Pixels());attributes=texture(LumenSurfaceWidth,receiverAsset->AttributeHeight(),receiverAsset->Attributes());
            rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),int(scene->TriangleCount())};
            lc.atlas={int(LumenSurfaceWidth),int(receiverAsset->SurfelCount()),0,int(scene->TriangleCount())};
            incidentTexture=texture(LumenSurfaceWidth,receiverAsset->CacheHeight(),std::vector<glm::vec4>(LumenSurfaceWidth*receiverAsset->CacheHeight(),glm::vec4(1,1,1,0)));
            lc.fieldSpacing.w=.4f;sc.options.x=0;rc.sunDirectionIntensity.w=0;
            for(float roughness:{.7f,.95f,1.f}) {
                auto a=draw(roughness,0,reflection),c=draw(roughness,1,reflection);double measured=energyMean(a)+energyMean(c),reference=0;
                double alpha=roughness*roughness;
                for(unsigned i=0;i<65536;++i){double u=(i+.5)/65536,cosH2=(1-u)/(1+(alpha*alpha-1)*u),cosL=2*cosH2-1;
                    if(cosL>0)reference+=1/(1+.5*(std::sqrt(1+alpha*alpha*(1-cosL*cosL)/(cosL*cosL))-1));}
                reference/=65536;
                std::cout<<"Cached reflection roughness="<<roughness<<" energy="<<measured<<" reference="<<reference<<'\n';
                Check(std::abs(measured-reference)<.025,"Cached reflection changed GGX white-field energy");
            }
            auto incidentFilter=b.FullscreenPipeline(Vertex,LumenIncidentFilterShader());
            auto incidentLookup=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){vec3 p=vec3((gl_FragCoord.x-16)*.025,(gl_FragCoord.y-16)*.025,0);
outColor=LmIncidentLookup(lmCache,lmAttributes,int(lmReflection.w),p);})");
            const auto& receiverAttributes=receiverAsset->Attributes();const unsigned surfels=receiverAsset->SurfelCount();
            Check(surfels<=32,"Incident cache fixture exceeded single-row encoder readback coverage");
            std::vector<glm::vec4> incidentPixels(LumenSurfaceWidth*receiverAsset->CacheHeight(),glm::vec4(.6f,.4f,.2f,.3f));
            cacheTexture=texture(LumenSurfaceWidth,receiverAsset->CacheHeight(),incidentPixels);
            auto constant=draw(.5f,0,incidentFilter);
            for(unsigned i=0;i<surfels;++i)for(unsigned channel=0;channel<4;++channel)
                Check(std::abs(constant[i*4+channel]-incidentPixels[i][channel])<1e-5,"Incident filtering changed a constant field");
            // Positive interpolation cannot extrapolate an affine value beyond
            // the sample hull. Track its effective world-space centroid with
            // coordinate signals, then independently check the affine identity
            // F(centroid), and a displacement bound in units of sample support.
            for(unsigned i=0;i<surfels;++i) {
                const auto p=glm::vec3(receiverAttributes[2+i*2]);incidentPixels[i]={p.x+2,p.y+2,1,.3f};
            }
            cacheTexture=texture(LumenSurfaceWidth,receiverAsset->CacheHeight(),incidentPixels);
            const auto filterCentroid=draw(.5f,0,incidentFilter);
            const auto lookupCentroidA=draw(.5f,0,incidentLookup),lookupCentroidB=draw(.5f,1,incidentLookup);
            for(unsigned i=0;i<surfels;++i) {
                const auto p=glm::vec3(receiverAttributes[2+i*2]);incidentPixels[i]={.6f+.15f*p.x,.4f+.1f*p.y,.2f,.3f};
            }
            cacheTexture=texture(LumenSurfaceWidth,receiverAsset->CacheHeight(),incidentPixels);
            auto linear=draw(.5f,0,incidentFilter);unsigned interior=0;
            for(unsigned i=0;i<surfels;++i) {
                const auto p=glm::vec3(receiverAttributes[2+i*2]);const float radius=std::sqrt(2.f)/3;
                if(std::abs(p.x)+radius>=.999f||std::abs(p.y)+radius>=.999f)continue;++interior;
                const glm::vec2 centroid{filterCentroid[i*4]-2,filterCentroid[i*4+1]-2};
                Check(glm::length(centroid-glm::vec2(p))<radius,"Positive world filter displaced a receiver beyond one sample step");
                Check(std::abs(linear[i*4]-(.6f+.15f*centroid.x))<.0001f&&
                      std::abs(linear[i*4+1]-(.4f+.1f*centroid.y))<.0001f&&
                      std::abs(linear[i*4+2]-.2f)<.0001f&&std::abs(linear[i*4+3]-.3f)<.0001f,
                      "World incident filter broke affine transport at its effective sampling centroid");
            }
            Check(interior>0,"Linear incident filter fixture missed interior shared-edge samples");
            auto linearA=draw(.5f,0,incidentLookup),linearB=draw(.5f,1,incidentLookup);
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
                const unsigned index=(y*32+x)*4;const float px=(float(x)+.5f-16)*.025f,py=(float(y)+.5f-16)*.025f;
                for(unsigned owner=0;owner<2;++owner) {
                    // Production resolve only accepts the actual containing
                    // triangle. Arbitrary extrapolation through an unrelated
                    // receiver on the other side of the seam is not transport.
                    if(owner==0?py>px:py<px)continue;
                    const auto& coordinate=owner==0?lookupCentroidA:lookupCentroidB;
                    const auto& value=owner==0?linearA:linearB;
                    const glm::vec2 centroid{coordinate[index]-2,coordinate[index+1]-2};
                    Check(glm::length(centroid-glm::vec2(px,py))<.5f,"Positive mesh-edge reconstruction exceeded its sample footprint");
                    Check(std::abs(value[index]-(.6f+.15f*centroid.x))<.0001f&&std::abs(value[index+1]-(.4f+.1f*centroid.y))<.0001f,
                          "Positive world lookup broke affine transport at its effective sampling centroid");
                }
                if(x==y)Check(std::abs(linearA[index]-linearB[index])<.0001f,"Affine illumination is discontinuous at the coplanar seam");
            }
            for(unsigned i=0;i<surfels;++i)incidentPixels[i]=glm::vec4(receiverAttributes[2+i*2].w<.5f?.2f:.8f,0,0,.3f);
            cacheTexture=texture(LumenSurfaceWidth,receiverAsset->CacheHeight(),incidentPixels);
            auto seamA=draw(.5f,0,incidentLookup),seamB=draw(.5f,1,incidentLookup);
            for(unsigned i=0;i<32;++i){const unsigned at=(i*32+i)*4;
                Check(std::abs(seamA[at]-.5f)<1e-5&&std::abs(seamB[at]-.5f)<1e-5,"Coplanar cache edge does not meet continuously with equal weights");}
            auto radianceLookup=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){vec3 p=vec3((gl_FragCoord.x-16)*.025,(gl_FragCoord.y-16)*.025,0);
outColor=LmRadianceLookup(lmCache,lmAttributes,int(lmReflection.w),p);})");
            auto radianceA=draw(.5f,0,radianceLookup),radianceB=draw(.5f,1,radianceLookup);
            for(unsigned i=0;i<32;++i){const unsigned at=(i*32+i)*4;
                Check(std::abs(radianceA[at]-.5f)<1e-5&&std::abs(radianceB[at]-.5f)<1e-5,
                      "Same-material radiance has a triangulation seam");}
            auto paintedGeometry=scene->Pixels();paintedGeometry[scene->NodeCount()*2+5+3]=glm::vec4(.2f,.4f,.6f,0);
            geometry=texture(scene->Width(),scene->Height(),paintedGeometry);
            radianceA=draw(.5f,0,radianceLookup);radianceB=draw(.5f,1,radianceLookup);
            for(unsigned i=0;i<32;++i){const unsigned at=(i*32+i)*4;
                Check(std::abs(radianceA[at]-.2f)<1e-5&&std::abs(radianceB[at]-.8f)<1e-5,
                      "Radiance reconstruction blurred a painted material boundary");}
            geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
            std::cout<<"Radiance reconstruction joins equal materials and preserves painted boundaries\n";
            std::array<ProbeTriangle,2> cornerTriangles{receivers[0],receivers[1]};
            cornerTriangles[1].a={-1,-1,0};cornerTriangles[1].b={-1,-1,2};cornerTriangles[1].c={1,-1,0};
            auto cornerAsset=BuildLumenGiScene(cornerTriangles,.5f);scene=cornerAsset->Geometry();
            geometry=texture(scene->Width(),scene->Height(),scene->Pixels());attributes=texture(LumenSurfaceWidth,cornerAsset->AttributeHeight(),cornerAsset->Attributes());
            rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),int(scene->TriangleCount())};
            lc.atlas={int(LumenSurfaceWidth),int(cornerAsset->SurfelCount()),0,int(scene->TriangleCount())};
            incidentPixels.assign(LumenSurfaceWidth*cornerAsset->CacheHeight(),glm::vec4(0));
            for(unsigned i=0;i<cornerAsset->SurfelCount();++i)incidentPixels[i]=glm::vec4(cornerAsset->Attributes()[2+i*2].w<.5f?.2f:.8f,0,0,.3f);
            cacheTexture=texture(LumenSurfaceWidth,cornerAsset->CacheHeight(),incidentPixels);
            auto cornerFiltered=draw(.5f,0,incidentFilter);
            for(unsigned i=0;i<cornerAsset->SurfelCount();++i)Check(std::abs(cornerFiltered[i*4]-incidentPixels[i].r)<1e-5,"Incident spatial filter leaked light around a wall corner");
            std::cout<<"Incident cache filtering: constant/linear transport, cross-edge lookup continuity and seam blending passed\n";
            // A linear fixture cannot expose the old extrapolation bug: its
            // independently chosen boundary triangles agree by coincidence.
            // Probe a nonlinear field on both sides of every outer-strip grid
            // line, including the diagonal hull beyond its last sample row.
            const ProbeTriangle boundaryTriangle{{0,0,0},{8,0,0},{0,8,0},glm::vec3(.5f)};
            auto boundaryAsset=BuildLumenGiScene(std::span(&boundaryTriangle,1),.75f);scene=boundaryAsset->Geometry();
            Check(boundaryAsset->Attributes()[0].y==8,"Nonlinear boundary regression requires an eight-cell lattice");
            geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
            // A compact atlas exercises all 64 surfels in the existing 32x32
            // GPU target; indexing is otherwise exactly the production layout.
            attributes=texture(32,unsigned(boundaryAsset->Attributes().size()/32),boundaryAsset->Attributes());
            rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),1};
            lc.atlas={32,int(boundaryAsset->SurfelCount()),0,1};
            incidentPixels.assign(64,glm::vec4(0));
            // Independent light transport is additive and order preserving.
            // R is a constant lamp, G is a nonnegative high-contrast beam,
            // B contains their sum. A signed reconstruction followed by a
            // zero clamp can make B darker than R even though every input
            // sample of B is at least as bright as the corresponding R.
            const auto positiveLookup=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){vec2 uv=gl_FragCoord.xy/32.;vec3 p=vec3(8*uv.x,8*(1-uv.x)*uv.y,0);
if(lmReflection.w==1)outColor=LmIncidentLinearBase(lmCache,lmAttributes,0,p);
else if(lmReflection.w==2)outColor=LmRadianceLookup(lmCache,lmAttributes,0,p);
else outColor=LmIncidentBase(lmCache,lmAttributes,0,p);})");
            for(unsigned i=0;i<64;++i) {
                const auto p=glm::vec3(boundaryAsset->Attributes()[1+i*2]);
                const float beam=(p.x<1.4f||p.y>4.2f)?24.f:0.f;
                incidentPixels[i]={1,beam,1+beam,.3f};
            }
            cacheTexture=texture(32,2,incidentPixels);
            const auto checkPositiveTransport=[&](const auto& pixels,unsigned count,const char* stage) {
                double worst=0;
                for(unsigned i=0;i<count;++i) {
                    const float base=pixels[i*4],beam=pixels[i*4+1],sum=pixels[i*4+2];
                    worst=std::max(worst,double(std::abs(sum-base-beam)));
                    Check(std::abs(base-1)<.0001f,"Positive reconstruction changed a constant lamp");
                    Check(beam>=0&&beam<=24.0001f,"Light reconstruction overshot a nonnegative source range");
                    Check(sum>=base-.0001f&&std::abs(sum-base-beam)<.0002f,
                          "Adding a nonnegative light darkened a receiver or broke superposition");
                }
                std::cout<<stage<<" nonnegative-light superposition error="<<worst<<'\n';
            };
            checkPositiveTransport(draw(.5f,0,positiveLookup),1024,"Boundary reconstruction");
            checkPositiveTransport(draw(.5f,1,positiveLookup),1024,"Boundary fallback");
            checkPositiveTransport(draw(.5f,2,positiveLookup),1024,"Outgoing radiance");
            checkPositiveTransport(draw(.5f,0,incidentFilter),64,"World incident filter");
            for(unsigned i=0;i<64;++i) {
                const auto p=glm::vec3(boundaryAsset->Attributes()[1+i*2]);incidentPixels[i]={p.x,p.y,1,.3f};
            }
            cacheTexture=texture(32,2,incidentPixels);
            const auto boundaryFilterCentroid=draw(.5f,0,incidentFilter);
            const auto boundaryLookupCentroid=draw(.5f,0,positiveLookup);
            unsigned exactInterior=0;
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
                const glm::vec2 p{8*(float(x)+.5f)/32,8*(1-(float(x)+.5f)/32)*(float(y)+.5f)/32};
                const unsigned index=(y*32+x)*4;
                if(p.x>2&&p.y>2&&p.x+p.y<6) {
                    ++exactInterior;
                    Check(glm::length(glm::vec2(boundaryLookupCentroid[index],boundaryLookupCentroid[index+1])-p)<.0001f,
                          "Positive spline lost affine precision with a complete interior stencil");
                }
            }
            Check(exactInterior>8,"Positive spline fixture missed its complete interior stencil");
            const auto boundarySignal=[](glm::vec3 p) {return .6f+.01f*p.x+.015f*p.y;};
            const auto boundaryNoise=[](glm::vec3 p) {return .1f*std::sin(2.3f*(p.x-1.f/3)+3.7f*(p.y-1.f/3));};
            for(unsigned i=0;i<64;++i) {
                const auto p=glm::vec3(boundaryAsset->Attributes()[1+i*2]);
                incidentPixels[i]={boundarySignal(p)+boundaryNoise(p),.4f+.02f*p.x,.2f,.3f};
            }
            cacheTexture=texture(32,2,incidentPixels);
            const auto boundaryLookup=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){int k=int(gl_FragCoord.x)/2%6+1,kind=int(gl_FragCoord.y)%3;
float epsilon=(int(gl_FragCoord.x)%2==0?-1:1)*.00001;
vec2 at=kind==0?vec2(-.2,float(k)+epsilon):(kind==1?vec2(float(k)+epsilon,-.2):vec2(7.2-float(k),float(k)+epsilon));
outColor=LmIncidentBase(lmCache,lmAttributes,0,vec3(at+1.0/3.0,0));})");
            auto boundary=draw(.5f,0,boundaryLookup);double boundaryJump=0;
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;x+=2) {
                const auto at=(y*32+x)*4;boundaryJump=std::max(boundaryJump,double(std::abs(boundary[at]-boundary[at+4])));
                Check(std::abs(boundary[at]-boundary[at+4])<.0001f,"Nonlinear incident field jumps at an outer cache grid boundary");
            }
            const auto splineGradient=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){int column=int(gl_FragCoord.x),k=column/4%4+1,kind=int(gl_FragCoord.y)%3;
const float offsets[4]=float[4](-2,-1,1,2);float epsilon=offsets[column%4]*.002;
vec2 at=kind==0?vec2(float(k),1.2):(kind==1?vec2(float(k)+.5,1.2):vec2(float(k)+.2,5-float(k)-.2));
outColor=LmIncidentBase(lmCache,lmAttributes,0,vec3(at+vec2(epsilon,0)+1.0/3.0,0));})");
            const auto spline=draw(.5f,0,splineGradient);double derivativeGap=0;
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;x+=4) {
                const unsigned at=(y*32+x)*4;
                const double left=(spline[at+4]-spline[at])/.002,right=(spline[at+12]-spline[at+8])/.002;
                derivativeGap=std::max(derivativeGap,std::abs(left-right));
                Check(std::abs(left-right)<.006,"Nonlinear incident reconstruction has an interior slope discontinuity");
            }
            auto filteredNoise=draw(.5f,0,incidentFilter);double rawSquare=0,filteredSquare=0;unsigned noisyInterior=0;
            const float support=boundaryAsset->Attributes()[0].w;
            for(unsigned i=0;i<64;++i) {
                const auto p=glm::vec3(boundaryAsset->Attributes()[1+i*2]);
                const glm::vec2 centroid{boundaryFilterCentroid[i*4],boundaryFilterCentroid[i*4+1]};
                Check(glm::length(centroid-glm::vec2(p))<support,"Truncated positive world filter exceeded its geometric support");
                Check(std::abs(filteredNoise[i*4+1]-(.4f+.02f*centroid.x))<.0001f,
                      "Wide world incident filter broke affine precision at its effective centroid");
                if(p.x<=support||p.y<=support||p.x+p.y+support*std::sqrt(2.f)>=8)continue;
                const double raw=boundaryNoise(p),filtered=filteredNoise[i*4]-boundarySignal(p);
                rawSquare+=raw*raw;filteredSquare+=filtered*filtered;++noisyInterior;
            }
            Check(noisyInterior>=3&&filteredSquare<rawSquare*.64,
                  "Incident filtering failed to reduce skew-cell spatial transport noise");
            std::cout<<"Nonlinear incident boundary jump="<<boundaryJump<<", interior derivative gap="<<derivativeGap
                     <<"; spatial noise RMS ratio="<<std::sqrt(filteredSquare/rawSquare)<<'\n';
            const ProbeTriangle thinTriangle{{0,0,0},{8,0,0},{0,.5f,0},glm::vec3(.5f)};
            auto thinAsset=BuildLumenGiScene(std::span(&thinTriangle,1),.2f);scene=thinAsset->Geometry();
            geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
            attributes=texture(32,unsigned(thinAsset->Attributes().size()/32),thinAsset->Attributes());
            rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),1};
            lc.atlas={32,int(thinAsset->SurfelCount()),0,1};incidentPixels.assign(64,glm::vec4(0));
            Check(thinAsset->SurfelCount()==64,"Rank-one filter regression requires an eight-cell thin triangle");
            for(unsigned i=0;i<64;++i) {
                const auto p=glm::vec3(thinAsset->Attributes()[1+i*2]);incidentPixels[i]={p.x,p.y,1,.3f};
            }
            cacheTexture=texture(32,2,incidentPixels);const auto thinCentroid=draw(.5f,0,incidentFilter);
            for(unsigned i=0;i<64;++i) {
                const auto p=glm::vec3(thinAsset->Attributes()[1+i*2]);
                incidentPixels[i]={boundarySignal(p),.4f+.02f*p.x,.2f,.3f};
            }
            cacheTexture=texture(32,2,incidentPixels);const auto thinFiltered=draw(.5f,0,incidentFilter);
            for(unsigned i=0;i<64;++i) {
                const glm::vec2 centroid{thinCentroid[i*4],thinCentroid[i*4+1]};
                Check(std::abs(thinFiltered[i*4]-(.6f+.01f*centroid.x+.015f*centroid.y))<.0001f&&
                      std::abs(thinFiltered[i*4+1]-(.4f+.02f*centroid.x))<.0001f&&
                      std::abs(thinFiltered[i*4+2]-.2f)<.0001f&&std::abs(thinFiltered[i*4+3]-.3f)<.0001f,
                      "Rank-one positive world filter broke constant/affine transport on a thin triangle");
            }
            // Roof slabs meet at non-conforming edges: two disjoint short
            // neighbours border one long edge, with a real opening between.
            // Verify both shared intervals meet continuously while the open
            // interval stays independent instead of bridging across the gap.
            const std::array<ProbeTriangle,3> splitEdge{{
                {{0,0,0},{0,4,0},{-4,0,0},glm::vec3(.5f)},
                {{0,0,0},{2,0,0},{0,1,0},glm::vec3(.5f)},
                {{0,3,0},{2,3,0},{0,4,0},glm::vec3(.5f)}}};
            auto splitAsset=BuildLumenGiScene(splitEdge,.5f);scene=splitAsset->Geometry();
            geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
            attributes=texture(32,unsigned(splitAsset->Attributes().size()/32),splitAsset->Attributes());
            rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),3};
            lc.atlas={32,int(splitAsset->SurfelCount()),0,3};
            unsigned longId=0,lowerId=0,upperId=0;const auto& splitGeometry=scene->Pixels();
            for(unsigned i=0;i<3;++i) {
                const unsigned at=scene->NodeCount()*2+i*5;
                const auto center=(glm::vec3(splitGeometry[at])+glm::vec3(splitGeometry[at+1])+glm::vec3(splitGeometry[at+2]))/3.f;
                if(center.x<0)longId=i;else if(center.y<2)lowerId=i;else upperId=i;
            }
            incidentPixels.assign(64,glm::vec4(0));
            for(unsigned i=0;i<splitAsset->SurfelCount();++i) {
                const unsigned owner=unsigned(splitAsset->Attributes()[3+i*2].w);
                incidentPixels[i]={owner==longId?.2f:owner==lowerId?.8f:.6f,0,0,.3f};
            }
            cacheTexture=texture(32,2,incidentPixels);
            const auto splitLookup=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){int part=int(gl_FragCoord.y)%3;vec3 p=vec3(0,part==0?.5:part==1?3.5:2.0,0);
outColor=LmIncidentLookup(lmCache,lmAttributes,int(lmReflection.w),p);})");
            const auto splitLong=draw(.5f,longId,splitLookup),splitLower=draw(.5f,lowerId,splitLookup),splitUpper=draw(.5f,upperId,splitLookup);
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
                const unsigned at=(y*32+x)*4;
                if(y%3==0)Check(std::abs(splitLong[at]-.5f)<1e-5&&std::abs(splitLong[at]-splitLower[at])<1e-5,
                               "Long/short coplanar lower edge has a discontinuous irradiance seam");
                else if(y%3==1)Check(std::abs(splitLong[at]-.4f)<1e-5&&std::abs(splitLong[at]-splitUpper[at])<1e-5,
                                    "Long/short coplanar upper edge has a discontinuous irradiance seam");
                else Check(std::abs(splitLong[at]-.2f)<1e-5,"Partial edge blending bridged a genuine roof opening");
            }
            std::cout<<"Non-conforming roof edge continuity and open-interval isolation passed\n";
            // A folded square's upper diagonal corner can represent a distant
            // canonical sample. Trace-hit transport must not borrow that bright
            // sample at the opposite end of the same mesh edge.
            const ProbeTriangle transportReceiver{{0,0,0},{32,0,0},{0,32,0},glm::vec3(.5f)};
            const auto transportAsset=BuildLumenGiScene(std::span(&transportReceiver,1),.2f,32);
            Check(transportAsset->Attributes()[0].y==32,"Transport locality regression requires a 32-cell lattice");
            scene=transportAsset->Geometry();geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
            attributes=texture(32,unsigned(transportAsset->Attributes().size()/32),transportAsset->Attributes());
            rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),1};
            lc.atlas={32,int(transportAsset->SurfelCount()),0,1};
            std::vector<glm::vec4> transportPixels(1024,glm::vec4(0,0,0,1));
            const glm::vec3 brightTransportPoint=glm::vec3(transportAsset->Attributes()[1+(21*32+9)*2]);
            for(unsigned i=0;i<transportAsset->SurfelCount();++i) {
                const auto p=glm::vec3(transportAsset->Attributes()[1+i*2]);
                if(glm::length(p-brightTransportPoint)<.0001f)transportPixels[i]=glm::vec4(1);
            }
            cacheTexture=texture(32,32,transportPixels);
            const auto transportLookup=b.FullscreenPipeline(Vertex,LumenScreenPreamble()+R"(
void main(){int part=int(gl_FragCoord.y)%3;
vec3 p=part==0?vec3(22,10,0):part==1?vec3(9.0+1.0/3.0,21.0+1.0/3.0,0):vec3(22.0+1.0/3.0,9.0+1.0/3.0,0);
outColor=vec4(LmLookup(lmCache,0,p),1);})");
            const auto transportValues=draw(.5f,0,transportLookup);
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
                const float expected=y%3==1?1.f:0.f;const unsigned at=(y*32+x)*4;
                for(unsigned channel=0;channel<3;++channel)
                    Check(std::isfinite(transportValues[at+channel])&&std::abs(transportValues[at+channel]-expected)<.0001f,
                          "Transport lookup imported distant folded radiance across the triangle diagonal");
            }
            std::cout<<"Transport hit reconstruction preserves local diagonal support\n";
            // Production resolve has two different units: Material supplies E,
            // while diagnostic views supply reflected radiance. A strongly
            // colored receiver exposes accidental extra rho or PI factors.
            const ProbeTriangle displayReceiver{{-2,-2,0},{2,-2,0},{0,2,0},{.2f,.4f,.6f}};
            const auto displayAsset=BuildLumenGiScene(std::span(&displayReceiver,1),.75f);
            scene=displayAsset->Geometry();geometry=texture(scene->Width(),scene->Height(),scene->Pixels());
            attributes=texture(LumenSurfaceWidth,displayAsset->AttributeHeight(),displayAsset->Attributes());
            resolveAttributes=attributes;
            rc.geometry={int(scene->NodeCount()*2),int(scene->Width()),int(scene->NodeCount()),1};
            lc.atlas={int(LumenSurfaceWidth),int(displayAsset->SurfelCount()),0,1};lc.gather={32,32,16,0};
            lc.inverseView=glm::translate(glm::mat4(1),glm::vec3(0,0,2));lc.settings.x=1.25f;
            screenSource=texture(32,32,std::vector<glm::vec4>(1024,glm::vec4(0,0,0,1)));
            const auto displayResolve=b.FullscreenPipeline(Vertex,LumenResolveShader());
            const auto checkDisplay=[&](const std::vector<float>& pixels,glm::vec3 expected,float alpha) {
                for(unsigned i=0;i<1024;++i) {
                    for(unsigned channel=0;channel<3;++channel)
                        Check(std::isfinite(pixels[i*4+channel])&&std::abs(pixels[i*4+channel]-expected[channel])<.00001f,
                              "Lumen display resolve changed cache radiance/irradiance units");
                    Check(std::abs(pixels[i*4+3]-alpha)<.00001f,"Lumen display resolve changed diagnostic validity or material sky visibility");
                }
            };
            const glm::vec3 displayIncident(.7f,.5f,.3f),displayTotal(1.1f,.8f,.55f),displayDirect(.4f,.3f,.15f);
            for(const auto mode:{LumenLightingView::Material,LumenLightingView::SurfaceCache,LumenLightingView::IndirectOnly,LumenLightingView::DirectOnly}) {
                const auto source=mode==LumenLightingView::SurfaceCache?displayTotal:mode==LumenLightingView::DirectOnly?displayDirect:displayIncident;
                cacheTexture=texture(LumenSurfaceWidth,displayAsset->CacheHeight(),
                    std::vector<glm::vec4>(LumenSurfaceWidth*displayAsset->CacheHeight(),glm::vec4(source,.25f)));
                surface.screenJitter.z=float(mode);
                const auto expected=source*lc.settings.x*(mode==LumenLightingView::Material?glm::vec3(3.14159265359f):
                    mode==LumenLightingView::IndirectOnly?displayReceiver.diffuseReflectance:glm::vec3(1));
                checkDisplay(draw(.5f,0,displayResolve),expected,mode==LumenLightingView::Material?.25f:1.f);
            }
            // A missing sparse ID must still resolve an existing surface with
            // a short geometry query, rather than replacing it with probe E.
            screenSource=texture(32,32,std::vector<glm::vec4>(1024,glm::vec4(0)));
            surface.screenJitter.z=float(LumenLightingView::DirectOnly);
            checkDisplay(draw(.5f,0,displayResolve),displayDirect*lc.settings.x,1);
            // Give the old probe fallback a deliberately nonzero field and
            // query a point well outside the triangle. Diagnostic misses must
            // remain black even though Material's fallback has illumination.
            std::vector<glm::vec4> displayProbes(8*26,glm::vec4(0));
            for(unsigned i=0;i<8;++i) {
                displayProbes[i]=glm::vec4(.6f/.282095f,.4f/.282095f,.2f/.282095f,.5f/.282095f);
                for(unsigned row=9;row<25;++row)displayProbes[row*8+i]={1000,1000000,0,0};
                displayProbes[25*8+i].x=1;
            }
            probeTexture=texture(8,26,displayProbes);rc.origin={99,-1,-1,0};rc.spacing=glm::vec4(2);rc.counts={2,2,2,8};
            lc.inverseView=glm::translate(glm::mat4(1),glm::vec3(100,0,2));
            checkDisplay(draw(.5f,0,displayResolve),glm::vec3(0),0);
            surface.screenJitter.z=0;
            checkDisplay(draw(.5f,0,displayResolve),glm::vec3(.6f,.4f,.2f)*(3.14159265359f*lc.settings.x),.5f);
            std::cout<<"Cache display units, missing receiver IDs and strict geometric miss passed\n";
            // Exercise the actual opaque PBR output, with bright analytic
            // illumination/emission and dark metallic/AO material settings.
            // Diagnostic radiance must bypass all of them exactly; mode zero
            // must still shade normally. Coverage remains the material alpha.
            const auto displayPbr=b.FullscreenPipeline(R"GLSL(#version 450 core
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
out vec3 vWorldPosition;out vec3 vWorldNormal;out vec2 vTexCoord;out vec4 vWorldTangent;
void main(){gl_Position=vec4(position,0,1);vWorldPosition=vec3(0);vWorldNormal=vec3(0,0,1);vTexCoord=uv;vWorldTangent=vec4(1,0,0,1);}
)GLSL",Material::PbrFragmentShader());
            struct alignas(16) DisplayView {glm::mat4 vp{1};glm::vec4 camera{0,0,2,0};} displayView;
            Material::ParameterBlock displayMaterial(Material::MakePbrTemplate());
            Check(displayMaterial.Set("baseColor",glm::vec4(.08f,.12f,.2f,.6f))&&displayMaterial.Set("metallic",1.f)&&
                  displayMaterial.Set("ao",0.f)&&displayMaterial.Set("opacity",.5f)&&
                  displayMaterial.Set("emissiveColor",glm::vec4(40,20,10,0))&&displayMaterial.Set("reflectionStrength",1.f),
                  "Cannot initialize cache-display PBR fixture");
            const auto displayViewBuffer=b.Uniform(sizeof(displayView)),displayMaterialBuffer=b.Uniform(displayMaterial.Bytes().size()),
                displayEffectsBuffer=b.Uniform(sizeof(SceneEffectsConstants)),displayProbeBuffer=b.Uniform(sizeof(DiffuseProbeConstants)),
                displayProbeOptionsBuffer=b.Uniform(sizeof(DiffuseProbeRuntimeConstants)),displayVisibilityBuffer=b.Uniform(sizeof(DiffuseProbeVisibilityConstants));
            SceneEffectsConstants displayEffects;displayEffects.sunDirectionIntensity={0,0,-1,100};displayEffects.sunColor=glm::vec4(10);
            displayEffects.effects.y=1;displayEffects.screenAndAo={1.f/32,1.f/32,1,0};
            const DiffuseProbeConstants disabledProbes;const DiffuseProbeRuntimeConstants disabledProbeOptions;
            const DiffuseProbeVisibilityConstants disabledVisibility;
            const auto displayRadiance=texture(32,32,std::vector<glm::vec4>(1024,glm::vec4(displayTotal,1)));
            surface.screenTime={1.f/32,1.f/32,0,0};surface.options={1,1,0,0};surface.screenJitter=glm::vec4(0);
            const auto drawDisplayPbr=[&](LumenLightingView mode) {
                surface.screenJitter.z=float(mode);auto e=device.BeginFrame({0,32,32});e.SetRenderTarget({target,32,32});
                e.BindPipeline(displayPbr);e.BindMesh(mesh);
                for(auto [buffer,binding,pointer,bytes]:{
                    std::tuple{displayViewBuffer,0u,(const void*)&displayView,uint32_t(sizeof(displayView))},
                    std::tuple{displayMaterialBuffer,Material::MaterialParameterBinding,(const void*)displayMaterial.Bytes().data(),uint32_t(displayMaterial.Bytes().size())},
                    std::tuple{points,3u,(const void*)pointData.data(),uint32_t(sizeof(pointData))},
                    std::tuple{displayEffectsBuffer,SceneEffectsBinding,(const void*)&displayEffects,uint32_t(sizeof(displayEffects))},
                    std::tuple{surfaceBuffer,SurfacePassBinding,(const void*)&surface,uint32_t(sizeof(surface))},
                    std::tuple{areas,AreaLightsBinding,(const void*)areaData.data(),uint32_t(sizeof(areaData))},
                    std::tuple{sky,SkyLightBinding,(const void*)&sc,uint32_t(sizeof(sc))},
                    std::tuple{displayProbeBuffer,DiffuseProbeBinding,(const void*)&disabledProbes,uint32_t(sizeof(disabledProbes))},
                    std::tuple{displayProbeOptionsBuffer,DiffuseProbeSettingsBinding,(const void*)&disabledProbeOptions,uint32_t(sizeof(disabledProbeOptions))},
                    std::tuple{displayVisibilityBuffer,DiffuseProbeVisibilityBinding,(const void*)&disabledVisibility,uint32_t(sizeof(disabledVisibility))},
                    std::tuple{rt,RealtimeGiBinding,(const void*)&rc,uint32_t(sizeof(rc))}}) {
                    for(unsigned offset=0;offset<bytes;offset+=256)e.UpdateUniformBufferBytes(buffer,(const std::byte*)pointer+offset,std::min(256u,bytes-offset),offset);
                    e.BindUniformBuffer(buffer,binding);
                }
                for(unsigned slot=0;slot<16;++slot)e.BindTexture(slot==15?displayRadiance:slot==11?brdfTexture:white,slot);
                e.DrawIndexed({3});e.End(false);bool done=false;
                Check(device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;}),"Submit cache-display PBR draw");
                while(!done){device.returnSystem.DrainCallbacks();if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
                std::vector<float> pixels;
                OnThread(device,[&]{if(vulkan)pixels=device.BackendDiagnostics()->ReadTexture(color);else {
                    const auto* spec=device.GetResourcePool().TextureTable.Get(color);pixels.resize(4096);
                    glGetTextureImage(spec->rhi_id,0,GL_RGBA,GL_FLOAT,int(pixels.size()*4),pixels.data());
                    Check(glGetError()==GL_NO_ERROR,"Cache-display PBR GL readback failed");
                }});return pixels;
            };
            const auto materialDisplay=drawDisplayPbr(LumenLightingView::Material);
            Check(materialDisplay[0]>39.f&&materialDisplay[1]>19.f,"Material display mode stopped normal PBR emission/lighting");
            for(const auto mode:{LumenLightingView::SurfaceCache,LumenLightingView::IndirectOnly,LumenLightingView::DirectOnly})
                checkDisplay(drawDisplayPbr(mode),displayTotal,.3f);
            std::cout<<"Opaque PBR cache display preserves radiance and coverage without analytic/emission double counting\n";
            OnThread(device, [&] {
                Check(!device.BackendDiagnostics()->ValidationErrorCount(), "Vulkan validation error");
            });
        }
        device.StopAndRelease();
        window.Shutdown();
        glfwTerminate();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
