"""Compare two renderer binaries from four existing UE-only performance logs."""
import argparse
import hashlib
import json
import math
from pathlib import Path

from summarize_gi90 import records, summary

PATHS = ("static", "camera", "model", "new_exposure", "lighting")
HOST = ("prepare", "record", "command_encode", "fence", "pipeline_prepare",
        "as_prepare", "as_pack", "sdf_prepare", "cache_prepare", "geometry_snapshot",
        "geometry_transform", "geometry_refit", "geometry_metadata")
STAGES = ("screen_trace", "sdf_trace", "sdf_kernel", "sdf_pack", "reconstruction",
          "probe_spatial", "native_neighbors", "native_coefficient_filter",
          "native_base_reconstruct", "temporal", "cache_lighting", "receiver_motion",
          "acceleration_build_nested", "opaque", "placement", "placement_uniform",
          "placement_mark", "placement_spawn", "ue_radiance_cache",
          "ue_probe_importance", "receiver_capture", "cached_direct", "post_output")
COUNTERS = ("graphics_draw_calls", "graphics_descriptor_allocations",
            "graphics_uniform_snapshot_copies", "descriptor_writes",
            "descriptor_cache_hits", "uniform_snapshot_copied_bytes",
            "geometry_snapshot_copy_bytes", "geometry_snapshot_full_bytes",
            "geometry_snapshot_patch_rows", "geometry_snapshot_catchup",
            "geometry_snapshot_versions", "card_lighting_updated_texels",
            "card_lighting_written_texels", "card_lighting_updated")


def read_run(path):
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    assert "GI90 PERFORMANCE COMPLETE" in text, f"Incomplete run: {path}"
    setup = next(records(path, "GI90 SETUP"))
    assert setup["build"] == "Release_NDEBUG" and setup["present"] == "1"
    assert setup.get("performance_ue_only") == "1" and setup.get("order") == "21,21"
    assert setup.get("epochs") == "2" and int(setup["measured"]) == 32
    rows = list(records(path, "GI90 SAMPLE"))
    assert len(rows) == 320, f"Expected 5 paths x 2 epochs x 32 frames: {path}"
    for name in PATHS:
        group = [r for r in rows if r["path"] == name]
        assert len(group) == 64
        assert {(int(r["trial"]), int(r["frame"])) for r in group} == {
            (trial, frame) for trial in range(2) for frame in range(32)}
    for row in rows:
        assert row["profile"] == "21" and row["present"] == "1" and row["ue_lumen_high"] == "1"
        for name in ("loop", "gpu", "prepare", "record", "command_encode"):
            assert math.isfinite(float(row[name])) and float(row[name]) >= 0
    identity_path = path.with_name(path.stem + "_identity.json")
    assert identity_path.is_file(), f"Missing execution identity: {identity_path}"
    identity = json.loads(identity_path.read_text(encoding="utf-8-sig"))
    assert identity.get("exit_code") == 0 and identity.get("exe_unchanged") is True
    assert isinstance(identity.get("sha256"), str) and len(identity["sha256"]) == 64
    return dict(log=str(path), log_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                setup=setup, identity=identity, identity_source=str(identity_path)), rows


def aggregate(rows):
    value = {name: summary([float(r[name]) for r in rows]) for name in HOST if all(name in r for r in rows)}
    value["loop"] = summary([float(r["loop"]) for r in rows])
    value["gpu"] = summary([float(r["gpu"]) for r in rows])
    value["fps"] = 1000 / value["loop"]["mean"]
    value["meets_original_90_mean_and_p95"] = max(value["loop"]["mean"], value["loop"]["p95"]) <= 1000 / 90
    value["stages"] = {}
    for name in STAGES:
        values = [float(r[name]) for r in rows if name in r]
        value["stages"][name] = dict(executed=len(values),
            amortized_ms=sum(values) / len(rows), executed_summary=summary(values) if values else None)
    value["counters"] = {name: dict(total=sum(int(r[name]) for r in rows),
        mean=sum(int(r[name]) for r in rows) / len(rows))
        for name in COUNTERS if all(name in r for r in rows)}
    return value


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    for label in ("a1", "b1", "b2", "a2"):
        cli.add_argument(label, type=Path)
    cli.add_argument("--output", required=True, type=Path)
    cli.add_argument("--reference-label", default="Renderer59", help="Reference renderer identity label; does not alter sample or identity checks")
    args = cli.parse_args()
    runs, samples = {}, {}
    for label in ("a1", "b1", "b2", "a2"):
        runs[label], samples[label] = read_run(getattr(args, label))
    assert runs["a1"]["identity"]["sha256"] == runs["a2"]["identity"]["sha256"], "Reference binary changed"
    assert runs["b1"]["identity"]["sha256"] == runs["b2"]["identity"]["sha256"], "Candidate binary changed"
    assert len({r["setup"]["internal"] for r in runs.values()}) == 1, "Resolution differs"
    # Launch identities must document the outer A/B/B/A order.
    times = [runs[k]["identity"].get("start_utc") for k in ("a1", "b1", "b2", "a2")]
    assert all(times) and times == sorted(times), "Execution identities do not show A/B/B/A order"
    result = dict(order=["a1", "b1", "b2", "a2"], reference_label=args.reference_label, runs=runs, trajectories=[],
        notes=[f"Reference is {args.reference_label} with this same QA harness, not profile0.",
               "Two executions per binary; 128 timed frames per path and binary. These are epochs, not independent frame trials.",
               "Mean/P95 are recomputed from pooled raw samples. FPS is 1000/mean loop; P95 values and FPS are never averaged.",
               "No new regression threshold is invented. The original 90 FPS mean/P95 gate remains reported.",
               "AS timings, sdf_kernel/pack, native neighbor/filter and placement details are nested; never sum nested timings.",
               "Fence waits overlap GPU execution. Descriptor/UBO counters include graphics and frame-compute; draw counts are graphics only.",
               "Binary SHA identities and execution order are verified; renderer-source equivalence is documented by the separate freeze manifest."])
    lines = [f"# {args.reference_label} / candidate UE-only A/B/B/A", "",
             f"| Path | {args.reference_label} loop ms | New loop ms | Loop delta ms | {args.reference_label} FPS | New FPS | {args.reference_label} P95 ms | New P95 ms | New GPU ms |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for path in PATHS:
        a = aggregate([r for label in ("a1", "a2") for r in samples[label] if r["path"] == path])
        b = aggregate([r for label in ("b1", "b2") for r in samples[label] if r["path"] == path])
        delta = b["loop"]["mean"] - a["loop"]["mean"]
        result["trajectories"].append(dict(path=path, reference=a, candidate=b,
            loop_mean_delta_ms=delta, loop_mean_ratio=b["loop"]["mean"] / a["loop"]["mean"]))
        lines.append(f"| {path} | {a['loop']['mean']:.4f} | {b['loop']['mean']:.4f} | {delta:+.4f} | "
                     f"{a['fps']:.2f} | {b['fps']:.2f} | {a['loop']['p95']:.4f} | {b['loop']['p95']:.4f} | {b['gpu']['mean']:.4f} |")
    lines += [""] + result["notes"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    args.output.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
