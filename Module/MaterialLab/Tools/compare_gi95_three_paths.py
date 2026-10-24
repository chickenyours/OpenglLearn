"""CPU-only integrity and pooled timings for the fixed GI95 three-path batch."""
import argparse
import datetime
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_gi95_runs import FLAGS, require, rows, sha, summarize

JOBS = (("67_gi95", "A1"), ("72_timed", "B1"), ("73", "C1"),
        ("73", "C2"), ("72_timed", "B2"), ("67_gi95", "A2"))


def parse_run(job, frozen):
    require(job["status"] == "finished" and job["exit_code"] == 0, "Three-path timing job did not finish")
    identity_path, log_path = Path(job["identity"]), Path(job["log"])
    identity = json.loads(identity_path.read_text(encoding="utf-8"))
    require(identity["trial"] == job["trial"] and identity["args"] == FLAGS and identity["status"] == "finished" and
            identity["exit_code"] == 0 and identity["exe_unchanged"] is True, "Run identity/flags mismatch")
    require(identity["sha256"] == frozen["sha256"] == sha(Path(identity["exe"])), "Run executable SHA mismatch")
    start, end = (datetime.datetime.fromisoformat(identity[k]) for k in ("start_utc", "end_utc"))
    require(start <= end, "Run timestamps are reversed")
    text = log_path.read_text(encoding="utf-8-sig", errors="replace")
    require(not any("Validation Error" in l or "VUID-" in l or l.startswith("MaterialLab failed:") for l in text.splitlines()),
            "Renderer or validation error in timing log")
    setup = rows(text, "GI95 SETUP ")
    require(len(setup) == 1 and setup[0]["build"] == "Release_NDEBUG" and setup[0]["internal"] == "1280x800" and
            setup[0]["epochs"] == "2" and setup[0]["measured"] == "64" and setup[0]["present"] == "1" and
            setup[0]["moving_ids"] == "19,20,21,22,23,24" and setup[0]["static_ids"] == "0..18" and
            setup[0]["motion_continuous_across_epochs"] == "1" and setup[0]["readbacks_in_timing"] == "0",
            "Timing setup differs from the fixed six-prop workload")
    samples = rows(text, "GI95 SAMPLE ")
    require(len(samples) == 128, "Expected 128 completed frames per run")
    for index, sample in enumerate(samples):
        require(sample["profile"] == "21" and sample["path"] == "all_props" and
                int(sample["epoch"]) == index // 64 and int(sample["frame"]) == index % 64 and
                int(sample["motion_frame"]) == 16 + index and sample["moving_props"] == "6" and
                sample["static_objects"] == "19" and sample["pose_verified"] == "1" and
                sample["present"] == "1" and sample["ue_lumen_high"] == "1", "Missing or altered completed motion frame")
        require(all(math.isfinite(float(sample[k])) and float(sample[k]) > 0 for k in ("loop", "gpu")), "Invalid frame timing")
    completed = rows(text, "GI95 PERFORMANCE COMPLETE ")
    require(len(completed) == 1 and completed[0]["completed_samples"] == "128" and
            completed[0]["continuous_motion_frames"] == "144" and completed[0]["readbacks_in_timing"] == "0",
            "Timing log did not complete its declared motion sequence")
    summary = summarize(samples)
    summary.update(order=job["order"], trial=job["trial"], identity=str(identity_path), identity_sha256=sha(identity_path),
                   binary_sha256=identity["sha256"], log=str(log_path), log_sha256=sha(log_path), setup=setup[0],
                   renderer_completion_summary=completed[0])
    return summary, samples, start, end


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("batch", type=Path)
    cli.add_argument("--output", required=True, type=Path)
    args = cli.parse_args()
    record = json.loads(args.batch.read_text(encoding="utf-8"))
    require(record["format"] == "GI95 fixed three-path batch v1" and record["status"] == "finished", "Incomplete fixed timing batch")
    require(record["flags"] == FLAGS and record["readbacks_in_timing"] == 0 and record["parameter_search"] is False,
            "Fixed workload flags changed")
    require([(job["trial"], job["order"]) for job in record["jobs"]] == list(JOBS), "Fixed A1/B1/C1/C2/B2/A2 order changed")
    require(set(record["frozen"]) == {"67_gi95", "72_timed", "73"}, "Unexpected frozen binary set")
    for trial, frozen in record["frozen"].items():
        manifest = json.loads(Path(frozen["manifest"]).read_text(encoding="utf-8"))
        require(frozen["sha256"] == manifest["exe_sha256"] == sha(Path(frozen["exe"])), "Frozen manifest/executable SHA mismatch: " + trial)
        require(not frozen["expected_sha256"] or frozen["sha256"] == frozen["expected_sha256"].upper(), "Expected binary SHA mismatch")
    result = dict(format="GI95 fixed three-path comparison v1", integrity_checked=True, batch=str(args.batch),
                  batch_sha256=sha(args.batch), quality_gates="not_evaluated_by_timing_tool", parameter_search=False,
                  timing_scopes_nonadditive=True, physical_scanout_fps_claim=False, runs=[], binaries={},
                  fps_formula="1000 / mean(all completed frame loop milliseconds); never average FPS",
                  p95_formula="Nearest rank: sorted completed milliseconds at ceil(.95*N)-1",
                  missing_scope_policy="Omit unavailable scopes; report actual executed samples; never insert zero")
    pooled = {trial: [] for trial in record["frozen"]}
    previous_end = None
    for job in record["jobs"]:
        summary, samples, start, end = parse_run(job, record["frozen"][job["trial"]])
        require(previous_end is None or previous_end <= start, "Timing jobs overlapped or ran out of order")
        previous_end = end
        result["runs"].append(summary)
        pooled[job["trial"]].extend(samples)
    for trial, samples in pooled.items():
        require(len(samples) == 256, "Expected exactly 256 completed frames per binary")
        result["binaries"][trial] = dict(binary_sha256=record["frozen"][trial]["sha256"], **summarize(samples))
    result["three_path_summary"] = [dict(trial=trial, completed_frames=value["completed_frames"],
                                        mean_loop_ms=value["scopes_ms"]["loop"]["mean"], p95_loop_ms=value["scopes_ms"]["loop"]["p95"],
                                        completed_loop_fps=value["completed_loop_fps"], mean_gpu_ms=value["scopes_ms"]["gpu"]["mean"],
                                        p95_gpu_ms=value["scopes_ms"]["gpu"]["p95"], cached_direct=value["scopes_ms"].get("cached_direct"))
                                    for trial, value in result["binaries"].items()]
    a = result["binaries"]["72_timed"]["scopes_ms"].get("cached_direct")
    b = result["binaries"]["73"]["scopes_ms"].get("cached_direct")
    result["cached_direct_72_timed_vs_73"] = dict(available=a is not None and b is not None, full_frame_coverage=bool(
        a and b and a["executed_samples"] == b["executed_samples"] == 256), before=a, after=b,
        mean_delta_ms=None if a is None or b is None else b["mean"] - a["mean"],
        mean_ratio=None if a is None or b is None or a["mean"] <= 0 else b["mean"] / a["mean"])
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(dict(integrity_checked=True, frames_per_binary=256, three_path_summary=result["three_path_summary"],
                          cached_direct_72_timed_vs_73=result["cached_direct_72_timed_vs_73"], output=str(args.output))))


if __name__ == "__main__":
    main()
