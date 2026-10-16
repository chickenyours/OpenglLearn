// Included after the placement reference helpers. This checks visible geometry
// and captured radiance independently, rather than duplicating HZB traversal.
struct ScreenTraceQualityMetrics {
    std::array<unsigned, 9> statuses{};
    unsigned active = 0, positiveHits = 0, blackHits = 0, coarseMipRays = 0;
    unsigned visiblePhysicalHits = 0, matchedPhysicalHits = 0;
    double depthFetches = 0, iterations = 0;
};
void ScreenProbeTraceQuality(MaterialLabModule& lab, const Options& options) {
    auto frame = MakeScreenProbeTraceFrame(options);
    frame.present = false; frame.screenProbeStudyHud = false;
    frame.width = options.studySizeSet ? options.benchmarkWidth : 640;
    frame.height = options.studySizeSet ? options.benchmarkHeight : 400;
    frame.effects.outputSize = {0, 0};
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    frame.effects.temporal.msaaSamples = 1;
    frame.camera = ProbePlacementCameraPreset(0, frame.OutputAspect());
    auto& placement = frame.effects.screenProbePlacement;
    placement.enabled = true; placement.adaptive = true; placement.freeze = false;
    placement.frameIndex = 0; placement.baseSpacing = 32; placement.candidateCount = 8;
    auto& tracing = placement.trace;
    tracing.enabled = true; tracing.rayResolution = 8; tracing.maxSteps = 512; tracing.maxMip = 10;
    tracing.showPanel = true; tracing.displayMode = 0;
    const auto render = [&] { Check(lab.Render(frame), lab.LastError()); frame.effects.cameraCut = false; placement.reset = false; };
    const auto save = [&](const char* label) {
        if (options.screenshot.empty()) return;
        auto path = std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string() + "_" + label + path.extension().string());
        Save(lab.Readback(), path.string());
    };
    const auto inverseTransforms = [&] {
        std::vector<glm::mat4> result;
        for (const auto& object : ProbePlacementObjects())
            result.push_back(glm::inverse(ProbePlacementTransform(object, frame.screenProbeStudyState)));
        return result;
    };
    const auto checkHzb = [&](const auto& data, const char* label) {
        const std::size_t pixels = std::size_t(frame.width) * frame.height;
        Check(data.sourceSize == glm::uvec2(frame.width, frame.height) && data.sourceHdr.size() == pixels &&
              data.sourceViewPositions.size() == pixels && data.sourceWorldNormals.size() == pixels,
              std::string(label) + ": trace source dimensions/data are incomplete");
        Check(!data.hzb.empty() && data.hzb.size() <= Render::ScreenProbeTraceMaxHzbLevels &&
              data.hzb.size() == data.hzbSizes.size() && data.hzbSizes[0] == data.sourceSize,
              std::string(label) + ": invalid HZB level metadata");
        const auto depthClose = [](float a, float b) { return std::abs(a - b) <= std::max(1e-5f, std::abs(b) * 2e-6f); };
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            const auto p = data.sourceViewPositions[pixel];
            const bool valid = p.w > .5f && p.z < 0 && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            const float expected = valid ? -p.z : 1e20f;
            Check(data.hzb[0].size() == pixels && depthClose(data.hzb[0][pixel].x, expected),
                  std::string(label) + ": HZB mip zero does not preserve the captured positive linear depth");
        }
        for (unsigned level = 1; level < data.hzb.size(); ++level) {
            const auto previous = data.hzbSizes[level - 1], size = data.hzbSizes[level];
            Check(size == glm::max(glm::uvec2(1), (previous + glm::uvec2(1)) / 2u) &&
                  data.hzb[level].size() == std::size_t(size.x) * size.y,
                  std::string(label) + ": HZB reduction lost an odd edge row/column");
            for (unsigned y = 0; y < size.y; ++y) for (unsigned x = 0; x < size.x; ++x) {
                float expected = 1e20f;
                for (unsigned dy = 0; dy < 2; ++dy) for (unsigned dx = 0; dx < 2; ++dx) {
                    const auto at = glm::min(glm::uvec2(x * 2 + dx, y * 2 + dy), previous - glm::uvec2(1));
                    expected = std::min(expected, data.hzb[level - 1][std::size_t(at.y) * previous.x + at.x].x);
                }
                Check(depthClose(data.hzb[level][std::size_t(y) * size.x + x].x, expected),
                      std::string(label) + ": HZB reduction does not contain the closest valid source depth");
            }
        }
        // Verify selected captured pixels against independently intersected
        // exact visible meshes, so a coherent but wrong depth pyramid fails.
        const auto triangles = ProbePlacementTriangles(frame.screenProbeStudyState);
        const auto inverses = inverseTransforms();
        const auto inverseVP = glm::inverse(frame.camera->projection * frame.camera->view);
        unsigned verified = 0;
        const unsigned stride = std::max(8u, frame.width / 32);
        for (unsigned y = stride / 2; y < frame.height; y += stride)
            for (unsigned x = stride / 2; x < frame.width; x += stride) {
                const auto p = data.sourceViewPositions[std::size_t(y) * frame.width + x];
                if (p.w <= .5f) continue;
                const auto ndc = (glm::vec2(x, y) + .5f) / glm::vec2(frame.width, frame.height) * 2.f - 1.f;
                const auto farPoint = inverseVP * glm::vec4(ndc, 1, 1);
                const auto direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - frame.camera->position);
                const auto hit = PlacementTraceReference(frame.camera->position, direction, frame.camera->farPlane, triangles, inverses);
                const auto world = glm::vec3(glm::inverse(frame.camera->view) * glm::vec4(glm::vec3(p), 1));
                Check(hit.object >= 0 && glm::length(hit.position - world) < .006f,
                      std::string(label) + ": captured HZB source is not the nearest CPU-visible surface");
                ++verified;
            }
        Check(verified >= 32, std::string(label) + ": HZB source reference missed visible geometry");
        std::cout << "Screen trace HZB " << label << ": levels=" << data.hzb.size()
                  << ", CPU visible-depth samples=" << verified << '\n';
    };
    const auto inspectRays = [&](const auto& data, const auto& probes, const char* label, bool physicalReference = false) {
        const std::size_t probeSlots = probes.uniform.size() + probes.spawned.size();
        const std::size_t raysPerProbe = std::size_t(tracing.rayResolution) * tracing.rayResolution;
        const std::size_t rays = probeSlots * raysPerProbe;
        Check(data.probeSlots == probeSlots && data.rayResolution == tracing.rayResolution &&
              data.candidateCount == placement.candidateCount && data.uniformGridSize == probes.uniformGridSize &&
              data.radianceStatus.size() == rays && data.hitWorldDistance.size() == rays &&
              data.hitPixelStepsMip.size() == rays && data.rayDirectionDepthFetches.size() == rays,
              std::string(label) + ": ray atlas does not correspond to the actual placement slots");
        const auto triangles = ProbePlacementTriangles(frame.screenProbeStudyState);
        const auto inverses = inverseTransforms(); const auto inverseView = glm::inverse(frame.camera->view);
        const auto vp = frame.camera->projection * frame.camera->view, inverseVP = glm::inverse(vp);
        const auto halfPixelWorld = [&](glm::vec3 world) {
            const auto clip = vp * glm::vec4(world, 1); const auto ndc = glm::vec3(clip) / clip.w;
            const auto corner = inverseVP * glm::vec4(ndc + glm::vec3(1.f / frame.width, 1.f / frame.height, 0), 1);
            return glm::length(glm::vec3(corner) / corner.w - world);
        };
        ScreenTraceQualityMetrics result;
        for (std::size_t index = 0; index < rays; ++index) {
            const unsigned probe = unsigned(index / raysPerProbe);
            const auto position = probe < probes.uniform.size() ? probes.uniform[probe] : probes.spawned[probe - probes.uniform.size()];
            const auto normalDepth = probe < probes.uniform.size() ? probes.uniformNormals[probe] : probes.spawnedNormals[probe - probes.uniform.size()];
            const auto color = data.radianceStatus[index], hit = data.hitWorldDistance[index];
            const auto metadata = data.hitPixelStepsMip[index], ray = data.rayDirectionDepthFetches[index];
            for (unsigned channel = 0; channel < 4; ++channel)
                Check(std::isfinite(color[channel]) && std::isfinite(hit[channel]) &&
                      std::isfinite(metadata[channel]) && std::isfinite(ray[channel]),
                      std::string(label) + ": ray diagnostics contain nonfinite values");
            const unsigned status = unsigned(std::round(color.w));
            Check(status < result.statuses.size() && std::abs(color.w - status) < .001f,
                  std::string(label) + ": unclassified trace result");
            ++result.statuses[status];
            if (position.w <= .5f) {
                Check(status == unsigned(Render::ScreenProbeTraceStatus::Inactive),
                      std::string(label) + ": an unplaced probe traced a ray");
                continue;
            }
            Check(status != unsigned(Render::ScreenProbeTraceStatus::Inactive),
                  std::string(label) + ": an active probe silently lost its trace status");
            ++result.active; result.depthFetches += ray.w; result.iterations += metadata.z;
            if (metadata.w > .5f) ++result.coarseMipRays;
            const auto direction = glm::vec3(ray);
            Check(std::abs(glm::length(direction) - 1) < .001f && ray.w >= 0 &&
                  metadata.z >= 0 && metadata.z <= tracing.maxSteps && metadata.w >= 0 && metadata.w <= tracing.maxMip,
                  std::string(label) + ": invalid world ray or HZB traversal accounting");
            if (glm::dot(direction, glm::vec3(normalDepth)) < -.05f)
                Check(status == unsigned(Render::ScreenProbeTraceStatus::Backface),
                      std::string(label) + ": a ray emitted into its receiver traced unrelated color");
            const auto normal = glm::normalize(glm::vec3(normalDepth));
            const auto probeWorld = glm::vec3(position);
            const auto probeClip = vp * glm::vec4(probeWorld, 1);
            const auto corner = inverseVP * glm::vec4(glm::vec3(probeClip) / probeClip.w +
                                                     glm::vec3(1.f / frame.width, 1.f / frame.height, 0), 1);
            const float bias = std::max(tracing.normalBias,
                2 * std::abs(glm::dot(glm::vec3(corner) / corner.w - probeWorld, normal)));
            const auto origin = probeWorld + normal * bias;
            if (physicalReference && index % 7 == 0 && glm::dot(direction, normal) > .1f &&
                status != unsigned(Render::ScreenProbeTraceStatus::StepBudget) &&
                status != unsigned(Render::ScreenProbeTraceStatus::OriginUnavailable)) {
                // Only a first physical hit that is itself visible to the
                // capture camera can be expected from a screen-space tracer.
                const auto physical = PlacementTraceReference(origin, direction, tracing.maxDistance, triangles, inverses);
                if (physical.object >= 0 && physical.distance > bias * 4 && glm::dot(physical.normal, -direction) > .2f) {
                    const auto clip = vp * glm::vec4(physical.position, 1);
                    if (clip.w > 0) {
                        const auto projected = (glm::vec2(clip) / clip.w * .5f + .5f) * glm::vec2(frame.width, frame.height);
                        const auto pixel = glm::ivec2(glm::floor(projected));
                        if (pixel.x > 2 && pixel.y > 2 && pixel.x + 3 < int(frame.width) && pixel.y + 3 < int(frame.height)) {
                            const auto cameraHit = PlacementTraceReference(frame.camera->position,
                                glm::normalize(physical.position - frame.camera->position), frame.camera->farPlane, triangles, inverses);
                            const auto captured = data.sourceViewPositions[std::size_t(pixel.y) * frame.width + pixel.x];
                            const auto capturedWorld = glm::vec3(inverseView * glm::vec4(glm::vec3(captured), 1));
                            const float footprint = halfPixelWorld(physical.position);
                            if (captured.w > .5f && glm::length(cameraHit.position - physical.position) < .006f &&
                                glm::length(capturedWorld - physical.position) < std::max(.01f, footprint * 3)) {
                                ++result.visiblePhysicalHits;
                                const float tolerance = footprint * 4 + tracing.thickness +
                                                        tracing.relativeThickness * std::max(-captured.z, 0.f);
                                if (status == unsigned(Render::ScreenProbeTraceStatus::Hit) &&
                                    glm::length(glm::vec3(hit) - physical.position) < tolerance) ++result.matchedPhysicalHits;
                            }
                        }
                    }
                }
            }
            if (status != unsigned(Render::ScreenProbeTraceStatus::Hit)) continue;
            const auto pixel = glm::ivec2(glm::round(glm::vec2(metadata)));
            Check(pixel.x >= 0 && pixel.y >= 0 && pixel.x < int(frame.width) && pixel.y < int(frame.height) &&
                  glm::length(glm::vec2(metadata) - glm::vec2(pixel)) < .001f,
                  std::string(label) + ": a hit sampled color outside the captured input");
            const auto at = std::size_t(pixel.y) * frame.width + pixel.x;
            const auto viewPosition = data.sourceViewPositions[at];
            const auto world = glm::vec3(inverseView * glm::vec4(glm::vec3(viewPosition), 1));
            Check(viewPosition.w > .5f && glm::length(glm::vec3(hit) - world) < .001f,
                  std::string(label) + ": hit position differs from the actual sampled GBuffer surface");
            const auto displacement = world - frame.camera->position;
            const auto visible = PlacementTraceReference(frame.camera->position, glm::normalize(displacement),
                                                         frame.camera->farPlane, triangles, inverses);
            Check(visible.object >= 0 && glm::length(visible.position - world) < .006f,
                  std::string(label) + ": a trace reported a surface hidden from the source camera");
            Check(hit.w > tracing.normalBias && hit.w <= tracing.maxDistance + .001f &&
                  glm::length(world - glm::vec3(position)) > tracing.normalBias,
                  std::string(label) + ": a hit is a self intersection or exceeds its ray limit");
            const auto delta = world - origin;
            const float alongRay = glm::dot(delta, direction);
            const float lineError = glm::length(delta - direction * alongRay);
            const float lineTolerance = halfPixelWorld(world) * 3 +
                1.5f * (tracing.thickness + tracing.relativeThickness * std::max(-viewPosition.z, 0.f));
            Check(alongRay > 0 && std::abs(hit.w - alongRay) < .002f && lineError < lineTolerance,
                  std::string(label) + ": sampled surface does not correspond to the probe's outgoing world ray");
            const auto hitNormal = glm::vec3(data.sourceWorldNormals[at]);
            Check(glm::dot(hitNormal, -direction) > .01f,
                  std::string(label) + ": trace sampled the backside of its visible receiver");
            const auto expected = glm::vec3(data.sourceHdr[at]);
            Check(glm::length(glm::vec3(color) - expected) < .0002f &&
                  color.x >= -.0001f && color.y >= -.0001f && color.z >= -.0001f,
                  std::string(label) + ": ray radiance is not the untouched pre-marker HDR source color");
            if (glm::length(expected) > .001f) ++result.positiveHits;
            if (glm::length(expected) < 1e-6f) ++result.blackHits;
        }
        Check(result.active > 0 && result.statuses[unsigned(Render::ScreenProbeTraceStatus::Hit)] > 0 &&
              result.statuses[unsigned(Render::ScreenProbeTraceStatus::Backface)] > 0,
              std::string(label) + ": trace fixture lacks active rays, visible hits or rejected hemisphere rays");
        Check(result.statuses[unsigned(Render::ScreenProbeTraceStatus::OffScreen)] +
              result.statuses[unsigned(Render::ScreenProbeTraceStatus::NoHit)] > 0,
              std::string(label) + ": trace fixture does not exercise a classified screen-space miss");
        if (physicalReference) {
            std::cout << "Screen trace visible physical first hits " << label << ": matched=" << result.matchedPhysicalHits
                      << '/' << result.visiblePhysicalHits << '\n';
            Check(result.visiblePhysicalHits >= 16 && result.matchedPhysicalHits >= result.visiblePhysicalHits * .60,
                  std::string(label) + ": HZB tracing skipped too many independently visible physical ray intersections");
        }
        std::cout << "Screen trace " << label << ": active=" << result.active << ", hit=" << result.statuses[1]
                  << ", offscreen=" << result.statuses[2] << ", nohit=" << result.statuses[3]
                  << ", uncertain=" << result.statuses[4] << ", backface=" << result.statuses[5]
                  << ", unavailable=" << result.statuses[6] << ", invalidHDR=" << result.statuses[7]
                  << ", budget=" << result.statuses[8] << ", depthfetch=" << result.depthFetches
                  << ", iterations=" << result.iterations << ", coarseMipRays=" << result.coarseMipRays << '\n';
        return result;
    };

    placement.reset = true; render();
    const auto placed = lab.ReadbackScreenProbePlacement();
    const auto hierarchical = lab.ReadbackScreenProbeTrace();
    checkHzb(hierarchical, "hierarchical");
    const auto hierarchyMetrics = inspectRays(hierarchical, placed, "hierarchical", true);
    Check(hierarchyMetrics.positiveHits > 0 && hierarchyMetrics.coarseMipRays > 0,
          "Hierarchical tracing did not read positive HDR or traverse coarse HZB cells");
    save("hzb_radiance"); tracing.displayMode = 1; render(); save("hzb_status"); tracing.displayMode = 0;

    // Changing only the permitted maximum mip keeps the tracing algorithm,
    // origin, geometry and color source identical. Mip zero measures its true
    // fine-grid fetch cost; it is not used as the geometry correctness oracle.
    tracing.maxMip = 0; render();
    const auto fine = lab.ReadbackScreenProbeTrace();
    const auto fineMetrics = inspectRays(fine, lab.ReadbackScreenProbePlacement(), "mip zero");
    unsigned completedRays = 0, excludedBudget = 0, hitUnion = 0, commonHits = 0, agreeingHits = 0;
    double hierarchyCompletedFetches = 0, fineCompletedFetches = 0;
    for (std::size_t ray = 0; ray < fine.radianceStatus.size(); ++ray) {
        const auto a = unsigned(std::round(hierarchical.radianceStatus[ray].w));
        const auto b = unsigned(std::round(fine.radianceStatus[ray].w));
        if (a == unsigned(Render::ScreenProbeTraceStatus::StepBudget) || b == unsigned(Render::ScreenProbeTraceStatus::StepBudget)) {
            ++excludedBudget; continue;
        }
        if (hierarchical.hitPixelStepsMip[ray].z > 0 && fine.hitPixelStepsMip[ray].z > 0) {
            ++completedRays; hierarchyCompletedFetches += hierarchical.rayDirectionDepthFetches[ray].w;
            fineCompletedFetches += fine.rayDirectionDepthFetches[ray].w;
        }
        const bool aHit = a == unsigned(Render::ScreenProbeTraceStatus::Hit), bHit = b == unsigned(Render::ScreenProbeTraceStatus::Hit);
        if (aHit || bHit) ++hitUnion;
        if (!aHit || !bHit) continue;
        ++commonHits;
        const auto wa = glm::vec3(hierarchical.hitWorldDistance[ray]), wb = glm::vec3(fine.hitWorldDistance[ray]);
        const auto clip = frame.camera->projection * frame.camera->view * glm::vec4(wa, 1);
        const auto corner = glm::inverse(frame.camera->projection * frame.camera->view) *
            glm::vec4(glm::vec3(clip) / clip.w + glm::vec3(1.f / frame.width, 1.f / frame.height, 0), 1);
        const float worldTolerance = std::max(.02f, 3 * glm::length(glm::vec3(corner) / corner.w - wa));
        if (glm::length(glm::vec2(hierarchical.hitPixelStepsMip[ray]) - glm::vec2(fine.hitPixelStepsMip[ray])) <= 2 &&
            glm::length(wa - wb) < worldTolerance) ++agreeingHits;
    }
    Check(fineMetrics.coarseMipRays == 0 && completedRays >= 32 && hierarchyCompletedFetches < fineCompletedFetches,
          "HZB traversal did not reduce actual depth fetches for completed rays against the same mip-zero tracer");
    Check(hitUnion >= 32 && commonHits >= hitUnion * .90 && agreeingHits >= commonHits * .90,
          "Hierarchical cell skipping changed too many completed mip-zero hit classifications or surfaces");
    std::cout << "Screen trace completed HZB/mip-zero: rays=" << completedRays << ", excluded budget=" << excludedBudget
              << ", common/union hits=" << commonHits << '/' << hitUnion << ", agreeing surfaces=" << agreeingHits
              << ", depthfetch ratio=" << hierarchyCompletedFetches / std::max(fineCompletedFetches, 1.)
              << "; all-ray counts=" << hierarchyMetrics.depthFetches << '/' << fineMetrics.depthFetches << '\n';
    save("mip0_reference"); tracing.maxMip = 10;

    // A genuinely black visible surface is still a successful hit. Turning
    // lighting off leaves geometry and trace classification unchanged.
    const auto lighting = frame.effects.lighting;
    const auto shadows = frame.effects.shadows;
    frame.effects.lighting.ambientAndExposure = {0, 0, 0, 1};
    for (auto& color : frame.effects.lighting.lightColors) color = glm::vec4(0);
    frame.effects.shadows.sunIntensity = 0; frame.effects.sky.enabled = false;
    frame.effects.spotLight.enabled = false; frame.effects.areaLights.count = 0;
    render(); const auto black = lab.ReadbackScreenProbeTrace();
    const auto blackMetrics = inspectRays(black, lab.ReadbackScreenProbePlacement(), "black surface");
    Check(blackMetrics.blackHits == blackMetrics.statuses[unsigned(Render::ScreenProbeTraceStatus::Hit)] &&
          blackMetrics.blackHits > 0, "Black radiance was incorrectly treated as missing source color");
    for (std::size_t ray = 0; ray < black.radianceStatus.size(); ++ray)
        Check(black.radianceStatus[ray].w == hierarchical.radianceStatus[ray].w,
              "Extinguishing illumination changed screen-ray geometry classification");
    tracing.displayMode = 1; render(); save("black_hit_status"); tracing.displayMode = 0;
    frame.effects.lighting = lighting; frame.effects.shadows = shadows;

    tracing.rayResolution = 4; tracing.maxSteps = 128; render();
    inspectRays(lab.ReadbackScreenProbeTrace(), lab.ReadbackScreenProbePlacement(), "compact 4x4 default budget");
    save("compact_4x4"); tracing.rayResolution = 8; tracing.maxSteps = 512;
    render(); placement.freeze = true; render();
    const auto frozen = lab.ReadbackScreenProbePlacement();
    frame.camera = ProbePlacementCameraPreset(1, frame.OutputAspect()); frame.effects.cameraCut = true; render();
    const auto frozenMoved = lab.ReadbackScreenProbePlacement();
    Check(PlacementSamePositions(frozen.uniform, frozenMoved.uniform) && PlacementSamePositions(frozen.spawned, frozenMoved.spawned),
          "Moving the trace camera also moved frozen source probes");
    const auto frozenMetrics = inspectRays(lab.ReadbackScreenProbeTrace(), frozenMoved, "frozen probes/current camera");
    Check(frozenMetrics.statuses[unsigned(Render::ScreenProbeTraceStatus::OriginUnavailable)] > 0,
          "Frozen probes hidden from the new source camera traced unavailable color");
    save("frozen_origin_status"); placement.freeze = false;

    frame.width = 637; frame.height = 401; frame.camera = ProbePlacementCameraPreset(1, frame.OutputAspect());
    frame.effects.cameraCut = true; render(); const auto odd = lab.ReadbackScreenProbeTrace();
    checkHzb(odd, "odd extent"); inspectRays(odd, lab.ReadbackScreenProbePlacement(), "odd extent");
    save("odd_hzb");

    // The live 16px density setting at normal presentation size exceeds an
    // 8192-row ray atlas if its width remains fixed at 1024 texels. Exercise
    // the wider allocation without repeating expensive source geometry QA.
    frame.width = 1280; frame.height = 800;
    frame.camera = ProbePlacementCameraPreset(0, frame.OutputAspect());
    placement.baseSpacing = 16; placement.candidateCount = 8; placement.adaptive = false;
    placement.freeze = false; placement.reset = true;
    tracing.rayResolution = 8; tracing.maxSteps = 128; tracing.maxMip = 10;
    frame.effects.cameraCut = true; render();
    const auto capacity = lab.ReadbackScreenProbeTrace(false);
    const std::size_t capacitySlots = std::size_t(80) * 50 * 9;
    const std::size_t capacityRays = capacitySlots * 64;
    Check(capacity.atlasSize.x > 1024 && capacity.atlasSize.x <= 8192 &&
          capacity.atlasSize.y > 0 && capacity.atlasSize.y <= 8192 &&
          std::size_t(capacity.atlasSize.x) * capacity.atlasSize.y >= capacityRays * 4,
          "Normal-size 16px placement exceeded the supported ray-atlas dimensions");
    Check(capacity.uniformGridSize == glm::uvec2(80, 50) && capacity.probeSlots == capacitySlots &&
          capacity.rayResolution == 8 && capacity.candidateCount == 8 &&
          capacity.radianceStatus.size() == capacityRays && capacity.hitWorldDistance.size() == capacityRays &&
          capacity.hitPixelStepsMip.size() == capacityRays && capacity.rayDirectionDepthFetches.size() == capacityRays,
          "Widening the ray atlas truncated logical probe slots or decoded padding as rays");
    unsigned capacityHits = 0;
    for (const auto& ray : capacity.radianceStatus) {
        Check(std::isfinite(ray.w) && ray.w >= 0 && ray.w <= 8 && std::abs(ray.w - std::round(ray.w)) < .001f,
              "Dense ray-atlas allocation produced an invalid trace status");
        if (unsigned(std::round(ray.w)) == unsigned(Render::ScreenProbeTraceStatus::Hit)) ++capacityHits;
    }
    Check(capacityHits > 0, "Dense ray-atlas allocation lost all visible surface hits");
    std::cout << "Screen trace 1280x800/16px capacity: atlas=" << capacity.atlasSize.x << 'x' << capacity.atlasSize.y
              << ", probe slots=" << capacitySlots << ", logical rays=" << capacityRays << ", hits=" << capacityHits << '\n';
    std::cout << "Screen-probe HZB tracing passed: visible-depth min pyramid, true surface/color hits, rejected/offscreen/miss statuses, "
                 "legal black hits, completed-ray hierarchical cost/hit agreement, compact rays, frozen origins, odd extents and dense atlas capacity.\n";
}
