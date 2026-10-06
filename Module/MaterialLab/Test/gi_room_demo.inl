// Included in main.cpp's anonymous namespace; uses the shared CLI, captures
// and frame pacing. This room deliberately contains no legacy texture assets.
FrameSettings MakeGiRoomFrame(const Options& options) {
    FrameSettings frame;
    frame.giRoom = true;
    frame.effects.lumenGi.lightingView = Render::LumenLightingView::SurfaceCache;
    frame.effects.lumenGi.fullResolutionDirectLighting = true;
    frame.effects.lumenGi.traceDirectLighting = true;
    frame.effects.lumenGi.surfaceRays = 32;
    frame.effects.lumenGi.surfaceUpdatesPerFrame = 1024;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
    frame.effects.temporal.msaaSamples = options.msaaSamples;
    frame.effects.temporal.motionBlurEnabled = false;
    frame.effects.post.bloomEnabled = false;
    frame.effects.post.ssaoEnabled = false;
    frame.effects.post.filter = Render::PostFilter::None;
    frame.effects.post.toneMap = Render::ToneMapMode::ACES;
    frame.effects.post.vignette = 0;
    frame.effects.spotLight.intensity = {350, 350, 350};
    frame.effects.spotLight.sourceRadius = .04f;
    frame.effects.spotLight.innerAngle = glm::radians(10.f);
    frame.effects.spotLight.outerAngle = glm::radians(22.f);
    ConfigureGiRoomLighting(frame.effects, frame.giRoomState);
    return frame;
}
void GiRoomSetView(FrameSettings& frame, Render::LumenLightingView view) {
    frame.effects.lumenGi.lightingView = view;
    frame.giRoomState.indirectOnly = view == Render::LumenLightingView::IndirectOnly;
}
const char* GiRoomViewName(Render::LumenLightingView view) {
    switch (view) {
    case Render::LumenLightingView::SurfaceCache: return "GI full";
    case Render::LumenLightingView::IndirectOnly: return "GI bounce only";
    case Render::LumenLightingView::DirectOnly: return "GI direct only";
    default: return "Material";
    }
}
void GiRoomSave(MaterialLabModule& lab, const Options& options, const char* label) {
    if (options.screenshot.empty()) return;
    auto path = std::filesystem::path(options.screenshot);
    path.replace_filename(path.stem().string() + "_" + label + path.extension().string());
    Save(lab.Readback(), path.string());
}
double GiRoomMean(const std::vector<glm::vec4>& image, std::size_t count = 0) {
    Check(!image.empty(), "GI room linear diagnostic is empty");
    if (!count) count = image.size();
    Check(count <= image.size(), "GI room linear diagnostic is truncated");
    double sum = 0;
    for (std::size_t pixel = 0; pixel < count; ++pixel) for (unsigned channel = 0; channel < 3; ++channel) {
        const float value = image[pixel][channel];
        Check(std::isfinite(value) && value >= -.0001f, "GI room contains nonfinite or negative radiance");
        sum += value;
    }
    return sum / (count * 3.);
}
double GiRoomLinearDifference(const std::vector<glm::vec4>& a, const std::vector<glm::vec4>& b,
                             std::size_t count = 0) {
    Check(a.size() == b.size() && !a.empty(), "GI room diagnostic dimensions differ");
    if (!count) count = a.size();
    Check(count <= a.size(), "GI room diagnostic comparison is truncated");
    double sum = 0;
    for (std::size_t pixel = 0; pixel < count; ++pixel) for (unsigned channel = 0; channel < 3; ++channel)
        sum += std::abs(double(a[pixel][channel]) - b[pixel][channel]);
    return sum / (count * 3.);
}
Render::PipelineCamera GiRoomAim(glm::vec3 eye, glm::vec3 target, float aspect) {
    auto camera = GiRoomCameraPreset(0, aspect);
    camera.position = ClampGiRoomCamera(eye);
    camera.view = glm::lookAt(camera.position, target, glm::vec3(0, 1, 0));
    return camera;
}
void GiRoomQuality(MaterialLabModule& lab, const Options& options) {
    auto frame = MakeGiRoomFrame(options);
    frame.giRoomHud = false;
    frame.present = false;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    SetShowcaseResolution(frame, options.studySizeSet ? options.benchmarkWidth : 960,
                          options.studySizeSet ? options.benchmarkHeight : 600, options.renderScale);
    frame.camera = GiRoomCameraPreset(0, frame.OutputAspect());
    unsigned renderedFrames=0;
    const auto render = [&] {
        ++renderedFrames;
        if(!lab.Render(frame))throw std::runtime_error("GI room frame "+std::to_string(renderedFrames)+": "+lab.LastError());
        frame.effects.cameraCut = false;
    };
    const auto settle = [&](unsigned count) {
        for (unsigned index = 0; index < count; ++index) {
            render();
            if (!options.vulkan && index % 6 == 5) Check(!lab.Readback().rgba.empty(), lab.LastError());
        }
    };
    const auto read = [&] {
        auto image = lab.ReadbackLumenGather();
        Check(image.size() == std::size_t(frame.width) * frame.height,
              "GI room view diagnostic does not use the internal extent");
        GiRoomMean(image);
        return image;
    };
    const auto identity = [&] {
        const auto scene = lab.PreparedLumenGiScene();
        Check(bool(scene), "GI room has no world tracing scene");
        return scene;
    };
    settle(128);
    const auto scene = identity();
    const auto topology = scene->Geometry()->TopologyKey();
    Check(scene->Geometry()->TriangleCount() == 48, "GI room unexpectedly contains extra scene geometry");
    const auto stats = lab.PipelineStatistics();
    Check(stats.lumenGiPasses > 0 && stats.shadowPasses == 0 && stats.reflectionPasses == 0 &&
          stats.skyPasses == 0 && stats.transparentPasses == 0 && stats.indirectPasses == 0,
          "GI room retained an unrelated lighting or reflection pass");
    const auto full = read();
    const auto fullMean = GiRoomMean(full);
    const auto pointImage = lab.Readback();
    Check(pointImage.width == frame.OutputExtent().x && pointImage.height == frame.OutputExtent().y,
          "GI room did not preserve its output resolution");
    GiRoomSave(lab, options, "point_full");

    GiRoomSetView(frame, Render::LumenLightingView::DirectOnly); render();
    const auto direct = read();
    GiRoomSave(lab, options, "point_direct");
    GiRoomSetView(frame, Render::LumenLightingView::IndirectOnly); render();
    const auto indirect = read();
    GiRoomSave(lab, options, "point_bounce");
    const auto directMean = GiRoomMean(direct), indirectMean = GiRoomMean(indirect);
    Check(fullMean > .001 && directMean > .001 && indirectMean > .001,
          "GI room point source did not produce direct and bounced radiance");
    double decomposition = 0;
    for (std::size_t pixel = 0; pixel < full.size(); ++pixel) for (unsigned channel = 0; channel < 3; ++channel)
        decomposition += std::abs(double(full[pixel][channel]) - direct[pixel][channel] - indirect[pixel][channel]);
    decomposition /= full.size() * 3 * fullMean;
    Check(decomposition < .05, "Full GI view is not the direct plus indirect solution");
    Check(identity()->Geometry()->TopologyKey() == topology && identity().get() == scene.get(),
          "Switching diagnostic display modes rebuilt the world scene");
    std::cout << "GI room linear point energy: full=" << fullMean << ", direct=" << directMean
              << ", bounce=" << indirectMean << ", decomposition error=" << decomposition * 100 << "%\n";

    // Translation must reach the immutable refit before this same submission.
    GiRoomSetView(frame, Render::LumenLightingView::SurfaceCache); render();
    const auto beforeMove = identity();
    const auto beforeDirect = lab.ReadbackLumenSurfaceCache(true);
    const auto oldCenter = GiRoomMovable(2).position + frame.giRoomState.offsets[2];
    Check(MoveGiRoomObject(frame.giRoomState, 2, {.8f, 0, -.6f}), "Blocker movement was rejected");
    const auto delta = GiRoomMovable(2).position + frame.giRoomState.offsets[2] - oldCenter;
    render();
    const auto afterMove = identity();
    Check(afterMove->Geometry()->TopologyKey() == topology && afterMove->SurfelCount() == beforeMove->SurfelCount(),
          "Blocker movement discarded the surface cache topology");
    const auto& oldGeometry = *beforeMove->Geometry();
    const auto& newGeometry = *afterMove->Geometry();
    Check(oldGeometry.TriangleCount() == newGeometry.TriangleCount() &&
          oldGeometry.NodeCount() == newGeometry.NodeCount(), "Blocker refit changed tracing topology");
    unsigned movedTriangles = 0;
    for (unsigned triangle = 0; triangle < oldGeometry.TriangleCount(); ++triangle) {
        const auto offset = oldGeometry.NodeCount() * 2 + triangle * 5;
        bool unchanged = true, translated = true;
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto change = glm::vec3(newGeometry.Pixels()[offset + corner] - oldGeometry.Pixels()[offset + corner]);
            unchanged &= glm::length(change) < 1e-4f;
            translated &= glm::length(change - delta) < 1e-4f;
        }
        Check(unchanged || translated, "World tracing geometry does not match the displayed blocker pose");
        if (translated) ++movedTriangles;
    }
    Check(movedTriangles == 12, "Blocker movement did not refit all twelve triangles in the same frame");
    const auto afterDirect = lab.ReadbackLumenSurfaceCache(true);
    const auto moveResponse = GiRoomLinearDifference(beforeDirect, afterDirect, afterMove->SurfelCount());
    Check(moveResponse > .00001, "Moving blocker did not update world-space illumination in the same frame");
    GiRoomSave(lab, options, "moved_blocker");
    std::cout << "GI room moving blocker: refit triangles=" << movedTriangles
              << ", direct-cache response=" << moveResponse << ", geometry latency=0 frames\n";

    frame.giRoomState.pointLight = frame.giRoomState.flashlight = false;
    render(); render();
    const auto dark = read();
    const auto darkCache = lab.ReadbackLumenSurfaceCache();
    Check(GiRoomMean(dark) < 1e-7 && GiRoomMean(darkCache, identity()->SurfelCount()) < 1e-7,
          "Sealed room remains lit when both real light sources are disabled");
    const auto darkImage = lab.Readback();
    for (std::size_t pixel = 0; pixel < darkImage.rgba.size(); pixel += 4)
        Check(darkImage.rgba[pixel] <= 1 && darkImage.rgba[pixel + 1] <= 1 && darkImage.rgba[pixel + 2] <= 1,
              "Unlit room retained an environment fill or post-processing light");
    GiRoomSave(lab, options, "dark");

    frame.giRoomState.flashlight = true;
    frame.camera = GiRoomAim({0, 3.6f, 3.25f}, {-2.5f, 3.4f, -4}, frame.OutputAspect());
    frame.effects.cameraCut = true;
    settle(64);
    const auto flashScene = identity();
    const auto flashLeft = lab.ReadbackLumenSurfaceCache(true);
    Check(GiRoomMean(read()) > .001, "Flashlight-only room is black");
    GiRoomSave(lab, options, "flashlight_left");
    frame.camera = GiRoomAim({0, 3.6f, 3.25f}, {2.5f, 3.4f, -4}, frame.OutputAspect());
    frame.effects.cameraCut = true; render();
    const auto flashRight = lab.ReadbackLumenSurfaceCache(true);
    const auto flashResponse = GiRoomLinearDifference(flashLeft, flashRight, flashScene->SurfelCount());
    Check(identity().get() == flashScene.get() && identity()->Geometry()->TopologyKey() == topology,
          "Turning the flashlight rebuilt the world scene");
    Check(flashResponse > .001, "Turning the flashlight did not move the world-space beam in the same frame");
    settle(32);
    GiRoomSave(lab, options, "flashlight_right");
    GiRoomSetView(frame, Render::LumenLightingView::IndirectOnly); render();
    Check(GiRoomMean(read()) > .0001, "Flashlight spot did not bounce onto surrounding room surfaces");
    GiRoomSave(lab, options, "flashlight_bounce");
    std::cout << "GI room flashlight: direction-only direct-cache response=" << flashResponse
              << ", beam latency=0 frames, scene unchanged\n";
    frame.giRoomHud=true;render();GiRoomSave(lab,options,"hud");frame.giRoomHud=false;

    // Reproduce the thin blocker's front/top/right corner at close range.
    // Both sources are in the visible faces' positive hemispheres, so adding
    // the point lamp must never erase a bright flashlight edge. Direct-only
    // captures remove convergence/tone-mapping from the numeric comparison;
    // full-solution captures retain the actual user-facing presentation.
    frame.giRoomState = GiRoomSettings{};
    const auto& blocker = GiRoomMovable(2);
    const auto blockerModel = GiRoomTransform(blocker, frame.giRoomState);
    const auto blockerInverse = glm::inverse(blockerModel);
    const auto blockerRotation = glm::rotate(glm::mat4(1), blocker.yaw, glm::vec3(0, 1, 0));
    const auto localWorld = [&](glm::vec3 local) {
        return blocker.position + glm::vec3(blockerRotation * glm::vec4(local, 0));
    };
    frame.giRoomState.lightPosition = localWorld({1.9f, 1.8f, 1.8f});
    frame.camera = GiRoomAim(localWorld({1.45f, 2.1f, 2.2f}), localWorld({.15f, 1.2f, .1f}), frame.OutputAspect());
    frame.effects.cameraCut = true;
    const auto closeCamera = *frame.camera;
    std::array<std::vector<glm::vec4>, 3> cornerDirect, cornerFull;
    constexpr std::array<const char*, 3> cornerLabels{{"blocker_point", "blocker_flashlight", "blocker_combined"}};
    for (unsigned configuration = 0; configuration < 3; ++configuration) {
        frame.giRoomState.pointLight = configuration != 1;
        frame.giRoomState.flashlight = configuration != 0;
        GiRoomSetView(frame, Render::LumenLightingView::SurfaceCache);
        settle(64);
        cornerFull[configuration] = read();
        GiRoomSave(lab, options, cornerLabels[configuration]);
        GiRoomSetView(frame, Render::LumenLightingView::DirectOnly); render();
        cornerDirect[configuration] = read();
        const std::string directLabel = std::string(cornerLabels[configuration]) + "_direct";
        GiRoomSave(lab, options, directLabel.c_str());
    }

    const auto closeScene = identity();
    const auto& closeGeometry = *closeScene->Geometry();
    const auto& closeTriangles = closeGeometry.Pixels();
    const auto triangleOffset = [&](unsigned triangle) { return closeGeometry.NodeCount() * 2 + triangle * 5; };
    const auto intersectTriangle = [&](glm::vec3 origin, glm::vec3 direction, unsigned triangle, float limit) {
        const auto offset = triangleOffset(triangle);
        const auto a = glm::vec3(closeTriangles[offset]);
        const auto ab = glm::vec3(closeTriangles[offset + 1]) - a, ac = glm::vec3(closeTriangles[offset + 2]) - a;
        const auto h = glm::cross(direction, ac);
        const float determinant = glm::dot(ab, h);
        if (std::abs(determinant) < 1e-8f) return limit;
        const float inverse = 1 / determinant;
        const auto displacement = origin - a;
        const float u = glm::dot(displacement, h) * inverse;
        if (u < -1e-6f || u > 1 + 1e-6f) return limit;
        const auto q = glm::cross(displacement, ab);
        const float v = glm::dot(direction, q) * inverse;
        if (v < -1e-6f || u + v > 1 + 1e-6f) return limit;
        const float distance = glm::dot(ac, q) * inverse;
        return distance > .0001f && distance < limit ? distance : limit;
    };
    const auto visibleSource = [&](glm::vec3 world, glm::vec3 normal, glm::vec3 source, float radius) {
        const auto displacement = source - world;
        const float distance = glm::length(displacement);
        const auto direction = displacement / distance;
        const auto origin = world + normal * .002f;
        const float limit = std::max(0.f, distance - radius - .004f);
        for (unsigned triangle = 0; triangle < closeGeometry.TriangleCount(); ++triangle)
            if (intersectTriangle(origin, direction, triangle, limit) < limit) return false;
        return true;
    };
    const auto physicalDirect = [&](glm::vec3 world, glm::vec3 normal, glm::vec3 rho, bool point, bool torch) {
        glm::vec3 radiance(0);
        if (point) {
            const auto displacement = frame.giRoomState.lightPosition - world;
            const float squaredDistance = glm::dot(displacement, displacement);
            const float cosine = std::max(0.f, glm::dot(normal, displacement / std::sqrt(squaredDistance)));
            if (cosine > 0 && visibleSource(world, normal, frame.giRoomState.lightPosition, frame.giRoomState.lightRadius))
                radiance += frame.giRoomState.lightRadiance * cosine /
                    std::max(squaredDistance, frame.giRoomState.lightRadius * frame.giRoomState.lightRadius);
        }
        if (torch) {
            const auto forward = -glm::vec3(glm::inverse(frame.camera->view)[2]);
            const auto torchPosition = ClampGiRoomCamera(frame.camera->position + forward * .08f,
                                                        std::max(.12f, frame.effects.spotLight.sourceRadius + .02f));
            const auto displacement = torchPosition - world;
            const float squaredDistance = glm::dot(displacement, displacement);
            const auto toSource = displacement / std::sqrt(squaredDistance);
            const float cosine = std::max(0.f, glm::dot(normal, toSource));
            const float inner = std::cos(frame.effects.spotLight.innerAngle), outer = std::cos(frame.effects.spotLight.outerAngle);
            const float phase = std::clamp((glm::dot(forward, -toSource) - outer) / (inner - outer), 0.f, 1.f);
            const float cone = phase * phase * (3 - 2 * phase);
            if (cosine > 0 && cone > 0 && visibleSource(world, normal, torchPosition, frame.effects.spotLight.sourceRadius))
                radiance += frame.effects.spotLight.intensity * (cosine * cone) /
                    std::max(squaredDistance, frame.effects.spotLight.sourceRadius * frame.effects.spotLight.sourceRadius);
        }
        return rho * radiance / 3.14159265358979323846f;
    };
    const auto luma = [](glm::vec3 value) { return double(glm::dot(value, glm::vec3(.2126f, .7152f, .0722f))); };
    struct CornerStatistics {
        unsigned samples = 0, blackPixels = 0, monotonicPixels = 0;
        double expected = 0, measured = 0, monotonicLoss = 0, fullLoss = 0;
    };
    std::array<CornerStatistics, 3> cornerStatistics{};
    const auto closeVP = closeCamera.projection * closeCamera.view, inverseCloseVP = glm::inverse(closeVP);
    glm::vec2 screenMinimum(float(frame.width), float(frame.height)), screenMaximum(0);
    for (int z : {-1, 1}) for (int y : {-1, 1}) for (int x : {-1, 1}) {
        const auto projected = closeVP * blockerModel * glm::vec4(x, y, z, 1);
        const auto screen = (glm::vec2(projected) / projected.w * .5f + .5f) * glm::vec2(frame.width, frame.height);
        screenMinimum = glm::min(screenMinimum, screen); screenMaximum = glm::max(screenMaximum, screen);
    }
    const auto minimumPixel = glm::clamp(glm::ivec2(glm::floor(screenMinimum)), glm::ivec2(0), glm::ivec2(frame.width - 1, frame.height - 1));
    const auto maximumPixel = glm::clamp(glm::ivec2(glm::ceil(screenMaximum)), glm::ivec2(0), glm::ivec2(frame.width - 1, frame.height - 1));
    for (int y = minimumPixel.y; y <= maximumPixel.y; ++y) for (int x = minimumPixel.x; x <= maximumPixel.x; ++x) {
        const auto ndc = (glm::vec2(x, y) + .5f) / glm::vec2(frame.width, frame.height) * 2.f - 1.f;
        const auto farPoint = inverseCloseVP * glm::vec4(ndc, 1, 1);
        const auto direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - closeCamera.position);
        float distance = closeCamera.farPlane; int receiver = -1;
        for (unsigned triangle = 0; triangle < closeGeometry.TriangleCount(); ++triangle) {
            const float hit = intersectTriangle(closeCamera.position, direction, triangle, distance);
            if (hit < distance) { distance = hit; receiver = int(triangle); }
        }
        if (receiver < 0) continue;
        const auto world = closeCamera.position + direction * distance;
        const auto local = glm::vec3(blockerInverse * glm::vec4(world, 1));
        int face = -1;
        // Stay inside each silhouette by several percent. The top/side strips
        // retain the narrow front edge while excluding mixed-face raster pixels.
        if (std::abs(local.z - 1) < .0001f && std::abs(local.x) < .93f && local.y > .75f && local.y < .96f) face = 0;
        else if (std::abs(local.y - 1) < .0001f && std::abs(local.x) < .93f && local.z > .3f && local.z < .9f) face = 1;
        else if (std::abs(local.x - 1) < .0001f && local.y > .55f && local.y < .96f && local.z > .3f && local.z < .9f) face = 2;
        if (face < 0) continue;
        const auto offset = triangleOffset(unsigned(receiver));
        const auto normal = glm::normalize(glm::cross(glm::vec3(closeTriangles[offset + 1] - closeTriangles[offset]),
                                                      glm::vec3(closeTriangles[offset + 2] - closeTriangles[offset])));
        const auto rho = glm::vec3(closeTriangles[offset + 3]);
        const double expectedPoint = luma(physicalDirect(world, normal, rho, true, false));
        const double expectedFlash = luma(physicalDirect(world, normal, rho, false, true));
        // Only test a genuinely illuminated beam footprint with independent
        // CPU visibility; physical cone falloff or an occluder is not a black edge.
        if (expectedFlash < .05 || expectedPoint < .005) continue;
        const double expected = expectedPoint + expectedFlash;
        const auto pixel = std::size_t(y) * frame.width + x;
        const double pointValue = luma(glm::vec3(cornerDirect[0][pixel]));
        const double flashValue = luma(glm::vec3(cornerDirect[1][pixel]));
        const double combinedValue = luma(glm::vec3(cornerDirect[2][pixel]));
        const double fullValue = luma(glm::vec3(cornerFull[2][pixel]));
        auto& result = cornerStatistics[unsigned(face)];
        ++result.samples; result.expected += expected; result.measured += combinedValue;
        if (combinedValue < expected * .15) ++result.blackPixels;
        const double strongest = std::max(pointValue, flashValue);
        const double loss = std::max(0., strongest - combinedValue - .001);
        result.monotonicLoss += loss;
        if (loss > strongest * .02) ++result.monotonicPixels;
        result.fullLoss += std::max(0., combinedValue - fullValue - .001);
    }
    constexpr std::array<const char*, 3> faceNames{{"front top edge", "top front edge", "right front edge"}};
    for (unsigned face = 0; face < cornerStatistics.size(); ++face) {
        const auto& result = cornerStatistics[face];
        const double monotonicLoss = result.monotonicLoss / std::max(result.measured, .001);
        const double fullLoss = result.fullLoss / std::max(result.measured, .001);
        std::cout << "GI room thin-blocker " << faceNames[face] << ": samples=" << result.samples
                  << ", direct/physical=" << result.measured / std::max(result.expected, .001)
                  << ", black pixels=" << result.blackPixels << ", adding-light loss=" << monotonicLoss * 100
                  << "%, full-below-direct=" << fullLoss * 100 << "%\n";
    }
    for (unsigned face = 0; face < cornerStatistics.size(); ++face) {
        const auto& result = cornerStatistics[face];
        const double monotonicLoss = result.monotonicLoss / std::max(result.measured, .001);
        const double fullLoss = result.fullLoss / std::max(result.measured, .001);
        Check(result.samples >= 4, std::string("Thin-blocker test has no meaningful footprint on ") + faceNames[face]);
        Check(result.blackPixels <= result.samples * .05,
              std::string("Strong flashlight produced a physically lit black edge on ") + faceNames[face]);
        Check(monotonicLoss < .002 && result.monotonicPixels <= result.samples * .01,
              std::string("Adding the point source erased flashlight lighting on ") + faceNames[face]);
        Check(fullLoss < .02, std::string("Bounced radiance darkened the direct solution on ") + faceNames[face]);
    }
    // Numeric tests above intentionally bypass AA. Retain a separate native
    // presentation capture with the interactive room's actual TAA path.
    GiRoomSetView(frame, Render::LumenLightingView::SurfaceCache);
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
    frame.effects.cameraCut = true; settle(32);
    GiRoomSave(lab, options, "blocker_combined_taa");

    // A camera-mounted beam exposes cache-grid scalloping on large sloped
    // receivers even when all the thin-blocker energy checks above pass.
    // Compare the native direct image to independent CPU geometry/lighting,
    // including the dark side of the cone. The old cache display is a negative
    // control, so a smoother-looking but equally inaccurate result cannot pass.
    frame.giRoomState = GiRoomSettings{};
    frame.giRoomState.pointLight = false;
    frame.giRoomState.flashlight = true;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    struct BeamStatistics {
        unsigned samples = 0, litSamples = 0;
        double energy = 0, cachedError = 0, nativeError = 0;
    };
    std::array<BeamStatistics, 3> beamStatistics{};
    constexpr std::array<const char*, 3> receiverNames{{"floor", "back wall", "prop tops"}};
    constexpr std::array<const char*, 2> beamLabels{{"beam_downward", "beam_oblique"}};
    const std::array<Render::PipelineCamera, 2> beamCameras{{
        GiRoomAim({0, 4.6f, 3.4f}, {0, .9f, -.8f}, frame.OutputAspect()),
        GiRoomAim({0, 3.6f, 3.25f}, {2.5f, 3.4f, -4}, frame.OutputAspect())}};
    for (unsigned view = 0; view < beamCameras.size(); ++view) {
        frame.camera = beamCameras[view];
        frame.effects.cameraCut = true;
        frame.effects.lumenGi.fullResolutionDirectLighting = false;
        GiRoomSetView(frame, Render::LumenLightingView::DirectOnly);
        settle(32);
        const auto cachedBeam = read();
        const std::string cachedLabel = std::string(beamLabels[view]) + "_cached_direct";
        GiRoomSave(lab, options, cachedLabel.c_str());
        frame.effects.lumenGi.fullResolutionDirectLighting = true;
        frame.effects.cameraCut = true; render();
        const auto nativeBeam = read();
        const std::string nativeLabel = std::string(beamLabels[view]) + "_native_direct";
        GiRoomSave(lab, options, nativeLabel.c_str());
        const auto inverseVP = glm::inverse(frame.camera->projection * frame.camera->view);
        const unsigned stride = std::max(1u, frame.width / 480);
        for (unsigned y = stride; y + stride < frame.height; y += stride)
            for (unsigned x = stride; x + stride < frame.width; x += stride) {
                const auto ndc = (glm::vec2(x, y) + .5f) / glm::vec2(frame.width, frame.height) * 2.f - 1.f;
                const auto farPoint = inverseVP * glm::vec4(ndc, 1, 1);
                const auto direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - frame.camera->position);
                float distance = frame.camera->farPlane; int receiver = -1;
                for (unsigned triangle = 0; triangle < closeGeometry.TriangleCount(); ++triangle) {
                    const float hit = intersectTriangle(frame.camera->position, direction, triangle, distance);
                    if (hit < distance) { distance = hit; receiver = int(triangle); }
                }
                if (receiver < 0) continue;
                const auto world = frame.camera->position + direction * distance;
                const auto offset = triangleOffset(unsigned(receiver));
                const auto a = glm::vec3(closeTriangles[offset]);
                const auto b = glm::vec3(closeTriangles[offset + 1]);
                const auto c = glm::vec3(closeTriangles[offset + 2]);
                const auto normal = glm::normalize(glm::cross(b - a, c - a));
                int kind = -1;
                if (std::abs(world.y) < .0001f) kind = 0;
                else if (std::abs(world.z + 4) < .0001f) kind = 1;
                else if (normal.y > .999f && world.y > .1f && world.y < 4.9f) kind = 2;
                if (kind < 0) continue;
                // Exclude mixed-face raster pixels; cache-cell/cone boundaries
                // inside the actual receiver remain part of the comparison.
                const float clearance = std::min({glm::length(glm::cross(b - a, world - a)) / glm::length(b - a),
                                                  glm::length(glm::cross(c - b, world - b)) / glm::length(c - b),
                                                  glm::length(glm::cross(a - c, world - c)) / glm::length(a - c)});
                if (clearance < .025f) continue;
                const double expected = luma(physicalDirect(world, normal, glm::vec3(closeTriangles[offset + 3]), false, true));
                const auto pixel = std::size_t(y) * frame.width + x;
                auto& result = beamStatistics[unsigned(kind)];
                ++result.samples; if (expected > .005) ++result.litSamples;
                result.energy += expected;
                result.cachedError += std::abs(luma(glm::vec3(cachedBeam[pixel])) - expected);
                result.nativeError += std::abs(luma(glm::vec3(nativeBeam[pixel])) - expected);
            }
        GiRoomSetView(frame, Render::LumenLightingView::SurfaceCache);
        frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
        frame.effects.cameraCut = true; settle(32);
        const std::string taaLabel = std::string(beamLabels[view]) + "_full_taa";
        GiRoomSave(lab, options, taaLabel.c_str());
        frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    }
    for (unsigned kind = 0; kind < beamStatistics.size(); ++kind) {
        const auto& result = beamStatistics[kind];
        const double cachedError = result.cachedError / std::max(result.energy, .001);
        const double nativeError = result.nativeError / std::max(result.energy, .001);
        std::cout << "GI room beam " << receiverNames[kind] << ": samples=" << result.samples
                  << ", lit samples=" << result.litSamples << ", cache physical error=" << cachedError * 100
                  << "%, native physical error=" << nativeError * 100 << "%\n";
        Check(result.litSamples >= 16, std::string("Beam aliasing test missed illuminated ") + receiverNames[kind]);
        Check(nativeError < .035, std::string("Native beam retains spatial cone/shadow error on ") + receiverNames[kind]);
        Check(cachedError > nativeError * 3 + .01,
              std::string("Beam aliasing negative control did not expose cache-grid error on ") + receiverNames[kind]);
    }

    // Independently integrate one physical diffuse bounce over triangle area.
    // A small torch footprint is more reliably covered by equal-area source
    // quadrature than by a handful of receiver hemisphere rays. This reference
    // uses neither the surface-cache lattice nor the GPU ray directions.
    frame.giRoomState = GiRoomSettings{};
    frame.giRoomState.pointLight = false; frame.giRoomState.flashlight = true;
    frame.camera = GiRoomAim({0, 3.6f, 3.25f}, {2.5f, 3.4f, -4}, frame.OutputAspect());
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    GiRoomSetView(frame, Render::LumenLightingView::IndirectOnly);
    struct BounceSource { glm::vec3 position, normal, radiance; float area; };
    const auto bounceSources = [&](unsigned subdivisions) {
        std::vector<BounceSource> result;
        for (unsigned triangle = 0; triangle < closeGeometry.TriangleCount(); ++triangle) {
            const auto offset = triangleOffset(triangle);
            const auto a = glm::vec3(closeTriangles[offset]);
            const auto ab = glm::vec3(closeTriangles[offset + 1]) - a;
            const auto ac = glm::vec3(closeTriangles[offset + 2]) - a;
            const auto normal = glm::normalize(glm::cross(ab, ac));
            const float area = glm::length(glm::cross(ab, ac)) * .5f / float(subdivisions * subdivisions);
            const auto addSource = [&](float u, float v) {
                const auto position = a + ab * (u / subdivisions) + ac * (v / subdivisions);
                const auto radiance = physicalDirect(position, normal, glm::vec3(closeTriangles[offset + 3]), false, true);
                if (glm::length(radiance) > 1e-8f) result.push_back({position, normal, radiance, area});
            };
            for (unsigned y = 0; y < subdivisions; ++y) for (unsigned x = 0; x + y < subdivisions; ++x) {
                addSource(float(x) + 1.f / 3, float(y) + 1.f / 3);
                if (x + y + 1 < subdivisions) addSource(float(x) + 2.f / 3, float(y) + 2.f / 3);
            }
        }
        return result;
    };
    const auto sources32 = bounceSources(32), sources64 = bounceSources(64);
    Check(!sources32.empty() && !sources64.empty(), "Indirect area reference missed the flashlight footprint");
    const auto areaBounce = [&](glm::vec3 point, glm::vec3 normal, glm::vec3 rho,
                                const std::vector<BounceSource>& sources) {
        glm::dvec3 incident(0);
        for (const auto& source : sources) {
            const auto delta = source.position - point;
            const float distanceSquared = glm::dot(delta, delta);
            if (distanceSquared < 1e-8f) continue;
            const auto direction = delta / std::sqrt(distanceSquared);
            const float form = std::max(0.f, glm::dot(normal, direction)) *
                               std::max(0.f, glm::dot(source.normal, -direction));
            if (form > 0 && visibleSource(point, normal, source.position, 0))
                incident += glm::dvec3(source.radiance) * double(form * source.area / distanceSquared);
        }
        return glm::vec3(incident * glm::dvec3(rho) * (.9 / 3.14159265358979323846));
    };
    constexpr unsigned bounceGrid = 8;
    struct BounceSample {
        bool valid = false;
        glm::vec3 world{0}, reference32{0}, reference64{0};
        std::array<glm::vec3, 2> measured{};
        glm::vec2 pixel{0};
    };
    std::array<std::array<BounceSample, bounceGrid * bounceGrid>, 2> bounceSamples{};
    const auto referenceVP = frame.camera->projection * frame.camera->view;
    for (unsigned patch = 0; patch < bounceSamples.size(); ++patch)
        for (unsigned y = 0; y < bounceGrid; ++y) for (unsigned x = 0; x < bounceGrid; ++x) {
            const float u = (float(x) + .5f) / bounceGrid, v = (float(y) + .5f) / bounceGrid;
            auto& sample = bounceSamples[patch][y * bounceGrid + x];
            sample.world = patch == 0 ? glm::vec3(2.7f + u, 2.35f + 2 * v, -4) :
                                        glm::vec3(3.1f + .65f * u, 0, -3.2f + 1.9f * v);
            const glm::vec3 normal = patch == 0 ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            const auto projected = referenceVP * glm::vec4(sample.world, 1);
            if (projected.w <= 0) continue;
            const auto ndc = glm::vec3(projected) / projected.w;
            if (std::abs(ndc.x) >= .98f || std::abs(ndc.y) >= .98f || std::abs(ndc.z) >= 1) continue;
            const auto displacement = sample.world - frame.camera->position;
            const auto direction = glm::normalize(displacement);
            const float expectedDistance = glm::length(displacement);
            float distance = expectedDistance + .001f; int receiver = -1;
            for (unsigned triangle = 0; triangle < closeGeometry.TriangleCount(); ++triangle) {
                const float hit = intersectTriangle(frame.camera->position, direction, triangle, distance);
                if (hit < distance) { distance = hit; receiver = int(triangle); }
            }
            if (receiver < 0 || std::abs(distance - expectedDistance) > .001f) continue;
            const auto rho = glm::min(glm::vec3(closeTriangles[triangleOffset(unsigned(receiver)) + 3]), glm::vec3(.95f));
            sample.reference32 = areaBounce(sample.world, normal, rho, sources32);
            sample.reference64 = areaBounce(sample.world, normal, rho, sources64);
            sample.pixel = (glm::vec2(ndc) * .5f + .5f) * glm::vec2(frame.width, frame.height) - .5f;
            sample.valid = true;
        }
    constexpr std::array<const char*, 2> bounceModes{{"cache_direct_16", "traced_direct_32"}};
    for (unsigned mode = 0; mode < bounceModes.size(); ++mode) {
        frame.effects.lumenGi.traceDirectLighting = mode != 0;
        frame.effects.lumenGi.surfaceRays = mode == 0 ? 16 : 32;
        frame.effects.lumenGi.reset = true; frame.effects.cameraCut = true; render();
        frame.effects.lumenGi.reset = false;
        const auto oneBounce = read();
        const std::string label = std::string("indirect_one_bounce_") + bounceModes[mode];
        GiRoomSave(lab, options, label.c_str());
        for (auto& patch : bounceSamples) for (auto& sample : patch) if (sample.valid) {
            const auto low = glm::ivec2(glm::floor(sample.pixel));
            const auto fraction = glm::fract(sample.pixel);
            glm::vec3 value(0);
            for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
                const auto tap = glm::clamp(low + glm::ivec2(x, y), glm::ivec2(0), glm::ivec2(frame.width - 1, frame.height - 1));
                const float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
                value += glm::vec3(oneBounce[std::size_t(tap.y) * frame.width + tap.x]) * weight;
            }
            sample.measured[mode] = value;
        }
    }
    constexpr std::array<const char*, 2> bouncePatchNames{{"white back wall", "floor beside green wall"}};
    for (unsigned patch = 0; patch < bounceSamples.size(); ++patch) {
        unsigned samples = 0; double referenceEnergy = 0, convergence = 0;
        glm::dvec3 referenceMean(0);
        for (const auto& sample : bounceSamples[patch]) if (sample.valid) {
            ++samples; referenceMean += glm::dvec3(sample.reference64);
            referenceEnergy += luma(sample.reference64);
            convergence += std::abs(luma(sample.reference32) - luma(sample.reference64));
        }
        Check(samples >= 32 && referenceEnergy > 1e-6, std::string("Indirect area reference missed ") + bouncePatchNames[patch]);
        Check(convergence / referenceEnergy < .02,
              std::string("Indirect area quadrature has not converged on ") + bouncePatchNames[patch]);
        referenceMean /= samples;
        std::cout << "GI room indirect reference " << bouncePatchNames[patch] << ": samples=" << samples
                  << ", source patches 32/64=" << sources32.size() << '/' << sources64.size()
                  << ", RGB=" << referenceMean.r << '/' << referenceMean.g << '/' << referenceMean.b
                  << ", quadrature convergence=" << convergence / referenceEnergy * 100 << "%\n";
        const double meanReference = referenceEnergy / samples;
        std::array<double, 2> bounceL1{};
        for (unsigned mode = 0; mode < bounceModes.size(); ++mode) {
            glm::dvec3 measuredMean(0); double absoluteError = 0, squaredError = 0, rgbError = 0;
            for (const auto& sample : bounceSamples[patch]) if (sample.valid) {
                measuredMean += glm::dvec3(sample.measured[mode]);
                const double error = luma(sample.measured[mode]) - luma(sample.reference64);
                absoluteError += std::abs(error); squaredError += error * error;
                const auto rgb = glm::abs(sample.measured[mode] - sample.reference64);
                rgbError += rgb.r + rgb.g + rgb.b;
            }
            measuredMean /= samples;
            const double meanMeasured = luma(glm::vec3(measuredMean));
            double covariance = 0, referenceVariance = 0, residualCurvature = 0; unsigned curvatureSamples = 0;
            for (const auto& sample : bounceSamples[patch]) if (sample.valid) {
                const double reference = luma(sample.reference64) - meanReference;
                covariance += reference * (luma(sample.measured[mode]) - meanMeasured);
                referenceVariance += reference * reference;
            }
            for (unsigned y = 1; y + 1 < bounceGrid; ++y) for (unsigned x = 1; x + 1 < bounceGrid; ++x) {
                const unsigned at = y * bounceGrid + x;
                const auto& center = bounceSamples[patch][at];
                const std::array<unsigned, 4> neighbours{{at - 1, at + 1, at - bounceGrid, at + bounceGrid}};
                if (!center.valid || std::any_of(neighbours.begin(), neighbours.end(), [&](unsigned n) { return !bounceSamples[patch][n].valid; })) continue;
                double curvature = luma(center.measured[mode]) - luma(center.reference64);
                for (const auto n : neighbours) curvature -= .25 *
                    (luma(bounceSamples[patch][n].measured[mode]) - luma(bounceSamples[patch][n].reference64));
                residualCurvature += curvature * curvature; ++curvatureSamples;
            }
            const double rgbEnergy = samples * (referenceMean.r + referenceMean.g + referenceMean.b);
            bounceL1[mode] = absoluteError / referenceEnergy;
            const double normalizedRgbError = rgbError / std::max(rgbEnergy, 1e-8);
            const double spatialRmse = std::sqrt(squaredError / samples) / meanReference;
            const double contrastGain = covariance / std::max(referenceVariance, 1e-12);
            const double referenceContrast = std::sqrt(referenceVariance / samples) / meanReference;
            const double curvatureError = std::sqrt(residualCurvature / std::max(curvatureSamples, 1u)) / meanReference;
            std::cout << "GI room indirect " << bounceModes[mode] << ' ' << bouncePatchNames[patch]
                      << ": RGB=" << measuredMean.r << '/' << measuredMean.g << '/' << measuredMean.b
                      << ", luma L1=" << bounceL1[mode] * 100
                      << "%, RGB L1=" << normalizedRgbError * 100
                      << "%, spatial RMSE=" << spatialRmse * 100
                      << "%, contrast gain=" << contrastGain
                      << ", reference contrast=" << referenceContrast * 100
                      << "%, residual curvature=" << curvatureError * 100
                      << "% (" << curvatureSamples << " stencils)\n";
            Check(curvatureSamples >= 12 && referenceContrast > .04,
                  std::string("Indirect spatial reference lacks sufficient shape samples on ") + bouncePatchNames[patch]);
            if (mode == 1) {
                Check(bounceL1[mode] < .06 && normalizedRgbError < .06 && spatialRmse < .06,
                      std::string("Traced diffuse bounce has excessive physical radiance error on ") + bouncePatchNames[patch]);
                Check(contrastGain > .65 && contrastGain < 1.35,
                      std::string("Diffuse denoising lost the physical brightness gradient on ") + bouncePatchNames[patch]);
                Check(curvatureError < .02,
                      std::string("Traced diffuse bounce retained spatial blotches on ") + bouncePatchNames[patch]);
            }
        }
        if (patch == 1) Check(bounceL1[1] < bounceL1[0] * .5,
                             "Traced diffuse transport did not halve the floor reference error");
    }
    // Retain the actual accumulated presentation, including both the smooth
    // indirect-only field and its combination with the native direct beam.
    frame.effects.lumenGi.traceDirectLighting = true; frame.effects.lumenGi.surfaceRays = 32;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
    GiRoomSetView(frame, Render::LumenLightingView::SurfaceCache);
    frame.effects.cameraCut = true; settle(128);
    GiRoomSave(lab, options, "indirect_green_wall_full_taa");
    GiRoomSetView(frame, Render::LumenLightingView::IndirectOnly);
    frame.effects.cameraCut = true; settle(32);
    GiRoomSave(lab, options, "indirect_green_wall_bounce_taa");
    std::cout << "GI room isolation, linear energy, unlit enclosure, moving blocker and flashlight passed.\n";
}
void GiRoomPerformance(MaterialLabModule& lab, const Options& options) {
    auto frame = MakeGiRoomFrame(options);
    frame.giRoomHud = false; frame.present = false;
    SetShowcaseResolution(frame, options.studySizeSet ? options.benchmarkWidth : 2560,
                          options.studySizeSet ? options.benchmarkHeight : 1440, options.renderScale);
    frame.camera = GiRoomCameraPreset(0, frame.OutputAspect());
    const auto render = [&] { Check(lab.Render(frame), lab.LastError()); frame.effects.cameraCut = false; };
    const auto warm = [&](unsigned count) {
        for (unsigned index = 0; index < count; ++index) {
            render();
            if (!options.vulkan && index % 6 == 5 && !frame.present) Check(!lab.Readback().rgba.empty(), lab.LastError());
        }
    };
    warm(128);
    frame.present = options.benchmarkPresent;
    if (frame.present) warm(32);
    std::cout << "GI room resolution: render " << frame.width << 'x' << frame.height << ", output "
              << frame.OutputExtent().x << 'x' << frame.OutputExtent().y
              << ", MSAA=" << frame.effects.temporal.msaaSamples << '\n';
    const auto measure = [&](const char* label) {
        const auto timing = lab.MeasureFrames(frame, 120, 0);
        Check(timing.gpuMilliseconds > 0 && timing.elapsedMilliseconds > 0, lab.LastError());
        const auto stats = lab.PipelineStatistics();
        std::cout << "GI room benchmark " << label << (frame.present ? " with presentation" : " offscreen")
                  << ": GPU=" << timing.gpuMilliseconds << " ms, wall=" << timing.elapsedMilliseconds
                  << " ms, FPS=" << 1000 / timing.elapsedMilliseconds << ", surfels=" << stats.lumenSurfaceTexels
                  << ", updates=" << stats.lumenUpdatedSurfels << '\n';
        if (!options.screenshot.empty()) {
            const bool shown = frame.present;
            frame.present = false; render(); GiRoomSave(lab, options, label); frame.present = shown;
        }
    };
    measure("point_full");
    GiRoomSetView(frame, Render::LumenLightingView::IndirectOnly); render(); measure("point_bounce");
    GiRoomSetView(frame, Render::LumenLightingView::DirectOnly); render(); measure("point_direct");
    frame.giRoomState.pointLight = false; frame.giRoomState.flashlight = true;
    GiRoomSetView(frame, Render::LumenLightingView::SurfaceCache);
    frame.camera = GiRoomAim({0, 3.6f, 3.25f}, {2.5f, 3.4f, -4}, frame.OutputAspect());
    frame.effects.cameraCut = true; warm(32); measure("flashlight_full");
    frame.giRoomState.pointLight = true; warm(32); measure("combined_full");
    std::cout << "GI room 120-frame isolated lighting performance batches completed.\n";
}
void GiRoomInteractive(MaterialLabModule& lab, ApplicationWindow::Window& window, const Options& options) {
    auto frame = MakeGiRoomFrame(options);
    auto* native = window.GetNativeWindow();
    Input input;
    glfwSetWindowUserPointer(native, &input);
    glfwSetScrollCallback(native, [](GLFWwindow* handle, double, double delta) {
        static_cast<Input*>(glfwGetWindowUserPointer(handle))->scroll += float(delta);
    });
    glm::vec3 position;
    float yaw = 0, pitch = 0, speed = 2.5f, renderScale = options.renderScale;
    const auto pose = [&](const Render::PipelineCamera& camera) {
        position = camera.position;
        const auto forward = -glm::vec3(glm::inverse(camera.view)[2]);
        yaw = std::atan2(forward.x, -forward.z); pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));
    };
    pose(GiRoomCameraPreset(0, 1));
    std::array<bool, GLFW_KEY_LAST + 1> previous{};
    bool looking = false, clicked = false, saved = false;
    double lastX = 0, lastY = 0, lastTime = glfwGetTime(), smoothFrameMs = 1000. / 90;
    double lastFpsSample=lastTime;
    FramePacer pacer;
    std::cout << "Sealed GI room: one point lamp; F camera flashlight, F6 point lamp, G full/bounce/direct.\n"
                 "RMB look; WASD move; Q/E down/up; Shift fast; wheel speed. 1/2/3 camera views.\n"
                 "Click/Tab select; arrows/PgUp/PgDn move; Z/X rotate; R reset prop; Backspace reset room.\n"
                 "P cycles 100/75/62.5/50 percent render scale; F1 help. No sky, ambient fill or bloom.\n";
    while (!window.ShouldClose()) {
        const auto start = std::chrono::steady_clock::now(); window.PollEvents();
        const double now = glfwGetTime(), elapsed = now - lastTime; lastTime = now;
        const float dt = float(std::clamp(elapsed, .0001, .1));
        frame.effects.deltaSeconds = float(std::clamp(elapsed, .0001, 1.));
        frame.effects.cameraCut = elapsed > .25;
        smoothFrameMs = .9 * smoothFrameMs + .1 * std::clamp(elapsed, .0001, 1.) * 1000;
        if(now-lastFpsSample>=1) {frame.displayFps=float(1000/smoothFrameMs);lastFpsSample=now;}
        const auto down = [&](int key) { return glfwGetKey(native, key) == GLFW_PRESS; };
        const auto pressed = [&](int key) { const bool value = down(key), edge = value && !previous[key]; previous[key] = value; return edge; };
        if (pressed(GLFW_KEY_ESCAPE)) break;
        int width = 0, height = 0; glfwGetFramebufferSize(native, &width, &height);
        if (width <= 0 || height <= 0) { glfwWaitEventsTimeout(.05); continue; }
        if (pressed(GLFW_KEY_P)) {
            constexpr std::array<float, 4> scales{{1, .75f, .625f, .5f}};
            const auto found = std::find_if(scales.begin(), scales.end(), [&](float scale) { return std::abs(scale - renderScale) < .0001f; });
            renderScale = found == scales.end() ? 1 : scales[(unsigned(found - scales.begin()) + 1) % scales.size()];
            frame.effects.cameraCut = true;
        }
        SetShowcaseResolution(frame, options.renderSizeSet ? options.benchmarkWidth : unsigned(width),
                              options.renderSizeSet ? options.benchmarkHeight : unsigned(height), renderScale);
        const float aspect = frame.OutputAspect();
        for (unsigned preset = 0; preset < 3; ++preset) if (pressed(GLFW_KEY_1 + int(preset))) {
            pose(GiRoomCameraPreset(preset, aspect)); frame.effects.cameraCut = true;
        }
        auto& state = frame.giRoomState;
        if (pressed(GLFW_KEY_TAB)) state.selected = (state.selected + (down(GLFW_KEY_LEFT_SHIFT) ? int(GiRoomMovableCount) - 1 : 1)) % int(GiRoomMovableCount);
        if (pressed(GLFW_KEY_F1)) state.help = !state.help;
        if (pressed(GLFW_KEY_F)) state.flashlight = !state.flashlight;
        if (pressed(GLFW_KEY_F6)) state.pointLight = !state.pointLight;
        if (pressed(GLFW_KEY_G)) {
            const auto current = frame.effects.lumenGi.lightingView;
            GiRoomSetView(frame, current == Render::LumenLightingView::SurfaceCache ? Render::LumenLightingView::IndirectOnly :
                current == Render::LumenLightingView::IndirectOnly ? Render::LumenLightingView::DirectOnly : Render::LumenLightingView::SurfaceCache);
            frame.effects.cameraCut = true;
        }
        if (pressed(GLFW_KEY_R)) { state.offsets[state.selected] = {}; state.rotations[state.selected] = 0; }
        if (pressed(GLFW_KEY_BACKSPACE)) {
            state = GiRoomSettings{}; GiRoomSetView(frame, Render::LumenLightingView::SurfaceCache);
            pose(GiRoomCameraPreset(0, aspect)); frame.effects.cameraCut = true;
        }
        double x = 0, y = 0; glfwGetCursorPos(native, &x, &y);
        const bool focused = glfwGetWindowAttrib(native, GLFW_FOCUSED);
        const bool rmb = focused && glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rmb != looking) {
            glfwSetInputMode(native, GLFW_CURSOR, rmb ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            if (glfwRawMouseMotionSupported()) glfwSetInputMode(native, GLFW_RAW_MOUSE_MOTION, rmb ? GLFW_TRUE : GLFW_FALSE);
            glfwGetCursorPos(native, &x, &y);
        }
        if (rmb && looking) { yaw += float(x - lastX) * .0025f; pitch = std::clamp(pitch - float(y - lastY) * .0025f, -1.5f, 1.5f); }
        lastX = x; lastY = y; looking = rmb;
        speed = std::clamp(speed * std::pow(1.15f, input.scroll), .3f, 8.f); input.scroll = 0;
        if (focused) {
            const auto forward = ShowcaseForward(yaw, 0), right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            const auto movement = forward * (float(down(GLFW_KEY_W)) - float(down(GLFW_KEY_S))) +
                right * (float(down(GLFW_KEY_D)) - float(down(GLFW_KEY_A))) + glm::vec3(0, float(down(GLFW_KEY_E)) - float(down(GLFW_KEY_Q)), 0);
            if (glm::dot(movement, movement) > 0) position += glm::normalize(movement) * speed * dt * (down(GLFW_KEY_LEFT_SHIFT) ? 3.f : 1.f);
            position = ClampGiRoomCamera(position);
            const auto delta = glm::vec3(float(down(GLFW_KEY_RIGHT)) - float(down(GLFW_KEY_LEFT)),
                float(down(GLFW_KEY_PAGE_UP)) - float(down(GLFW_KEY_PAGE_DOWN)), float(down(GLFW_KEY_DOWN)) - float(down(GLFW_KEY_UP))) * dt * 1.5f;
            Check(MoveGiRoomObject(state, state.selected, delta, (float(down(GLFW_KEY_X)) - float(down(GLFW_KEY_Z))) * dt), "Invalid room prop edit");
        }
        auto camera = GiRoomCameraPreset(0, aspect);
        camera.position = position;
        camera.view = glm::lookAt(position, position + ShowcaseForward(yaw, pitch), glm::vec3(0, 1, 0));
        frame.camera = camera;
        const bool lmb = focused && glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (lmb && !clicked && !rmb) {
            int logicalWidth = 1, logicalHeight = 1; glfwGetWindowSize(native, &logicalWidth, &logicalHeight);
            const glm::vec2 ndc{float(x / logicalWidth) * 2 - 1, 1 - float(y / logicalHeight) * 2};
            const auto farPoint = glm::inverse(camera.projection * camera.view) * glm::vec4(ndc, 1, 1);
            const auto selected = PickGiRoomObject(position, glm::normalize(glm::vec3(farPoint) / farPoint.w - position), state);
            if (selected >= 0) state.selected = selected;
        }
        clicked = lmb;
        if (!saved && !options.screenshot.empty()) {
            frame.present = false;
            for (unsigned warm = 0; warm < 128; ++warm) Check(lab.Render(frame), lab.LastError());
            Save(Capture(lab, frame), options.screenshot); saved = true;
        }
        frame.present = true; Check(lab.Render(frame), lab.LastError());
        std::ostringstream title;
        title << "Sealed GI Room | " << GiRoomViewName(frame.effects.lumenGi.lightingView)
              << " | point " << state.pointLight << " flashlight " << state.flashlight
              << " | " << GiRoomMovable(state.selected).name << " | " << std::fixed << std::setprecision(1)
              << 1000 / smoothFrameMs << " FPS | " << frame.width << 'x' << frame.height << " -> "
              << frame.OutputExtent().x << 'x' << frame.OutputExtent().y << " | F flashlight G view P scale F1 help";
        glfwSetWindowTitle(native, title.str().c_str());
        if (options.fpsLimit > 0) pacer.WaitUntil(start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1 / options.fpsLimit)));
    }
    glfwSetInputMode(native, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(native, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    glfwSetScrollCallback(native, nullptr); glfwSetWindowUserPointer(native, nullptr);
}
