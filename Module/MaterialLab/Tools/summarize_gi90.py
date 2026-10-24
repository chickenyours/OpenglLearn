"""Summarize the GI budget experiment without mixing timed and diagnostic frames."""
import argparse
import collections
import hashlib
import json
import math
import pathlib
import re
import statistics


def records(path, prefix):
    for line in pathlib.Path(path).read_text(encoding="utf-8-sig", errors="replace").splitlines():
        if line.startswith(prefix):
            yield dict(re.findall(r"(\w+)=([^\s]+)", line))


def summary(values):
    values = sorted(values)
    assert values and all(math.isfinite(x) and x >= 0 for x in values)
    return dict(n=len(values), mean=statistics.mean(values), median=statistics.median(values),
                p95=values[math.ceil(.95 * len(values)) - 1])


def main():
    cli = argparse.ArgumentParser()
    cli.add_argument("log", type=pathlib.Path)
    cli.add_argument("--output", required=True, type=pathlib.Path)
    cli.add_argument("--quality", type=pathlib.Path)
    cli.add_argument("--audit", type=pathlib.Path)
    cli.add_argument("--report-rejected-quality", action="store_true",
                     help="Write a report with quality.accepted=false; never changes any quality limit")
    args = cli.parse_args()
    text = args.log.read_text(encoding="utf-8-sig", errors="replace")
    assert "GI90 PERFORMANCE COMPLETE" in text, "Incomplete performance run"
    setup = next(records(args.log, "GI90 SETUP"))
    assert setup.get("build") == "Release_NDEBUG" and setup["present"] == "1"
    n = int(setup["measured"])
    groups = collections.defaultdict(list)
    for record in records(args.log, "GI90 SAMPLE"):
        assert record["present"] == "1"
        groups[(int(record["profile"]), record["path"])].append(record)
    assert len(groups) == 10, "Expected baseline/candidate across all five trajectories"
    result = dict(source=str(args.log), sha256=hashlib.sha256(args.log.read_bytes()).hexdigest(),
                  setup=setup, trajectories=[], quality=None, audit=[])
    result["measurement_notes"] = {
        "acceleration_build_nested": "GPU AS maintenance nested in its first native scene consumer; already included in that parent pass and the full GPU frame. Do not add it again.",
        "host_intervals": "as_prepare is inside command_encode; as_pack is inside as_prepare. pipeline_prepare is inside record, and geometry details are inside sdf_prepare/prepare. Fence wait overlaps GPU execution; these intervals are not additive.",
        "graphics_counters": "Field names remain graphics_* for compatibility. Since trial 51, descriptor allocations and uniform snapshot copies include graphics plus frame-compute work; graphics_draw_calls still counts graphics draws only.",
        "contact_risk_mask": "Detailed risk/mask scopes are inside contact_trace on the original gather, and outside contact_trace on native screen-probe transport. The per-trajectory nested marker records which applies; do not add nested scopes again.",
        "native_probe_details": "native_neighbors/native_coefficient_filter are inside probe_spatial. Legacy native_base_reconstruct is inside probe_spatial, or contact_mask when native_base_timed_in_contact_mask=1; UE High uses that scope for separate half-resolution integration outside probe_spatial. Scope movement is not a saving; do not add nested details to their parents again.",
        "snapshot_work": "geometry_snapshot_full_bytes is full immutable storage size, not actual copy work. geometry_snapshot_copy_bytes records actual copied rows or full-copy fallback.",
        "placement_details": "placement_uniform/mark/spawn are nested in placement. Logical Mark invocations alone are not a measured speedup.",
        "as_counters": "as_builds/as_updates/as_full_builds count native scene maintenance invocations, not rays or changed primitive counts. Actual ray queries come from the untimed audit."
    }
    stages = ("screen_trace", "sdf_trace", "reconstruction", "probe_spatial", "contact_trace",
              "contact_filter", "contact_risk", "contact_mask", "native_neighbors", "native_coefficient_filter", "native_base_reconstruct", "temporal", "cache_lighting", "receiver_motion", "fresh_sampling",
              "acceleration_build_nested", "area_shadow", "sun_shadows", "reflection", "opaque", "placement", "placement_uniform", "placement_mark", "placement_spawn", "ue_radiance_cache", "ue_probe_importance", "post_output", "receiver_capture", "cached_direct")
    for (profile, path), rows in sorted(groups.items()):
        assert len(rows) == 2 * n and len({(r["trial"], r["frame"]) for r in rows}) == 2 * n
        value = dict(profile=profile, path=path, loop=summary([float(r["loop"]) for r in rows]),
                     gpu=summary([float(r["gpu"]) for r in rows]), stages={}, counters={})
        value["fps"] = 1000 / value["loop"]["mean"]
        value["meets_90_mean_and_p95"] = max(value["loop"]["mean"], value["loop"]["p95"]) <= 1000 / 90
        # AS preparation/packing are nested within command encoding, and the
        # fence interval overlaps GPU execution. These are not additive costs.
        value["host_intervals"] = {key: summary([float(r[key]) for r in rows]) for key in
                                   ("prepare", "record", "as_prepare", "as_pack", "command_encode", "fence")}
        for key in ("sdf_prepare", "cache_prepare", "pipeline_prepare", "shadow_record", "geometry_transform", "geometry_snapshot", "geometry_refit", "geometry_metadata"):
            if all(key in r for r in rows):
                value["host_intervals"][key] = summary([float(r[key]) for r in rows])
        for key in stages:
            samples = [float(r[key]) for r in rows if key in r]
            value["stages"][key] = dict(executed=len(samples), amortized_ms=sum(samples) / len(rows),
                                        executed_summary=summary(samples) if samples else None)
            if key in ("placement_uniform", "placement_mark", "placement_spawn"):
                value["stages"][key]["nested"] = True
                value["stages"][key]["parent"] = "placement"
            if key == "acceleration_build_nested":
                value["stages"][key]["nested"] = True
                value["stages"][key]["parent"] = "first native scene consuming pass"
            if key in ("contact_risk", "contact_mask") and samples:
                flags = {r.get("contact_risk_mask_nested_in_contact_trace") for r in rows}
                assert len(flags) == 1 and flags <= {"0", "1"}, "Missing or inconsistent contact timing nesting"
                nested = flags == {"1"}
                value["stages"][key]["nested"] = nested
                value["stages"][key]["parent"] = "contact_trace" if nested else "GPU frame"
            if key in ("native_neighbors", "native_coefficient_filter", "native_base_reconstruct") and samples:
                value["stages"][key]["nested"] = True
                value["stages"][key]["parent"] = "probe_spatial"
                if key == "native_base_reconstruct":
                    flags = {r.get("native_base_timed_in_contact_mask", "0") for r in rows}
                    assert len(flags) == 1 and flags <= {"0", "1"}, "Inconsistent native base timing nesting"
                    if {r.get("ue_lumen_high", "0") for r in rows} == {"1"}:
                        # UE gather ends probe_spatial before SH conversion and
                        # this separate half-resolution integration scope.
                        value["stages"][key]["nested"] = False
                        value["stages"][key]["parent"] = "GPU frame"
                    elif flags == {"1"}:
                        value["stages"][key]["parent"] = "contact_mask"
        for key in ("capacity", "spatial_texels", "shadow_passes", "cache_reused", "raw_omitted", "sdf_cpu_voxels",
                    "graphics_draw_calls", "graphics_descriptor_allocations", "graphics_uniform_snapshot_copies",
                    "descriptor_writes", "descriptor_cache_hits", "uniform_snapshot_copied_bytes",
                    "as_builds", "as_storage_reuses", "as_buffer_allocations", "as_handle_creations", "as_updates", "as_full_builds",
                    "receiver_motion_static_reuse", "receiver_motion_draws", "receiver_motion_static_owners", "native_reconstruction_fused",
                    "geometry_snapshot_copy_bytes", "geometry_snapshot_full_bytes", "geometry_snapshot_patch_rows", "geometry_snapshot_catchup", "geometry_snapshot_versions", "native_force_raster_core", "native_receiver_filter_support_halo", "ue_lumen_high"):
            value["counters"][key] = sum(int(r.get(key, 0)) for r in rows)
        value["snapshot_fallback_counts"] = {}
        for row in rows:
            if "geometry_snapshot_fallback" in row:
                key = row["geometry_snapshot_fallback"]
                value["snapshot_fallback_counts"][key] = value["snapshot_fallback_counts"].get(key, 0) + 1
        result["trajectories"].append(value)
    if args.quality:
        quality_text = args.quality.read_text(encoding="utf-8-sig", errors="replace")
        images = list(records(args.quality, "GI90 QUALITY view="))
        oracles = list(records(args.quality, "GI90 ORACLE"))
        returns = list(records(args.quality, "GI90 RETURN"))
        validity = list(records(args.quality, "GI90 VALIDITY"))
        accepted = ("GI90 QUALITY COMPLETE accepted=1" in quality_text and
                    "GI90 CACHE CONTRACTS PASS" in quality_text and len(images) == 20 and
                    len(oracles) == 40 and len(returns) == 2 and len(validity) == 4 and
                    all(r["accepted"] == "1" and int(r["contact_groups"]) > 0 for r in images) and
                    all(r["accepted"] == "1" for r in returns) and
                    all(r["light_off"] == r["moving_black"] == r["unknown_black"] == "1" for r in validity) and
                    all(r["failures"] == "0" for r in oracles))
        result["quality"] = dict(accepted=accepted, source=str(args.quality), images=images, returns=returns,
                                  checked_rays=sum(int(r["checked"]) for r in oracles),
                                  near_checked_rays=sum(int(r["near_checked"]) for r in oracles), validity=validity,
                                  endpoint_max_m=max((float(r["endpoint_max_m"]) for r in oracles), default=None))
        assert accepted or args.report_rejected_quality, "Quality contracts did not pass"
    if args.audit:
        assert "GI90 AUDIT COMPLETE" in args.audit.read_text(encoding="utf-8-sig", errors="replace")
        result["audit"] = list(records(args.audit, "GI90 AUDIT profile="))
        assert len(result["audit"]) == 10, "Incomplete untimed actual-ray census"
        assert set((int(r["profile"]), r["path"]) for r in result["audit"]) == set(groups)
        assert all(r["sampling_budget_exact"] == "1" and
                   r["actual_receiver_rays"] == r["expected_receiver_rays"] for r in result["audit"])
        result["audit_oracles"] = list(records(args.audit, "GI90 ORACLE"))
        expected = sum(int(r["profile"]) >= 4 for r in result["audit"])
        result["audit_accepted"] = (len(result["audit_oracles"]) == expected and
                                    all(r["failures"] == "0" for r in result["audit_oracles"]))
        assert result["audit_accepted"] or args.report_rejected_quality, "Independent geometry audit failed"
    args.output.with_suffix(".json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    lines = ["# Release real-window GI budget comparison", "",
             "Loop includes rendering, submission, GPU wait, event polling and present; diagnostic readbacks and log output are excluded.", "",
             "| Profile | Trajectory | Loop ms | GPU ms | FPS | Loop P95 ms | 90 FPS mean/P95 |", "|---|---|---:|---:|---:|---:|---|"]
    for r in result["trajectories"]:
        lines.append(f"| {r['profile']} | {r['path']} | {r['loop']['mean']:.3f} | {r['gpu']['mean']:.3f} | {r['fps']:.2f} | {r['loop']['p95']:.3f} | {r['meets_90_mean_and_p95']} |")
    lines += ["", "Per-stage executed counts and amortized costs, actual workload counters and diagnostics are in the accompanying JSON.",
              "acceleration_build_nested is already inside its first native scene consuming pass and the GPU frame; do not add it to either total. CPU AS preparation/packing and fence waits are also overlapping intervals, not additive frame costs.",
              "Legacy graphics_* field names are retained: since trial 51, descriptor allocations and uniform snapshot copies include graphics plus frame-compute work, while graphics_draw_calls counts graphics draws only. AS maintenance counts are not ray-query counts.",
              "Detailed contact_risk/contact_mask timings retain their original/native nesting marker in JSON and must not be blindly added to contact_trace.",
              "These are fixed trajectories with two epochs per mode; this run does not establish cross-device performance or a confidence interval."]
    if result["quality"] is not None:
        lines += ["", f"Quality accepted: **{result['quality']['accepted']}**."]
    args.output.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
