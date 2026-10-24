"""Check completed frozen GI95 ABBA runs and recompute pooled frame timings."""
import argparse
import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

FLAGS = ["--gi95-motion-performance", "--render-width", "1280", "--render-height", "800",
         "--benchmark-present", "--surface-cache-dynamic-frames", "64", "--procedural", "--no-debug-ui"]
SCOPE_NAMES = ("screen_trace", "sdf_trace", "reconstruction", "probe_spatial", "contact_trace", "contact_filter",
               "temporal", "cache_lighting", "receiver_motion", "fresh_sampling", "acceleration_build_nested",
               "area_shadow", "sdf_kernel", "sdf_pack", "ue_radiance_cache", "ue_probe_importance",
               "geometry_transform", "geometry_snapshot", "geometry_refit", "as_prepare", "as_pack", "command_encode",
               "fence", "prepare", "record", "cached_direct")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def rows(text, prefix):
    return [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in text.splitlines() if line.startswith(prefix)]


def summarize(samples):
    result = dict(completed_frames=len(samples), scopes_ms={})
    for name in ("loop", "gpu", *SCOPE_NAMES):
        values = [float(sample[name]) for sample in samples if name in sample]
        if not values:
            continue
        require(all(math.isfinite(v) and v >= 0 for v in values), "Invalid timing scope: " + name)
        ordered = sorted(values)
        result["scopes_ms"][name] = dict(executed_samples=len(values), mean=statistics.fmean(values),
                                        median=statistics.median(values), p95=ordered[math.ceil(.95 * len(values)) - 1],
                                        minimum=ordered[0], maximum=ordered[-1])
    mean = result["scopes_ms"]["loop"]["mean"]
    result["completed_loop_fps"] = 1000 / mean
    limit = 1000 / 90
    result["existing_90fps_target_report"] = dict(threshold_ms=limit, mean_loop_ms=mean,
                                                  p95_loop_ms=result["scopes_ms"]["loop"]["p95"],
                                                  mean_meets_target=mean <= limit,
                                                  p95_meets_target=result["scopes_ms"]["loop"]["p95"] <= limit,
                                                  new_acceptance_gate=False)
    return result


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("abba", type=Path)
    cli.add_argument("--output", required=True, type=Path)
    args = cli.parse_args()
    record = json.loads(args.abba.read_text(encoding="utf-8"))
    require(record["format"] == "GI95 all-prop ABBA v1" and record["status"] == "finished", "Incomplete GI95 ABBA record")
    require(record["flags"] == FLAGS and record["readbacks_in_timing"] == 0, "GI95 timing flags changed")
    require(len(record["jobs"]) == 4 and [j["order"] for j in record["jobs"]] == ["A1", "B1", "B2", "A2"], "ABBA order changed")
    require([j["trial"] for j in record["jobs"]] == [record["reference"], record["candidate"], record["candidate"], record["reference"]],
            "ABBA binary assignment changed")
    for trial, frozen in record["frozen"].items():
        manifest = json.loads(Path(frozen["manifest"]).read_text(encoding="utf-8"))
        require(frozen["sha256"] == manifest["exe_sha256"] == sha(Path(frozen["exe"])), "Frozen binary SHA mismatch: " + trial)
    pooled = {record["reference"]: [], record["candidate"]: []}
    result = dict(format="GI95 all-prop ABBA comparison v1", abba_record=str(args.abba), abba_record_sha256=sha(args.abba),
                  integrity_checked=True, quality_gates="not_evaluated_by_timing_tool", runs=[], binaries={},
                  timing_scopes_nonadditive=True,
                  timing_scope_note="GPU and host scopes include nested intervals. Do not add them. Missing scopes are omitted, never inserted as zero.",
                  fps_formula="1000 / mean(completed frame loop milliseconds); never a mean of per-frame or per-run FPS",
                  p95_formula="Nearest rank: sorted completed frame milliseconds at ceil(0.95*N)-1",
                  physical_scanout_fps_claim=False)
    previous_end = None
    for job in record["jobs"]:
        require(job["status"] == "finished" and job["exit_code"] == 0, "ABBA job failed")
        identity_path, log_path = Path(job["identity"]), Path(job["log"])
        identity = json.loads(identity_path.read_text(encoding="utf-8"))
        require(identity["trial"] == job["trial"] and identity["args"] == FLAGS and identity["status"] == "finished" and
                identity["exit_code"] == 0 and identity["exe_unchanged"] is True, "Run identity or flags changed")
        require(identity["sha256"] == record["frozen"][job["trial"]]["sha256"] == sha(Path(identity["exe"])), "Run executable SHA mismatch")
        start = datetime.datetime.fromisoformat(identity["start_utc"])
        end = datetime.datetime.fromisoformat(identity["end_utc"])
        require(start <= end and (previous_end is None or previous_end <= start), "ABBA jobs overlap or ran out of order")
        previous_end = end
        text = log_path.read_text(encoding="utf-8-sig", errors="replace")
        require(not any("Validation Error" in l or "VUID-" in l or l.startswith("MaterialLab failed:") for l in text.splitlines()),
                "Renderer or validation error in completed timing run")
        setup = rows(text, "GI95 SETUP ")
        require(len(setup) == 1 and setup[0]["build"] == "Release_NDEBUG" and setup[0]["internal"] == "1280x800" and
                setup[0]["epochs"] == "2" and setup[0]["measured"] == "64" and setup[0]["present"] == "1" and
                setup[0]["moving_ids"] == "19,20,21,22,23,24" and setup[0]["static_ids"] == "0..18" and
                setup[0]["motion_continuous_across_epochs"] == "1" and setup[0]["readbacks_in_timing"] == "0",
                "GI95 timing setup does not describe the declared all-prop workload")
        samples = rows(text, "GI95 SAMPLE ")
        require(len(samples) == 128, "Expected 2 epochs x 64 completed samples")
        for index, s in enumerate(samples):
            require(s["profile"] == "21" and s["path"] == "all_props" and int(s["epoch"]) == index // 64 and
                    int(s["frame"]) == index % 64 and int(s["motion_frame"]) == 16 + index and
                    s["moving_props"] == "6" and s["static_objects"] == "19" and s["pose_verified"] == "1" and
                    s["present"] == "1" and s["ue_lumen_high"] == "1", "GI95 omitted, duplicated or altered an all-prop frame")
            require(all(math.isfinite(float(s[n])) and float(s[n]) > 0 for n in ("loop", "gpu")), "Invalid frame timing")
        complete = rows(text, "GI95 PERFORMANCE COMPLETE ")
        require(len(complete) == 1 and complete[0]["completed_samples"] == "128" and
                complete[0]["continuous_motion_frames"] == "144" and complete[0]["readbacks_in_timing"] == "0",
                "GI95 performance run did not complete its declared motion sequence")
        summary = summarize(samples)
        summary.update(order=job["order"], trial=job["trial"], identity=str(identity_path), identity_sha256=sha(identity_path),
                       binary_sha256=identity["sha256"], log=str(log_path), log_sha256=sha(log_path), setup=setup[0],
                       renderer_completion_summary=complete[0])
        result["runs"].append(summary)
        pooled[job["trial"]].extend(samples)
    for trial, samples in pooled.items():
        require(len(samples) == 256, "Expected exactly 256 completed frames per binary")
        result["binaries"][trial] = dict(binary_sha256=record["frozen"][trial]["sha256"], **summarize(samples))
    a, b = result["binaries"][record["reference"]], result["binaries"][record["candidate"]]
    result["comparison"] = dict(reference=record["reference"], candidate=record["candidate"],
                                mean_loop_ms_before=a["scopes_ms"]["loop"]["mean"], mean_loop_ms_after=b["scopes_ms"]["loop"]["mean"],
                                mean_loop_ms_delta=b["scopes_ms"]["loop"]["mean"] - a["scopes_ms"]["loop"]["mean"],
                                completed_loop_fps_before=a["completed_loop_fps"], completed_loop_fps_after=b["completed_loop_fps"])
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(dict(integrity_checked=True, completed_frames_per_binary=256, **result["comparison"], output=str(args.output))))


if __name__ == "__main__":
    main()
