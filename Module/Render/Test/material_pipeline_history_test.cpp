#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/rhi_device.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Completion {
    bool done = false;
    GLenum error = GL_NO_ERROR;
};
struct Recorded {
    RHIFrameEncoder encoder;
    std::uint64_t token;
    PipelineStatistics statistics;
};

// Empty scenes still execute the production HDR, TAA, history-depth-copy and
// display passes. Callbacks own their result so timeout/unwind cannot leave
// references to local stack variables on the render thread.
class HistoryFixture {
public:
    explicit HistoryFixture(OpenglBackendContext context) : pipeline_(device_) {
        device_.Run(BackendType::Opengl, std::move(context));
        settings_.shadows.enabled = false;
        settings_.shadows.atlasResolution = 128;
        settings_.reflection.enabled = false;
        settings_.post.bloomEnabled = false;
        settings_.post.ssaoEnabled = false;
        settings_.temporal.antialiasing = AntialiasingMode::TAA;
        settings_.temporal.msaaSamples = 1;
        SetCameraX(0);
    }
    ~HistoryFixture() {
        pipeline_.Shutdown();
        // Also releases any unsubmitted recording leases left by a failed check.
        device_.StopAndRelease();
        while (!device_.returnSystem.callbacks.empty()) device_.returnSystem.DrainCallbacks();
    }

    void Run() {
        CheckPipeline(pipeline_.Initialize(), "Initialize");
        Resize(64, 64);
        Check(!pipeline_.CompleteFrame(0) && !pipeline_.DiscardFrame(0), "Zero token must be rejected");

        // Recording alone must not create usable history, nor may a second
        // recording or a target resize race the pending history images.
        auto abandonedFirst = Record();
        Check(!abandonedFirst.statistics.historyUsed && !abandonedFirst.statistics.cameraHistoryUsed,
              "A fresh pipeline must have no completed history");
        CheckPendingGuards(abandonedFirst.token);
        Discard(abandonedFirst);
        Check(!pipeline_.CompleteFrame(abandonedFirst.token), "A discarded frame must not complete later");
        auto first = Record();
        Check(!first.statistics.historyUsed && !first.statistics.cameraHistoryUsed,
              "A recorded then discarded first frame must not become history");
        Complete(first);

        auto reused = Record();
        Check(reused.statistics.historyUsed && reused.statistics.cameraHistoryUsed,
              "An executed and completed frame must become TAA/camera history");
        Check(!pipeline_.CompleteFrame(first.token) && !pipeline_.DiscardFrame(first.token),
              "Stale completion/discard must be rejected while another frame is pending");
        Check(pipeline_.RecordedFrameToken() == reused.token, "A stale token must not consume the active token");
        Complete(reused);
        Check(!pipeline_.CompleteFrame(reused.token) && !pipeline_.DiscardFrame(reused.token),
              "A consumed token must not be accepted twice");

        // The abandoned camera is far enough away to trigger a camera cut.
        // Returning to the last EXECUTED camera must retain its old history;
        // accidentally committing the discarded camera would fail this check.
        SetCameraX(4);
        auto abandonedCut = Record();
        Check(!abandonedCut.statistics.historyUsed && !abandonedCut.statistics.cameraHistoryUsed,
              "The test camera displacement must trigger a history cut");
        Discard(abandonedCut);
        SetCameraX(0);
        auto afterDiscard = Record();
        Check(afterDiscard.statistics.historyUsed && afterDiscard.statistics.cameraHistoryUsed,
              "Discard must preserve the last completed image and camera metadata");
        Complete(afterDiscard);

        // Reset during a pending frame invalidates its future history metadata
        // without freeing the one-in-flight token or admitting another Record.
        auto resetPending = Record();
        Check(resetPending.statistics.historyUsed, "Reset test requires established history");
        pipeline_.ResetHistory();
        Check(pipeline_.RecordedFrameToken() == resetPending.token, "ResetHistory must preserve the pending token");
        CheckPendingGuards(resetPending.token);
        Complete(resetPending);
        auto afterReset = Record();
        Check(!afterReset.statistics.historyUsed && !afterReset.statistics.cameraHistoryUsed,
              "Completing an invalidated pending frame must not resurrect history");
        Complete(afterReset);
        auto reestablished = Record();
        Check(reestablished.statistics.historyUsed, "History must recover after a fresh completed frame");
        Discard(reestablished);

        // New render targets require fresh history. Tokens remain unique across
        // resize, so a delayed completion cannot acknowledge a newer frame.
        Resize(32, 32);
        auto resized = Record();
        Check(!resized.statistics.historyUsed && !resized.statistics.cameraHistoryUsed,
              "Resize must invalidate previous image/camera history");
        Check(!pipeline_.CompleteFrame(afterReset.token) && !pipeline_.DiscardFrame(afterReset.token),
              "Pre-resize tokens must remain stale");
        Check(pipeline_.RecordedFrameToken() == resized.token, "Stale pre-resize token consumed the new frame");
        Complete(resized);
        auto resizedReuse = Record();
        Check(resizedReuse.statistics.historyUsed, "History must be reusable at the new size");
        Discard(resizedReuse);

        // Shutdown also drops pending metadata. Its resource lease remains in
        // the abandoned encoder until Cancel, while stale tokens stay rejected
        // after the same pipeline object is initialized again.
        auto beforeShutdown = Record();
        const auto oldToken = beforeShutdown.token;
        pipeline_.Shutdown();
        Check(pipeline_.RecordedFrameToken() == 0 && !pipeline_.DebugTextures().sceneColor.IsValid(),
              "Shutdown must clear pending history and borrowed targets");
        Check(beforeShutdown.encoder.Cancel(), "Cannot cancel the shutdown frame's recording lease");
        Check(!pipeline_.CompleteFrame(oldToken) && !pipeline_.DiscardFrame(oldToken),
              "Shutdown must reject its pending token");
        CheckPipeline(pipeline_.Initialize(), "Reinitialize");
        Resize(64, 64);
        auto restarted = Record();
        Check(restarted.token != oldToken && !restarted.statistics.historyUsed && !restarted.statistics.cameraHistoryUsed,
              "Restart must use a new token and empty history");
        Check(!pipeline_.CompleteFrame(oldToken) && !pipeline_.DiscardFrame(oldToken),
              "A pre-shutdown token must not acknowledge a restarted frame");
        Check(pipeline_.RecordedFrameToken() == restarted.token, "Stale shutdown token consumed the restarted frame");
        Complete(restarted);
        auto restartedReuse = Record();
        Check(restartedReuse.statistics.historyUsed, "Restarted pipeline must accumulate history normally");
        Complete(restartedReuse);

        settings_.lighting.specularAA.z = 0;
        auto changedSpecularAA = Record();
        Check(!changedSpecularAA.statistics.historyUsed, "Changing specular filtering must invalidate HDR history");
        Complete(changedSpecularAA);
        auto reusedSpecularAA = Record();
        Check(reusedSpecularAA.statistics.historyUsed, "Unchanged specular settings must retain history");
        Complete(reusedSpecularAA);
        for (const auto invalid : {glm::vec4(-1, .2f, 1, 0), glm::vec4(.15f, 2, 1, 0), glm::vec4(.15f, .2f, .5f, 0)}) {
            settings_.lighting.specularAA = invalid;
            auto encoder = Begin();
            RenderFrame empty;
            Check(!pipeline_.Record(encoder, empty, camera_, settings_), "Invalid specular AA settings accepted");
            Check(encoder.Cancel() && pipeline_.RecordedFrameToken() == 0, "Rejected settings left a pending frame");
        }
        settings_.lighting.specularAA = Material::PbrPassConstants{}.specularAA;
        auto restoredSpecularAA = Record(); Complete(restoredSpecularAA);

        settings_.reflection.enabled=true;
        settings_.reflection.receiverBounds=ReflectionReceiverBounds{{-1,-1,-1},{1,1,1}};
        auto visibleMirror=Record();
        Check(visibleMirror.statistics.reflectionPasses==1,"Visible reflection receiver must draw its mirror");
        Complete(visibleMirror);
        settings_.reflection.receiverBounds=ReflectionReceiverBounds{{100,-1,-1},{102,1,1}};
        auto culledMirror=Record();
        Check(culledMirror.statistics.reflectionPasses==0,"Offscreen reflection receiver must skip its mirror");
        Complete(culledMirror);
        settings_.reflection.receiverBounds.reset();
        auto unboundedMirror=Record();
        Check(unboundedMirror.statistics.reflectionPasses==1,"Omitted receiver bounds must preserve mirror behavior");
        Complete(unboundedMirror);
        for(const auto invalid:{ReflectionReceiverBounds{{1,0,0},{-1,1,1}},
                                ReflectionReceiverBounds{{std::numeric_limits<float>::quiet_NaN(),0,0},{1,1,1}}}) {
            settings_.reflection.receiverBounds=invalid;
            auto encoder=Begin();RenderFrame empty;
            Check(!pipeline_.Record(encoder,empty,camera_,settings_),"Malformed mirror receiver bounds accepted");
            Check(encoder.Cancel()&&pipeline_.RecordedFrameToken()==0,"Invalid mirror bounds left a pending frame");
        }
        settings_.reflection.receiverBounds.reset();settings_.reflection.enabled=false;
        const float originalSourceRadius=settings_.lighting.lightColors[0].w;
        for(const float invalid:{-1.f,10001.f,std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity()}) {
            settings_.lighting.lightColors[0].w=invalid;
            auto encoder=Begin();RenderFrame empty;
            Check(!pipeline_.Record(encoder,empty,camera_,settings_),"Invalid point source radius accepted");
            Check(encoder.Cancel()&&pipeline_.RecordedFrameToken()==0,"Invalid point source left a pending frame");
        }
        settings_.lighting.lightColors[0].w=originalSourceRadius;

        settings_.sky.enabled=true;
        auto skyFirst=Record();
        Check(!skyFirst.statistics.historyUsed&&skyFirst.statistics.skyPasses==1,"Enabling sky must reset history and draw background");
        Complete(skyFirst);
        auto skyReused=Record(); Check(skyReused.statistics.historyUsed,"Stable sky must reuse history"); Complete(skyReused);
        settings_.sky.rotation=.5f;
        auto skyRotated=Record(); Check(!skyRotated.statistics.historyUsed,"Sky rotation retained old HDR history"); Complete(skyRotated);
        settings_.sky.background=false;
        auto skyHidden=Record(); Check(!skyHidden.statistics.historyUsed&&skyHidden.statistics.skyPasses==0,"Background toggle did not reset history"); Complete(skyHidden);
        settings_.sky.enabled=false;
        auto skyOff=Record(); Check(!skyOff.statistics.historyUsed&&skyOff.statistics.skyPasses==0,"Sky disable retained history or drawing"); Complete(skyOff);

        ProbeBakeSettings bake;
        bake.origin=glm::vec3(-1); bake.spacing=glm::vec3(2); bake.counts={2,2,2}; bake.raysPerProbe=64;
        ProbeBakeLighting probeLighting;
        settings_.probes.enabled=true;
        settings_.probes.volume=BakeDiffuseProbeVolume(bake,{},probeLighting);
        auto probesFirst=Record();
        Check(!probesFirst.statistics.historyUsed&&probesFirst.statistics.validDiffuseProbes==8,"Enabling probes retained history or missed resources"); Complete(probesFirst);
        auto probesReused=Record(); Check(probesReused.statistics.historyUsed,"Stable probes must reuse history"); Complete(probesReused);
        settings_.probes.visibilityBias=.2f;
        auto probesBiased=Record(); Check(!probesBiased.statistics.historyUsed,"Probe visibility change retained history"); Complete(probesBiased);
        settings_.probes.volume=BakeDiffuseProbeVolume(bake,{},probeLighting);
        auto probesReplaced=Record(); Check(!probesReplaced.statistics.historyUsed,"Replacing probe volume retained old history"); Complete(probesReplaced);
        auto probesPending=Record();
        // A recorded encoder must retain the old immutable buffers even after
        // the pipeline and the caller release their copies of the bake asset.
        pipeline_.Shutdown(); settings_.probes.volume.reset();
        Check(probesPending.encoder.End(false),"Cannot finish the leased probe frame");
        auto executed=std::make_shared<Completion>();
        Check(device_.async_SubmitFrameCommands(probesPending.encoder.GetCommandBuffer(),[executed]{executed->done=true;}),"Cannot submit leased probe frame");
        Wait(executed,"probe resources after shutdown");
        Check(!pipeline_.CompleteFrame(probesPending.token),"Shutdown probe token unexpectedly completed");
        CheckPipeline(pipeline_.Initialize(),"Probe reinitialize"); Resize(64,64);
        settings_.probes.enabled=false;
        auto probesOff=Record(); Check(!probesOff.statistics.historyUsed&&probesOff.statistics.validDiffuseProbes==0,"Disabled probes retained history/resources"); Complete(probesOff);

        settings_.realtimeGi.enabled=true;settings_.realtimeGi.scene=BuildRealtimeGiScene({});
        settings_.realtimeGi.origin=glm::vec3(-1);settings_.realtimeGi.spacing=glm::vec3(2);settings_.realtimeGi.counts={2,2,2};settings_.realtimeGi.probesPerFrame=4;
        auto abandonedRt=Record();Check(abandonedRt.statistics.realtimeGiUpdatedProbes==8,"Fresh realtime cache was not initialized");Discard(abandonedRt);
        auto rtFirst=Record();Check(rtFirst.statistics.realtimeGiUpdatedProbes==8,"Discard committed realtime GI state");Complete(rtFirst);
        auto rtReuse=Record();Check(rtReuse.statistics.realtimeGiUpdatedProbes==4&&rtReuse.statistics.realtimeGiPasses==2&&rtReuse.statistics.historyUsed,"Realtime cache/history did not reuse");Discard(rtReuse);
        auto rtAfterDiscard=Record();Check(rtAfterDiscard.statistics.realtimeGiUpdatedProbes==4,"Discard reset completed realtime cache");Complete(rtAfterDiscard);
        Resize(32,32);auto rtResized=Record();Check(rtResized.statistics.realtimeGiUpdatedProbes==4,"Viewport resize rebuilt world-space GI");Complete(rtResized);
        settings_.realtimeGi.reset=true;auto rtReset=Record();Check(rtReset.statistics.realtimeGiUpdatedProbes==8,"Explicit realtime reset ignored");Complete(rtReset);settings_.realtimeGi.reset=false;
        settings_.realtimeGi.scene=BuildRealtimeGiScene({});auto rtReplaced=Record();Check(rtReplaced.statistics.realtimeGiUpdatedProbes==8&&!rtReplaced.statistics.historyUsed,"Realtime scene replacement retained old cache/history");Complete(rtReplaced);
        settings_.realtimeGi.scene=RefitRealtimeGiScene(*settings_.realtimeGi.scene,{});
        auto rtRefitDropped=Record();Check(rtRefitDropped.statistics.realtimeGiUpdatedProbes==8,"Refit did not refresh world visibility");Discard(rtRefitDropped);
        auto rtRefitRetry=Record();Check(rtRefitRetry.statistics.realtimeGiUpdatedProbes==8,"Discard committed refitted world visibility");Complete(rtRefitRetry);
        auto rtRefitReuse=Record();Check(rtRefitReuse.statistics.realtimeGiUpdatedProbes==4,"Completed refit did not resume normal probe budget");Complete(rtRefitReuse);
        settings_.indirect.enabled=true;auto badEncoder=Begin();RenderFrame empty;
        Check(!pipeline_.Record(badEncoder,empty,camera_,settings_)&&badEncoder.Cancel(),"Conflicting realtime/screen GI accepted");settings_.indirect.enabled=false;
        auto leasedRt=Record();pipeline_.Shutdown();
        Check(leasedRt.encoder.End(false),"Cannot finish leased realtime frame");auto rtExecuted=std::make_shared<Completion>();
        Check(device_.async_SubmitFrameCommands(leasedRt.encoder.GetCommandBuffer(),[rtExecuted]{rtExecuted->done=true;}),"Cannot submit leased realtime frame");Wait(rtExecuted,"realtime cache after shutdown");
        Check(!pipeline_.CompleteFrame(leasedRt.token),"Shutdown realtime token unexpectedly completed");
        CheckPipeline(pipeline_.Initialize(),"Realtime reinitialize");Resize(32,32);
        auto rtRestarted=Record();Check(rtRestarted.statistics.realtimeGiUpdatedProbes==8&&!rtRestarted.statistics.historyUsed,"Restart retained stale realtime state");Complete(rtRestarted);
        settings_.realtimeGi.enabled=false;auto rtOff=Record();Check(rtOff.statistics.realtimeGiPasses==0&&!rtOff.statistics.historyUsed,"Realtime disable retained passes/history");Complete(rtOff);
        settings_.lumenGi.enabled=true;settings_.lumenGi.scene=BuildLumenGiScene({});settings_.lumenGi.counts={2,2,2};
        auto lmDropped=Record();Check(!lmDropped.statistics.lumenHistoryUsed&&lmDropped.statistics.lumenGiPasses>0,"Fresh hybrid GI retained history");Discard(lmDropped);
        auto lmFirst=Record();Check(!lmFirst.statistics.lumenHistoryUsed,"Discard committed hybrid gather");Complete(lmFirst);
        auto lmReuse=Record();Check(lmReuse.statistics.lumenHistoryUsed,"Completed hybrid gather was not reused");Discard(lmReuse);
        auto lmAfterDiscard=Record();Check(lmAfterDiscard.statistics.lumenHistoryUsed,"Discard invalidated completed hybrid gather");Complete(lmAfterDiscard);
        settings_.lumenGi.scene=RefitLumenGiScene(*settings_.lumenGi.scene,{});
        auto lmRefitDropped=Record();Check(lmRefitDropped.statistics.realtimeGiUpdatedProbes==8,"Hybrid refit did not refresh visibility");Discard(lmRefitDropped);
        auto lmRefitRetry=Record();Check(lmRefitRetry.statistics.realtimeGiUpdatedProbes==8,"Discard committed hybrid refit visibility");Complete(lmRefitRetry);
        auto lmRefitReuse=Record();Check(lmRefitReuse.statistics.realtimeGiUpdatedProbes==4,"Hybrid refit repeated full world visibility updates");Complete(lmRefitReuse);
        Resize(48,32);auto lmResized=Record();Check(!lmResized.statistics.lumenHistoryUsed,"Resized hybrid gather retained old view targets");Complete(lmResized);
        settings_.lumenGi.reflectionResolutionScale=1;auto lmFullReflections=Record();
        Check(!lmFullReflections.statistics.lumenHistoryUsed,"Recreated reflection outputs retained view history");Discard(lmFullReflections);
        settings_.lumenGi.reflectionResolutionScale=.5f;auto lmHalfReflections=Record();
        Check(!lmHalfReflections.statistics.lumenHistoryUsed,"Returned reflection extent read retired history");Complete(lmHalfReflections);
        settings_.lumenGi.reflectionTraceMaxDimension=320;auto lmTraceDetail=Record();
        Check(!lmTraceDetail.statistics.lumenHistoryUsed,"Changed reflection sampling reused incompatible history");Complete(lmTraceDetail);
        settings_.lumenGi.reflectionTraceMaxDimension=256;
        const auto checkReflectionSourceExtent=[&](unsigned expectedWidth,unsigned expectedHeight) {
            const auto debug=pipeline_.DebugTextures();
            auto checked=std::make_shared<Completion>();
            auto extents=std::make_shared<std::array<glm::ivec2,4>>();
            const std::array handles{debug.lumenReflectionSource,debug.lumenReflectionSourceDepth,
                                     debug.lumenReflections,debug.indirectDepth};
            device_.async_ExecuteCode([this,checked,extents,handles] {
                for(unsigned i=0;i<handles.size();++i) {
                    const auto* texture=device_.GetResourcePool().TextureTable.Get(handles[i]);
                    if(!texture){checked->error=GL_INVALID_OPERATION;return;}
                    glGetTextureLevelParameteriv(texture->rhi_id,0,GL_TEXTURE_WIDTH,&(*extents)[i].x);
                    glGetTextureLevelParameteriv(texture->rhi_id,0,GL_TEXTURE_HEIGHT,&(*extents)[i].y);
                }
                checked->error=glGetError();
            },[checked]{checked->done=true;});
            Wait(checked,"reflection source extent diagnostic");
            Check(checked->error==GL_NO_ERROR,"Cannot query reflection source/depth extents");
            Check((*extents)[0]==glm::ivec2(expectedWidth,expectedHeight)&&(*extents)[1]==(*extents)[0],
                  "Independent HDR reflection source/depth extent is incorrect");
            Check((*extents)[2]==glm::ivec2(width_,height_)&&(*extents)[3]==glm::ivec2(width_,height_),
                  "Reflection source scaling resized reflection resolve or full tracing depth");
        };
        settings_.lumenGi.reflectionResolutionScale=1;
        auto sourceHalf=Record();Complete(sourceHalf);checkReflectionSourceExtent(24,16);
        const auto mainDepth=pipeline_.DebugTextures().sceneDepth;
        const auto halfSource=pipeline_.DebugTextures().lumenReflectionSource;
        const auto sourceWorldCache=pipeline_.DebugTextures().lumenSurfaceCache;
        settings_.lumenGi.reflectionSourceResolutionScale=.25f;
        auto sourceQuarter=Record();
        Check(!sourceQuarter.statistics.lumenHistoryUsed&&!sourceQuarter.statistics.historyUsed&&
              sourceQuarter.statistics.realtimeGiUpdatedProbes==4,
              "Changing HDR source extent retained view history or reset world probes");
        Complete(sourceQuarter);checkReflectionSourceExtent(12,8);
        Check(pipeline_.DebugTextures().lumenReflectionSource!=halfSource&&pipeline_.DebugTextures().sceneDepth==mainDepth,
              "Changing HDR source extent failed to replace its target independently");
        auto sourceQuarterReuse=Record();
        Check(sourceQuarterReuse.statistics.lumenHistoryUsed&&sourceQuarterReuse.statistics.historyUsed,
              "Stable HDR source extent did not resume temporal history");
        Complete(sourceQuarterReuse);
        Check(pipeline_.DebugTextures().lumenSurfaceCache==sourceWorldCache,
              "HDR source scaling rebuilt the alternating world Surface Cache");
        settings_.lumenGi.reflectionSourceResolutionScale=.3f;
        auto sourceRounded=Record();Complete(sourceRounded);checkReflectionSourceExtent(15,10);
        const auto roundedSource=pipeline_.DebugTextures().lumenReflectionSource;
        settings_.lumenGi.reflectionSourceResolutionScale=.31f;
        auto sourceSameExtent=Record();
        Check(!sourceSameExtent.statistics.lumenHistoryUsed&&!sourceSameExtent.statistics.historyUsed,
              "Changed source scale with identical rounded extent bypassed the content hash");
        Complete(sourceSameExtent);checkReflectionSourceExtent(15,10);
        Check(pipeline_.DebugTextures().lumenReflectionSource==roundedSource,
              "Identical rounded HDR source extents unnecessarily replaced resources");
        settings_.lumenGi.reflectionSourceResolutionScale=.25f;Resize(49,33);
        auto sourceOdd=Record();Complete(sourceOdd);checkReflectionSourceExtent(13,9);
        for(float invalid:{.249f,1.001f,std::numeric_limits<float>::quiet_NaN()}) {
            settings_.lumenGi.reflectionSourceResolutionScale=invalid;auto encoder=Begin();RenderFrame empty;
            Check(!pipeline_.Record(encoder,empty,camera_,settings_)&&encoder.Cancel()&&pipeline_.RecordedFrameToken()==0,
                  "Invalid HDR reflection source scale accepted or left a pending frame");
        }
        settings_.lumenGi.reflectionSourceResolutionScale=.5f;Resize(48,32);
        auto sourceRestored=Record();Complete(sourceRestored);checkReflectionSourceExtent(24,16);
        settings_.lumenGi.reflectionResolutionScale=.5f;
        // Recreate only the main/MSAA targets; the hybrid reflection source
        // must retain its own depth attachment at the unchanged view extent.
        settings_.temporal.msaaSamples=4;Resize(48,32);auto lmMsaa=Record();Complete(lmMsaa);
        settings_.temporal.msaaSamples=1;Resize(48,32);auto lmSingleSample=Record();Complete(lmSingleSample);
        settings_.lumenGi.reset=true;auto lmReset=Record();Check(!lmReset.statistics.lumenHistoryUsed&&lmReset.statistics.realtimeGiUpdatedProbes==8,"Hybrid reset retained old world/view history");Complete(lmReset);settings_.lumenGi.reset=false;
        const auto emptyLmScene=settings_.lumenGi.scene;
        const auto pointColors=settings_.lighting.lightColors;
        for(auto& color:settings_.lighting.lightColors)color=glm::vec4(0);
        const ProbeTriangle receiver{{0,0,0},{0,0,4},{4,0,0},glm::vec3(.5f),glm::vec3(0)};
        settings_.lumenGi.scene=BuildLumenGiScene(std::span(&receiver,1));
        settings_.lumenGi.surfaceUpdatesPerFrame=8;
        const auto cacheEnergy=[&] {
            const auto handle=pipeline_.DebugTextures().lumenSurfaceCache;
            const auto pixels=std::make_shared<std::vector<glm::vec4>>(LumenSurfaceWidth*settings_.lumenGi.scene->CacheHeight());
            const auto checked=std::make_shared<Completion>();
            device_.async_ExecuteCode([this,handle,pixels,checked] {
                const auto* texture=device_.GetResourcePool().TextureTable.Get(handle);
                if(!texture){checked->error=GL_INVALID_OPERATION;return;}
                glGetTextureImage(texture->rhi_id,0,GL_RGBA,GL_FLOAT,GLsizei(pixels->size()*sizeof(glm::vec4)),pixels->data());
                checked->error=glGetError();
            },[checked]{checked->done=true;});
            Wait(checked,"hybrid source-off readback");Check(checked->error==GL_NO_ERROR,"Cannot read hybrid cache");
            float energy=0;for(const auto& value:*pixels)energy+=glm::length(glm::vec3(value));return energy;
        };
        auto lmSun=Record();Complete(lmSun);Check(cacheEnergy()>.01f,"Sun did not light hybrid receiver");
        const auto sunColor=settings_.shadows.sunColor;settings_.shadows.sunColor=glm::vec3(0);
        auto lmBlackSun=Record();Complete(lmBlackSun);
        Check(lmBlackSun.statistics.lumenUpdatedSurfels==settings_.lumenGi.scene->SurfelCount()&&cacheEnergy()==0,
              "Black sun retained old radiance instead of clearing the source-free solution");
        settings_.shadows.sunColor=sunColor;settings_.lighting.lightColors=pointColors;
        settings_.lumenGi.scene=emptyLmScene;settings_.lumenGi.surfaceUpdatesPerFrame=512;
        settings_.realtimeGi.enabled=true;auto conflicting=Begin();
        Check(!pipeline_.Record(conflicting,empty,camera_,settings_)&&conflicting.Cancel(),"Conflicting DDGI/hybrid modes accepted");settings_.realtimeGi.enabled=false;
        const auto originalLmScene=settings_.lumenGi.scene;settings_.lumenGi.scene=BuildLumenGiScene({});auto lmChanged=Record();Discard(lmChanged);
        settings_.lumenGi.scene=originalLmScene;auto lmReturned=Record();Check(lmReturned.statistics.realtimeGiUpdatedProbes==8&&!lmReturned.statistics.lumenHistoryUsed,"Recreated Surface Cache read uninitialized old scene history");Complete(lmReturned);
        auto leasedLm=Record();pipeline_.Shutdown();Check(leasedLm.encoder.End(false),"Cannot finish leased hybrid frame");auto lmExecuted=std::make_shared<Completion>();
        Check(device_.async_SubmitFrameCommands(leasedLm.encoder.GetCommandBuffer(),[lmExecuted]{lmExecuted->done=true;}),"Cannot submit leased hybrid frame");Wait(lmExecuted,"hybrid resources after shutdown");
        Check(!pipeline_.CompleteFrame(leasedLm.token),"Shutdown hybrid frame was accepted");
        CheckPipeline(pipeline_.Initialize(),"Hybrid restart");Resize(32,32);auto lmRestart=Record();Check(!lmRestart.statistics.lumenHistoryUsed,"Hybrid restart retained history");Complete(lmRestart);
        const auto worldCache=pipeline_.DebugTextures().realtimeGiCache;
        settings_.outputSize={64,48};auto lmOutput=Record();
        Check(lmOutput.statistics.historyUsed&&lmOutput.statistics.lumenHistoryUsed&&lmOutput.statistics.realtimeGiUpdatedProbes==4,
              "Output-only resize reset internal temporal history or the solved world GI cache");Complete(lmOutput);
        auto lmOutputReused=Record();Complete(lmOutputReused);
        Check(pipeline_.DebugTextures().realtimeGiCache==worldCache,"Output-only change reallocated world probe targets");
        settings_.outputSize={0,0};
        CheckOutputExtent();
        std::cout << "Material pipeline GPU history tests passed: completion, discard, pending reset, resize, restart, sky, leased probes, DDGI and hybrid cache state\n";
    }

private:
    void CheckOutputExtent() {
        settings_=MaterialPipelineSettings{};
        settings_.shadows.enabled=false;settings_.shadows.atlasResolution=128;
        settings_.reflection.enabled=false;settings_.post.bloomEnabled=false;settings_.post.ssaoEnabled=false;
        settings_.post.toneMap=ToneMapMode::None;settings_.post.gamma=1;
        settings_.temporal.antialiasing=AntialiasingMode::None;
        Resize(32,24);
        PipelineDetail::ResourceBuilder builder(device_);
        const std::string vertex=R"GLSL(#version 450 core
layout(location=0) in vec2 position;
void main() {gl_Position=vec4(position,0,1);}
)GLSL";
        const auto solid=builder.FullscreenPipeline(vertex,R"GLSL(#version 450 core
layout(location=0) out vec4 outColor;
void main() {outColor=vec4(.2,.4,.6,1);}
)GLSL");
        const auto checker=builder.FullscreenPipeline(vertex,R"GLSL(#version 450 core
layout(location=0) out vec4 outColor;
void main() {
    bool bright=(int(floor(gl_FragCoord.x/4))+int(floor(gl_FragCoord.y/4)))%2!=0;
    outColor=vec4(bright?vec3(.85,.7,.5):vec3(.15,.25,.4),1);
}
)GLSL");
        const auto overlay=builder.FullscreenPipeline(vertex,R"GLSL(#version 450 core
layout(location=0) out vec4 outColor;
layout(std140,binding=10) uniform OutputDiagnostic {vec4 outputExtent;};
void main() {
    if(gl_FragCoord.x<outputExtent.x-4 || gl_FragCoord.y<outputExtent.y-4) discard;
    outColor=vec4(0,1,0,1);
}
)GLSL");
        const auto mesh=builder.FullscreenMesh();
        const auto extent=builder.Uniform(sizeof(glm::vec4));
        const auto owner=builder.Finish();
        struct Image:Completion {
            std::array<GLint,4> viewport{};
            std::vector<glm::vec4> pixels=std::vector<glm::vec4>(96*64);
            GLint internalWidth=0,internalHeight=0;
        };
        const auto draw=[&](RenderResourceHandle<PipelineSpec> scenePipeline,bool withOverlay=false) {
            auto encoder=Begin();
            const glm::uvec2 output=settings_.outputSize.x?settings_.outputSize:glm::uvec2(width_,height_);
            RenderFrame frame;frame.frameIndex=frameIndex_;
            RenderItem item;item.mesh=mesh;item.pipeline=scenePipeline;item.draw.indexCount=3;
            frame.items.push_back(item);
            if(withOverlay) {item.pipeline=overlay;item.layer=RenderLayer::Overlay;frame.items.push_back(item);}
            try {
                RHICommand::SetRenderTarget sentinel;sentinel.width=96;sentinel.height=64;
                sentinel.clearFlags=RHICommand::ClearColor;sentinel.clearColor={1,0,1,1};
                Check(encoder.KeepAlive(owner)&&encoder.SetRenderTarget(sentinel)&&
                      encoder.UpdateUniformBuffer(extent,glm::vec4(output.x,output.y,0,0))&&
                      encoder.BindUniformBuffer(extent,10),"Cannot prepare output diagnostic");
                CheckPipeline(pipeline_.Record(encoder,frame,camera_,settings_),"Output Record");
                Recorded recorded{std::move(encoder),pipeline_.RecordedFrameToken(),pipeline_.Statistics()};
                Complete(recorded);
                auto image=std::make_shared<Image>();
                const auto internal=pipeline_.DebugTextures().sceneColor;
                device_.async_ExecuteCode([this,image,internal] {
                    glGetIntegerv(GL_VIEWPORT,image->viewport.data());
                    const auto* texture=device_.GetResourcePool().TextureTable.Get(internal);
                    if(!texture) {image->error=GL_INVALID_VALUE;return;}
                    glGetTextureLevelParameteriv(texture->rhi_id,0,GL_TEXTURE_WIDTH,&image->internalWidth);
                    glGetTextureLevelParameteriv(texture->rhi_id,0,GL_TEXTURE_HEIGHT,&image->internalHeight);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_BACK);
                    glReadPixels(0,0,96,64,GL_RGBA,GL_FLOAT,image->pixels.data());
                    image->error=glGetError();
                },[image] {image->done=true;});
                Wait(image,"Output diagnostic readback");
                Check(image->error==GL_NO_ERROR,"Cannot read final output framebuffer");
                Check(image->viewport==std::array<GLint,4>{0,0,GLint(output.x),GLint(output.y)},
                      "Presentation/overlay viewport did not use output extent");
                Check(image->internalWidth==GLint(width_)&&image->internalHeight==GLint(height_),
                      "Output extent resized scene/depth working resolution");
                // Undrawn pixels retain the sentinel. A cropped presentation
                // or accidentally native-sized internal target cannot pass.
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<96;++x)
                    if(x>=output.x||y>=output.y) Check(image->pixels[y*96+x]==glm::vec4(1,0,1,1),
                        "Presentation wrote outside its output extent");
                return std::pair{image,recorded.statistics};
            } catch(...) {encoder.Cancel();throw;}
        };
        const auto checkSolid=[&](const Image& image,bool withOverlay=false) {
            const auto output=settings_.outputSize.x?settings_.outputSize:glm::uvec2(width_,height_);
            for(unsigned y=0;y<output.y;++y) for(unsigned x=0;x<output.x;++x) {
                const glm::vec3 expected=withOverlay&&x>=output.x-4&&y>=output.y-4?
                    glm::vec3(0,1,0):glm::vec3(.2f,.4f,.6f);
                Check(glm::length(glm::vec3(image.pixels[y*96+x])-expected)<.009f,
                      "Output copy/reconstruction changed a solid color or cropped an output edge");
            }
        };
        auto native=draw(solid);checkSolid(*native.first);
        const auto internalHandle=pipeline_.DebugTextures().sceneColor;
        settings_.outputSize={64,48};auto enlarged=draw(solid);checkSolid(*enlarged.first);
        Check(pipeline_.DebugTextures().sceneColor==internalHandle,"Presentation change reallocated internal targets");
        settings_.outputSize={80,60};auto larger=draw(solid,true);checkSolid(*larger.first,true);
        Check(pipeline_.DebugTextures().sceneColor==internalHandle,"Output resize retired unchanged internal history textures");
        settings_.outputSize={64,48};settings_.temporal.antialiasing=AntialiasingMode::FXAA;
        auto fxaa=draw(solid);checkSolid(*fxaa.first);
        Check(fxaa.second.postPasses==enlarged.second.postPasses+1,"Upscaled FXAA did not run at internal extent");
        settings_.temporal.antialiasing=AntialiasingMode::TAA;
        auto taaFirst=draw(solid);checkSolid(*taaFirst.first);
        auto taaReused=draw(solid);Check(taaReused.second.historyUsed,"Upscaled output failed to retain internal TAA history");
        settings_.outputSize={80,60};auto taaOutputResize=draw(solid,true);checkSolid(*taaOutputResize.first,true);
        Check(taaOutputResize.second.historyUsed,"Changing output extent invalidated unchanged internal TAA history");
        settings_.temporal.antialiasing=AntialiasingMode::None;settings_.outputSize={64,48};
        auto pattern=draw(checker);
        const auto sourceColor=[](int x,int y,unsigned width,unsigned height) {
            x=std::clamp(x,0,int(width)-1);y=std::clamp(y,0,int(height)-1);
            return (x/4+y/4)%2?glm::dvec3(.85,.7,.5):glm::dvec3(.15,.25,.4);
        };
        // Integrate the cubic cardinal basis over actual source texel colors,
        // then compare the full production scene/post/output chain on the GPU.
        const auto cubic=[](double x) {
            x=std::abs(x);
            return x<1?1.5*x*x*x-2.5*x*x+1:
                   (x<2?-.5*x*x*x+2.5*x*x-4*x+2:0);
        };
        const auto checkReconstruction=[&](const Image& image,const auto& sample,double tolerance) {
            for(unsigned y=0;y<48;++y) for(unsigned x=0;x<64;++x) {
                const glm::dvec2 p((x+.5)*width_/64.0-.5,(y+.5)*height_/48.0-.5);
                const glm::ivec2 base(glm::floor(p));glm::dvec3 expected(0);
                double total=0;glm::dvec3 lo(1),hi(0);
                for(int j=-1;j<=2;++j) for(int i=-1;i<=2;++i) {
                    const auto color=sample(base.x+i,base.y+j);
                    const double w=cubic(p.x-base.x-i)*cubic(p.y-base.y-j);
                    expected+=color*w;total+=w;
                    if(i>=0&&i<=1&&j>=0&&j<=1) {lo=glm::min(lo,color);hi=glm::max(hi,color);}
                }
                expected=glm::clamp(expected/total,lo,hi);
                Check(glm::length(glm::dvec3(image.pixels[y*96+x])-expected)<tolerance,
                      "Checker reconstruction shifted texel coordinates or produced ringing");
            }
        };
        checkReconstruction(*pattern.first,[&](int x,int y) {return sourceColor(x,y,width_,height_);},.009);
        settings_.outputSize={0,0};settings_.temporal.antialiasing=AntialiasingMode::FXAA;
        const auto fxaaNative=draw(checker);
        double antialiasingDifference=0;
        for(unsigned y=0;y<height_;++y) for(unsigned x=0;x<width_;++x)
            antialiasingDifference+=glm::length(glm::dvec3(fxaaNative.first->pixels[y*96+x])-sourceColor(x,y,width_,height_));
        Check(antialiasingDifference/(width_*height_)>.005,"Checker did not exercise FXAA edge filtering");
        settings_.outputSize={64,48};const auto fxaaEnlarged=draw(checker);
        // The unscaled real FXAA output is an independent GPU reference for
        // its internal-pixel filter. Allow the two 8-bit readback quantizations.
        checkReconstruction(*fxaaEnlarged.first,[&](int x,int y) {
            x=std::clamp(x,0,int(width_)-1);y=std::clamp(y,0,int(height_)-1);
            return glm::dvec3(fxaaNative.first->pixels[std::size_t(y)*96+x]);
        },.015);
        settings_.temporal.antialiasing=AntialiasingMode::None;
        Resize(24,18);auto resized=draw(solid);checkSolid(*resized.first);
        Check(pipeline_.DebugTextures().sceneColor!=internalHandle,"Internal resize failed to replace its scene target");
        Resize(31,23);settings_.outputSize={79,59};
        auto oddOutput=draw(solid,true);checkSolid(*oddOutput.first,true);
        settings_.temporal.antialiasing=AntialiasingMode::TAA;
        auto oddTaa=draw(solid);checkSolid(*oddTaa.first);
        auto oddTaaReused=draw(solid);Check(oddTaaReused.second.historyUsed,"Odd internal extent could not accumulate TAA");
        Resize(29,21);auto oddResizedTaa=draw(solid);checkSolid(*oddResizedTaa.first);
        Check(!oddResizedTaa.second.historyUsed,"Internal resize retained mismatched TAA history");
        auto oddResizedReuse=draw(solid);Check(oddResizedReuse.second.historyUsed,"Resized internal history did not recover");
        settings_.temporal.antialiasing=AntialiasingMode::None;
        for(const auto invalid:{glm::uvec2(0,48),glm::uvec2(64,0),glm::uvec2(8193,1),glm::uvec2(1,8193)}) {
            settings_.outputSize=invalid;auto encoder=Begin();RenderFrame empty;
            Check(!pipeline_.Record(encoder,empty,camera_,settings_),"Invalid output extent accepted");
            Check(encoder.Cancel()&&pipeline_.RecordedFrameToken()==0,"Invalid output extent retained a pending frame");
        }
        settings_.outputSize={0,0};auto restored=draw(solid);checkSolid(*restored.first);
        std::cout<<"Output GPU tests passed: legacy extent, solid/checker bounds, native overlay, FXAA/TAA, output-only history reuse, internal resize and invalid extent\n";
    }
    void CheckPipeline(bool success, const char* operation) {
        Check(success, std::string(operation) + ": " + pipeline_.LastError());
    }
    void SetCameraX(float x) {
        camera_.position = {x, 0, 5};
        camera_.view = glm::lookAt(camera_.position, glm::vec3(x, 0, 0), glm::vec3(0, 1, 0));
    }
    void Resize(std::uint32_t width, std::uint32_t height) {
        CheckPipeline(pipeline_.Resize(width, height, 128, settings_.temporal.msaaSamples, 1), "Resize");
        width_ = width; height_ = height;
        camera_.projection = glm::perspective(glm::radians(50.0f), float(width) / height, 0.1f, 100.0f);
    }
    RHIFrameEncoder Begin() {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = settings_.outputSize.x ? settings_.outputSize.x : width_;
        begin.framebufferHeight = settings_.outputSize.y ? settings_.outputSize.y : height_;
        return device_.BeginFrame(begin);
    }
    Recorded Record() {
        auto encoder = Begin();
        try {
            RenderFrame empty; empty.frameIndex = frameIndex_;
            CheckPipeline(pipeline_.Record(encoder, empty, camera_, settings_), "Record");
            const auto token = pipeline_.RecordedFrameToken();
            Check(token != 0, "A successfully recorded frame must have a token");
            return {std::move(encoder), token, pipeline_.Statistics()};
        } catch (...) { encoder.Cancel(); throw; }
    }
    void CheckPendingGuards(std::uint64_t token) {
        auto blockedEncoder = Begin();
        RenderFrame empty;
        const bool recorded = pipeline_.Record(blockedEncoder, empty, camera_, settings_);
        const bool cancelled = blockedEncoder.Cancel();
        Check(!recorded && cancelled, "A second Record must reject a pending frame without leaking its encoder");
        Check(!pipeline_.Resize(width_ + 1, height_, 128, 1, 1), "Resize must reject a pending frame");
        Check(!pipeline_.CompleteFrame(token + 1) && !pipeline_.DiscardFrame(token + 1),
              "Unknown future tokens must be rejected");
        Check(pipeline_.RecordedFrameToken() == token, "Failed operations must preserve the pending token");
    }
    void Discard(Recorded& recorded) {
        Check(recorded.encoder.Cancel(), "Cannot cancel an unsubmitted frame");
        CheckPipeline(pipeline_.DiscardFrame(recorded.token), "Discard");
        Check(pipeline_.RecordedFrameToken() == 0, "Discard must consume its pending token");
    }
    void Complete(Recorded& recorded) {
        Check(recorded.encoder.End(false), "Cannot finish history frame");
        auto completed = std::make_shared<Completion>();
        Check(device_.async_SubmitFrameCommands(recorded.encoder.GetCommandBuffer(),
            [completed] { completed->done = true; }), "Cannot reliably submit history frame");
        Wait(completed, "history frame execution");
        CheckPipeline(pipeline_.CompleteFrame(recorded.token), "Complete");
        Check(pipeline_.RecordedFrameToken() == 0, "Complete must consume its pending token");
        auto checked = std::make_shared<Completion>();
        device_.async_ExecuteCode([checked] { checked->error = glGetError(); },
            [checked] { checked->done = true; });
        Wait(checked, "history frame GL validation");
        Check(checked->error == GL_NO_ERROR, "OpenGL error in real history passes: " + std::to_string(checked->error));
    }
    void Wait(const std::shared_ptr<Completion>& completion, const char* operation) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!completion->done) {
            device_.returnSystem.DrainCallbacks();
            if (completion->done) return;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timed out waiting for ") + operation);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(2ms);
        }
    }

    RHIDevice device_; // Must outlive the pipeline, callbacks and recording leases.
    MaterialRenderPipeline pipeline_;
    MaterialPipelineSettings settings_;
    PipelineCamera camera_;
    std::uint32_t width_ = 64, height_ = 64;
    std::uint64_t frameIndex_ = 0;
};
}

int main() {
    try {
        Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        {
            ApplicationWindow::Window window({96, 64, "Material history test", false});
            Check(window.Activate(), "Cannot create hidden OpenGL window");
            const auto context = window.GetRenderContextAsOpengl();
            Check(context.has_value(), "Hidden window has no OpenGL render context");
            HistoryFixture fixture(*context);
            fixture.Run();
        }
        glfwTerminate();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Material pipeline history test failed: " << error.what() << '\n';
        glfwTerminate();
        return 1;
    }
}
