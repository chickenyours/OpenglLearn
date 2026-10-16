// Included after MakeProbePlacementFrame in the demo's anonymous namespace.
// Geometry checks use independently intersected visible CPU triangles; no
// placement tile, candidate jitter or depth-similarity rule is reproduced here.
struct PlacementReferenceHit {
    float distance = std::numeric_limits<float>::infinity();
    glm::vec3 position{0}, normal{0};
    int object = -1;
};
float PlacementTriangleDistance(glm::vec3 origin, glm::vec3 direction,
                                const Render::ProbeTriangle& triangle, float limit) {
    const auto ab = triangle.b - triangle.a, ac = triangle.c - triangle.a;
    const auto h = glm::cross(direction, ac);
    const float determinant = glm::dot(ab, h);
    if (std::abs(determinant) < 1e-8f) return limit;
    const float inverse = 1 / determinant;
    const auto displacement = origin - triangle.a;
    const float u = glm::dot(displacement, h) * inverse;
    if (u < -1e-6f || u > 1 + 1e-6f) return limit;
    const auto q = glm::cross(displacement, ab);
    const float v = glm::dot(direction, q) * inverse;
    if (v < -1e-6f || u + v > 1 + 1e-6f) return limit;
    const float distance = glm::dot(ac, q) * inverse;
    return distance > .0001f && distance < limit ? distance : limit;
}
int PlacementReferenceObject(glm::vec3 world, const std::vector<glm::mat4>& inverses) {
    const auto& objects = ProbePlacementObjects();
    for (unsigned index = 0; index < objects.size(); ++index) {
        const auto local = glm::vec3(inverses[index] * glm::vec4(world, 1));
        if (objects[index].mesh == ShowcaseMesh::Plane) {
            if (std::abs(local.y) < .002f && std::abs(local.x) < 1.002f && std::abs(local.z) < 1.002f)
                return int(index);
        } else if (objects[index].mesh == ShowcaseMesh::Sphere) {
            // The visible sphere is polygonal; this associates its exact ray
            // hit with the owning object rather than replacing its geometry.
            if (std::abs(glm::length(local) - 1) < .01f) return int(index);
        } else {
            const auto magnitude = glm::abs(local);
            if (magnitude.x > 1.002f || magnitude.y > 1.002f || magnitude.z > 1.002f) continue;
            if (std::min({std::abs(magnitude.x - 1), std::abs(magnitude.y - 1), std::abs(magnitude.z - 1)}) < .002f)
                return int(index);
        }
    }
    return -1;
}
PlacementReferenceHit PlacementTraceReference(glm::vec3 origin, glm::vec3 direction, float limit,
                                              const std::vector<Render::ProbeTriangle>& triangles,
                                              const std::vector<glm::mat4>& inverses) {
    PlacementReferenceHit result; result.distance = limit;
    for (const auto& triangle : triangles) {
        const float hit = PlacementTriangleDistance(origin, direction, triangle, result.distance);
        if (hit < result.distance) {
            result.distance = hit;
            result.position = origin + direction * hit;
            result.normal = glm::normalize(glm::cross(triangle.b - triangle.a, triangle.c - triangle.a));
            result.object = PlacementReferenceObject(result.position, inverses);
        }
    }
    return result;
}
unsigned PlacementActiveCount(const std::vector<glm::vec4>& positions, float minimumStatus = .5f) {
    return unsigned(std::count_if(positions.begin(), positions.end(),
                                 [&](const auto& p) { return p.w > minimumStatus; }));
}
bool PlacementSamePositions(const std::vector<glm::vec4>& a, const std::vector<glm::vec4>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t index = 0; index < a.size(); ++index)
        if (glm::length(a[index] - b[index]) > 1e-5f) return false;
    return true;
}
void ProbePlacementQuality(MaterialLabModule& lab, const Options& options) {
    auto frame = MakeProbePlacementFrame(options);
    frame.screenProbeStudy = true; frame.screenProbeStudyHud = false; frame.present = false;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    frame.effects.temporal.msaaSamples = 1;
    frame.effects.post.bloomEnabled = false; frame.effects.post.ssaoEnabled = false;
    frame.width = options.studySizeSet ? options.benchmarkWidth : 640;
    frame.height = options.studySizeSet ? options.benchmarkHeight : 400;
    frame.effects.outputSize = {0, 0};
    frame.camera = ProbePlacementCameraPreset(0, frame.OutputAspect());
    auto& placement = frame.effects.screenProbePlacement;
    placement.enabled = true; placement.freeze = false; placement.adaptive = false;
    placement.baseSpacing = 32; placement.candidateCount = 8; placement.frameIndex = 0;
    placement.debugView = 2;
    const auto render = [&] {
        Check(lab.Render(frame), lab.LastError()); frame.effects.cameraCut = false; placement.reset = false;
    };
    const auto save = [&](const char* label) {
        if (options.screenshot.empty()) return;
        auto path = std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string() + "_" + label + path.extension().string());
        Save(lab.Readback(), path.string());
    };
    const auto geometry = [&] { return ProbePlacementTriangles(frame.screenProbeStudyState); };
    const auto transforms = [&] {
        std::vector<glm::mat4> result;
        for (const auto& object : ProbePlacementObjects())
            result.push_back(glm::inverse(ProbePlacementTransform(object, frame.screenProbeStudyState)));
        return result;
    };
    const auto validate = [&](const auto& diagnostics, const Render::PipelineCamera& camera,
                              const std::vector<Render::ProbeTriangle>& triangles,
                              const std::vector<glm::mat4>& inverses, const char* label) {
        const unsigned gridWidth = (frame.width + placement.baseSpacing - 1) / placement.baseSpacing;
        const unsigned gridHeight = (frame.height + placement.baseSpacing - 1) / placement.baseSpacing;
        Check(diagnostics.sourceWidth == frame.width && diagnostics.sourceHeight == frame.height &&
              diagnostics.uniformGridSize == glm::uvec2(gridWidth, gridHeight) &&
              diagnostics.candidateCount == placement.candidateCount,
              std::string(label) + ": placement diagnostics report the wrong input/grid extent");
        const std::size_t uniformSlots = std::size_t(gridWidth) * gridHeight;
        Check(diagnostics.uniform.size() == uniformSlots && diagnostics.uniformNormals.size() == uniformSlots &&
              diagnostics.marked.size() == uniformSlots * placement.candidateCount &&
              diagnostics.spawned.size() == diagnostics.marked.size() &&
              diagnostics.markedNormals.size() == diagnostics.marked.size() &&
              diagnostics.spawnedNormals.size() == diagnostics.spawned.size(),
              std::string(label) + ": decoded diagnostics retained padding or lost probe slots");
        const auto vp = camera.projection * camera.view;
        const auto checkPositions = [&](const auto& positions, const auto& normals, const char* kind) {
            unsigned checked = 0;
            for (std::size_t index = 0; index < positions.size(); ++index) {
                const auto& point = positions[index];
                for (unsigned channel = 0; channel < 4; ++channel)
                    Check(std::isfinite(point[channel]), std::string(label) + ": nonfinite " + kind + " position/status");
                if (point.w <= .5f) continue;
                const auto world = glm::vec3(point);
                const auto projected = vp * glm::vec4(world, 1);
                Check(projected.w > 0, std::string(label) + ": probe was placed behind the camera");
                const auto ndc = glm::vec3(projected) / projected.w;
                Check(std::abs(ndc.x) < 1.0001f && std::abs(ndc.y) < 1.0001f && std::abs(ndc.z) <= 1.0001f,
                      std::string(label) + ": probe was placed outside the input viewport");
                const auto displacement = world - camera.position;
                const float distance = glm::length(displacement);
                const auto hit = PlacementTraceReference(camera.position, displacement / distance,
                                                        camera.farPlane, triangles, inverses);
                Check(hit.object >= 0 && std::abs(hit.distance - distance) < .004f + distance * .0001f,
                      std::string(label) + ": " + kind + " world position is not on the nearest visible scene surface");
                const auto normal = glm::vec3(normals[index]);
                Check(std::isfinite(normals[index].w) && normals[index].w > 0 &&
                      std::abs(glm::length(normal) - 1) < .025f && glm::dot(normal, hit.normal) > .90f,
                      std::string(label) + ": " + kind + " normal/depth does not belong to its visible receiver");
                ++checked;
            }
            return checked;
        };
        const unsigned uniform = checkPositions(diagnostics.uniform, diagnostics.uniformNormals, "uniform");
        checkPositions(diagnostics.marked, diagnostics.markedNormals, "candidate");
        const unsigned spawned = checkPositions(diagnostics.spawned, diagnostics.spawnedNormals, "spawned");
        const unsigned marked = PlacementActiveCount(diagnostics.marked, 1.5f);
        Check(uniform > 0 && spawned <= marked, std::string(label) + ": active spawn count exceeds the marked candidate count");
        std::vector<std::uint64_t> pixels;
        for (std::size_t index = 0; index < diagnostics.spawned.size(); ++index) {
            if (diagnostics.spawned[index].w <= .5f) continue;
            Check(diagnostics.marked[index].w > 1.5f,
                  std::string(label) + ": spawn slot was not marked as unrepresented");
            const auto clip = vp * glm::vec4(glm::vec3(diagnostics.spawned[index]), 1);
            const auto pixel = (glm::vec2(clip) / clip.w * .5f + .5f) * glm::vec2(frame.width, frame.height);
            pixels.push_back((std::uint64_t(unsigned(std::floor(pixel.y))) << 32) | unsigned(std::floor(pixel.x)));
        }
        std::sort(pixels.begin(), pixels.end());
        Check(std::adjacent_find(pixels.begin(), pixels.end()) == pixels.end(),
              std::string(label) + ": duplicate candidate pixels spawned multiple probes");
        std::cout << "Screen-probe placement " << label << ": uniform=" << uniform
                  << ", marked=" << marked << ", spawned=" << spawned
                  << ", grid=" << gridWidth << 'x' << gridHeight << ", candidates=" << placement.candidateCount << '\n';
    };

    placement.reset = true; render();
    const auto uniform = lab.ReadbackScreenProbePlacement();
    const auto triangles = geometry(); const auto inverses = transforms();
    validate(uniform, *frame.camera, triangles, inverses, "uniform only");
    Check(PlacementActiveCount(uniform.spawned) == 0, "Uniform-only placement spawned adaptive probes");
    save("uniform");
    placement.adaptive = true; render();
    const auto adaptive = lab.ReadbackScreenProbePlacement();
    validate(adaptive, *frame.camera, triangles, inverses, "adaptive");
    Check(PlacementActiveCount(adaptive.spawned) > 0, "Depth edges and thin geometry did not spawn additional probes");
    Check(PlacementSamePositions(uniform.uniform, adaptive.uniform), "Adaptive placement moved the uniform base probes");
    save("adaptive");
    placement.debugView = 0; render(); save("world_markers");
    placement.debugView = 1; render(); save("candidates");
    placement.debugView = 2;

    // A planar region well inside the right side of the back wall has neither
    // an occluder nor a silhouette. Refinement should stay local to geometry.
    unsigned flatUniform = 0, flatSpawned = 0;
    const auto flatInterior = [](const glm::vec4& point) {
        return point.w > .5f && std::abs(point.z + 3.5f) < .002f &&
               point.x > 3.3f && point.x < 4.5f && point.y > 2.8f && point.y < 4.4f;
    };
    for (const auto& point : adaptive.uniform) if (flatInterior(point)) ++flatUniform;
    for (const auto& point : adaptive.spawned) if (flatInterior(point)) ++flatSpawned;
    Check(flatUniform >= 2, "Placement fixture does not contain enough visible flat-wall base probes");
    Check(flatSpawned == 0, "An unobstructed planar wall interior unnecessarily spawned detail probes");

    struct PlacedReceiver { glm::vec2 screen; int object; };
    std::vector<PlacedReceiver> baseReceivers, detailReceivers;
    const auto vp = frame.camera->projection * frame.camera->view;
    const auto append = [&](const auto& positions, auto& receivers) {
        for (const auto& point : positions) if (point.w > .5f) {
            const auto clip = vp * glm::vec4(glm::vec3(point), 1);
            receivers.push_back({(glm::vec2(clip) / clip.w * .5f + .5f) * glm::vec2(frame.width, frame.height),
                                 PlacementReferenceObject(glm::vec3(point), inverses)});
        }
    };
    append(adaptive.uniform, baseReceivers); detailReceivers = baseReceivers;
    append(adaptive.spawned, detailReceivers);
    const auto inverseVP = glm::inverse(vp);
    const auto& objects = ProbePlacementObjects();
    std::vector<unsigned> referencePixels(objects.size(), 0), baseCovered(objects.size(), 0), detailCovered(objects.size(), 0);
    double baseDistance = 0, detailDistance = 0; unsigned featurePixels = 0;
    const float coverageRadius = float(placement.baseSpacing) * .5f;
    const unsigned step = std::max(2u, frame.width / 240);
    for (unsigned y = step; y + step < frame.height; y += step) for (unsigned x = step; x + step < frame.width; x += step) {
        const glm::vec2 pixel(float(x) + .5f, float(y) + .5f);
        const auto ndc = pixel / glm::vec2(frame.width, frame.height) * 2.f - 1.f;
        const auto farPoint = inverseVP * glm::vec4(ndc, 1, 1);
        const auto direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - frame.camera->position);
        const auto hit = PlacementTraceReference(frame.camera->position, direction, frame.camera->farPlane, triangles, inverses);
        if (hit.object < 0 || objects[hit.object].mesh == ShowcaseMesh::Plane) continue;
        const auto nearest = [&](const auto& receivers) {
            float result = float(placement.baseSpacing) * 4;
            for (const auto& receiver : receivers) if (receiver.object == hit.object)
                result = std::min(result, glm::length(receiver.screen - pixel));
            return result;
        };
        const float coarse = nearest(baseReceivers), fine = nearest(detailReceivers);
        ++referencePixels[hit.object]; ++featurePixels; baseDistance += coarse; detailDistance += fine;
        if (coarse < coverageRadius) ++baseCovered[hit.object];
        if (fine < coverageRadius) ++detailCovered[hit.object];
    }
    Check(featurePixels >= 64, "CPU placement coverage reference did not see enough foreground geometry");
    bool thinImproved = false;
    for (unsigned index = 0; index < objects.size(); ++index) if (referencePixels[index]) {
        std::cout << "Screen-probe receiver " << objects[index].name << ": CPU visible pixels=" << referencePixels[index]
                  << ", covered uniform/adaptive=" << baseCovered[index] << '/' << detailCovered[index] << '\n';
        Check(detailCovered[index] >= baseCovered[index], "Additional probes reduced same-surface geometric coverage");
        if (objects[index].material == ProbePlacementMaterial::Thin && detailCovered[index] > baseCovered[index]) thinImproved = true;
    }
    std::cout << "Screen-probe geometry coverage: flat uniform/spawned=" << flatUniform << '/' << flatSpawned
              << ", mean same-receiver probe distance uniform/adaptive=" << baseDistance / featurePixels << '/'
              << detailDistance / featurePixels << " pixels\n";
    Check(thinImproved, "Adaptive placement did not improve coverage of the independently traced thin post");
    Check(detailDistance < baseDistance * .98, "Additional probes did not improve foreground surface coverage");

    // Fixing the candidate phase makes placement exactly repeatable even when
    // the renderer's ordinary frame sequence advances.
    render(); const auto repeated = lab.ReadbackScreenProbePlacement();
    Check(PlacementSamePositions(adaptive.uniform, repeated.uniform) &&
          PlacementSamePositions(adaptive.marked, repeated.marked) &&
          PlacementSamePositions(adaptive.spawned, repeated.spawned), "Static fixed-phase placement is nondeterministic");
    placement.frameIndex = 1; render(); const auto phase1 = lab.ReadbackScreenProbePlacement();
    validate(phase1, *frame.camera, triangles, inverses, "phase 1");
    Check(!PlacementSamePositions(adaptive.uniform, phase1.uniform) &&
          !PlacementSamePositions(adaptive.marked, phase1.marked),
          "Changing the placement phase did not move uniform and candidate sample points");
    save("phase1");
    placement.frameIndex = 0; render(); const auto restoredPhase = lab.ReadbackScreenProbePlacement();
    Check(PlacementSamePositions(adaptive.uniform, restoredPhase.uniform) &&
          PlacementSamePositions(adaptive.marked, restoredPhase.marked) &&
          PlacementSamePositions(adaptive.spawned, restoredPhase.spawned),
          "Restoring the placement phase did not reproduce its original world positions");
    placement.freeze = true; render(); const auto frozen = lab.ReadbackScreenProbePlacement();
    frame.camera = ProbePlacementCameraPreset(1, frame.OutputAspect()); frame.effects.cameraCut = true;
    render(); const auto frozenMoved = lab.ReadbackScreenProbePlacement();
    Check(PlacementSamePositions(frozen.uniform, frozenMoved.uniform) &&
          PlacementSamePositions(frozen.marked, frozenMoved.marked) &&
          PlacementSamePositions(frozen.spawned, frozenMoved.spawned), "Frozen probes moved with the observer camera");
    save("frozen_camera");
    placement.freeze = false; render(); const auto relocated = lab.ReadbackScreenProbePlacement();
    validate(relocated, *frame.camera, triangles, inverses, "camera refresh");
    Check(!PlacementSamePositions(frozen.uniform, relocated.uniform), "Unfreezing retained placement from the old camera");
    save("camera_refresh");

    frame.screenProbeStudyState.occluderOffset = .45f; render();
    const auto movedGeometry = lab.ReadbackScreenProbePlacement();
    validate(movedGeometry, *frame.camera, geometry(), transforms(), "geometry refresh");
    Check(!PlacementSamePositions(relocated.uniform, movedGeometry.uniform), "Moved foreground geometry retained stale probe positions");
    save("moved_geometry");

    // Resize while frozen: the old layout and projection cannot be reused for
    // a different input extent, including partially filled edge tiles.
    placement.freeze = true; frame.width = 637; frame.height = 401;
    frame.camera = ProbePlacementCameraPreset(0, frame.OutputAspect()); frame.effects.cameraCut = true;
    render(); const auto odd = lab.ReadbackScreenProbePlacement();
    validate(odd, *frame.camera, geometry(), transforms(), "odd resize");
    Check(!PlacementSamePositions(movedGeometry.uniform, odd.uniform), "Resize reused the frozen placement from a different extent");
    save("odd_resize");

    // Deliver a readable normal-resolution scene after the diagnostic cases.
    // HUD counts come from actual active GPU slots, not allocated atlas area.
    frame.screenProbeStudyState = {};
    frame.width = options.studySizeSet ? options.benchmarkWidth : 1280;
    frame.height = options.studySizeSet ? options.benchmarkHeight : 800;
    frame.effects.outputSize = {0, 0};
    frame.camera = ProbePlacementCameraPreset(0, frame.OutputAspect());
    placement.adaptive = true; placement.freeze = false; placement.frameIndex = 0;
    placement.debugView = 0; placement.reset = true; frame.effects.cameraCut = true;
    render(); const auto overview = lab.ReadbackScreenProbePlacement();
    frame.placementUniformCount = PlacementActiveCount(overview.uniform);
    frame.placementAdaptiveCount = PlacementActiveCount(overview.spawned);
    frame.placementMarkedCount = PlacementActiveCount(overview.marked, 1.5f);
    frame.screenProbeStudyHud = true; render(); save("overview");
    std::cout << "Screen-probe placement passed: visible surface positions/normals, mark/spawn ownership, pixel deduplication, "
                 "flat-region budget, thin-object coverage, deterministic phase variation, frozen world positions, camera/geometry refresh and odd extent.\n";
}
