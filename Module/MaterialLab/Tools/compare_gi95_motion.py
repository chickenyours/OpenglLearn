"""CPU-only GI95 diagnostics. This does not replace or change GI90 gates."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct

for variable in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ[variable] = "1"
import numpy as np
from PIL import Image

FORMAT = "GI95 all-object motion artifacts v1"
STAGES = {"final_gi": slice(12, 16), "before_ao": slice(16, 20),
          "history": slice(20, 24), "direct_source": slice(24, 28)}
PNG_STAGES = ("total", "indirect", "before_ao", "direct_source")
SPHERE_OWNERS = (20, 21, 24, 25)  # Serialized owners are one based.
CHECKPOINTS = (0, 31, 63, 95)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def resolve(root, value):
    path = Path(value)
    return path if path.is_absolute() else root / path


def key(shot):
    return shot["view"], shot["phase"], shot["phase_frame"]


def load_manifest(path):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    require(data["format"] == FORMAT and data["diagnostic_only"] is True and
            data["acceptance_thresholds_changed"] is False, "Unexpected GI95 manifest contract")
    require(data["moving_ids_zero_based"] == list(range(19, 25)) and
            data["static_ids_zero_based"] == [0, 18], "Unexpected movable/static ownership")
    for name, value in (("warm_frames", 64), ("static_frames", 8), ("motion_frames", 96),
                        ("settle_frames", 64), ("frozen_reference_frames", 64)):
        require(data[name] == value, "Unexpected GI95 frame schedule: " + name)
    shots = {key(s): s for s in data["snapshots"]}
    expected = {(v, "static", f) for v in range(2) for f in (0, 7)}
    expected |= {(v, p, f) for v in range(2) for p in ("all_props", "frozen_reference") for f in CHECKPOINTS}
    expected |= {(v, "paused", 63) for v in range(2)}
    require(len(data["snapshots"]) == 22 and set(shots) == expected, "Missing or duplicate GI95 snapshot")
    for s in shots.values():
        require(s["view_name"] == ("alias_front", "alias_rear")[s["view"]], "Unexpected camera name")
        require(np.asarray(s["prop_offsets"]).shape == (6, 3) and
                np.all(np.asarray(s["prop_offsets"])[:, 1] == 0), "Invalid XZ prop offsets")
        require(len(s["view_matrix_column_major"]) == len(s["projection_column_major"]) == 16,
                "Missing exact camera matrices")
        require(len(s["owner_pixels"]) == 25 and sum(s["owner_pixels"]) == s["roi_pixels"],
                "Owner histogram does not cover the whole visible floor/prop ROI")
        require(not any(s["owner_pixels"][1:19]), "Architecture entered the declared floor/prop ROI")
    return data, shots


def load_records(root, shot):
    path = resolve(root, shot["float_file"])
    raw = path.read_bytes()
    require(len(raw) >= 32, "Truncated GI95 float header: " + str(path))
    h = struct.unpack("<8I", raw[:32])
    require(h[:4] == (0x31414947, 1, 1280, 800) and h[5] == 28 and h[7] == 0,
            "Unexpected GI95 float schema: " + str(path))
    require(h[4] == shot["float_records"] <= 65536 and h[6] == shot["roi_pixels"] and
            len(raw) == 32 + h[4] * 28 * 4, "GI95 float record count/length mismatch")
    a = np.frombuffer(raw, dtype="<f4", offset=32).reshape(h[4], 28)
    require(np.isfinite(a).all(), "Nonfinite GI95 record: " + str(path))
    xy = a[:, :2]
    require(np.array_equal(xy, np.floor(xy)) and (xy >= 0).all() and
            (xy[:, 0] < 1280).all() and (xy[:, 1] < 800).all(), "Invalid pixel identity")
    ids = a[:, 2]
    require(np.array_equal(ids, np.floor(ids)) and np.isin(ids, (1, 20, 21, 22, 23, 24, 25)).all(),
            "Invalid floor/prop receiver owner")
    pixel_ids = xy[:, 1].astype(np.int64) * 1280 + xy[:, 0].astype(np.int64)
    require(len(np.unique(pixel_ids)) == len(pixel_ids), "Duplicate sampled pixel")
    require((a[:, 3] >= 0).all(), "Negative history length")
    return a, dict(path=str(path), sha256=sha(path), records=len(a), width=h[2], height=h[3])


def geometry_equal(a, b):
    return a.shape == b.shape and np.array_equal(a[:, :3], b[:, :3]) and np.array_equal(a[:, 4:12], b[:, 4:12])


def error_metrics(a, b):
    delta = a.astype(np.float64) - b.astype(np.float64)
    return dict(rms=float(np.sqrt(np.mean(delta * delta))),
                nrmse=float(np.sqrt(np.sum(delta * delta) / max(float(np.sum(b.astype(np.float64) ** 2)), 1e-20))),
                mae=float(np.mean(np.abs(delta))), p99=float(np.quantile(np.abs(delta), .99)))


def per_owner_error(a, b, stage="final_gi"):
    require(geometry_equal(a, b), "Same-pose receiver geometry changed")
    start = STAGES[stage].start
    return [dict(owner=int(owner), sampled_pixels=int(np.sum(a[:, 2] == owner)),
                 **error_metrics(a[a[:, 2] == owner, start:start + 3], b[b[:, 2] == owner, start:start + 3]))
            for owner in np.unique(a[:, 2])]


def load_png(root, shot, stage):
    path = resolve(root, shot[stage + "_png"])
    with Image.open(path) as image:
        image.load()
        require(image.size == (1280, 800) and image.mode == "RGBA", "Unexpected full PNG extent/mode")
        a = np.asarray(image).copy()
    return a, dict(path=str(path), sha256=sha(path))


def high_frequency(image, records, camera):
    # Fixed by owner and current geometry, never by output color or a winning
    # region. Neighbor ownership is not exported; this is a diagnostic of the
    # identical full-PNG stencil in each binary, not an aliasing acceptance gate.
    points = records[:, 4:7].astype(np.float64)
    inverse = np.linalg.inv(np.asarray(camera).reshape(4, 4, order="F"))
    direction = inverse[:3, 3] - points
    direction /= np.linalg.norm(direction, axis=1)[:, None]
    facing = np.abs(np.sum(direction * records[:, 8:11], axis=1))
    x = records[:, 0].astype(np.int64)
    y = 799 - records[:, 1].astype(np.int64)  # Renderer bottom-up -> PNG top-down.
    common = np.isin(records[:, 2], SPHERE_OWNERS) & (facing > .25) & (x > 0) & (x < 1279) & (y > 0) & (y < 799)
    result = []
    for owner in SPHERE_OWNERS:
        mask = common & (records[:, 2] == owner)
        xx, yy = x[mask], y[mask]
        if not len(xx):
            continue
        center = image[yy, xx, :3].astype(np.float64) / 255
        neighbor = sum(image[yy + dy, xx + dx, :3].astype(np.float64)
                       for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))) / (4 * 255)
        residual = center - neighbor
        chroma = residual - np.sum(residual * (.2126, .7152, .0722), axis=1)[:, None]
        amplitude = np.linalg.norm(chroma, axis=1)
        result.append(dict(owner=owner, fixed_centers=len(xx), chroma_laplacian_rms=float(np.sqrt(np.mean(chroma ** 2))),
                           chroma_laplacian_p95=float(np.quantile(amplitude, .95))))
    return result


def log_rows(text, prefix):
    return [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in text.splitlines() if line.startswith(prefix)]


def inspect_log(path, shots):
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    complete = log_rows(text, "GI95 ARTIFACTS COMPLETE ")
    require(len(complete) == 1 and complete[0]["snapshots"] == "22" and complete[0]["png"] == "88", "GI95 artifact run incomplete")
    static = log_rows(text, "GI95 ARTIFACT STATIC ")
    require(len(static) == 2 and {int(r["view"]) for r in static} == {0, 1}, "Missing static eight-frame RMS")
    for r in static:
        require(r["samples"] == "8" and r["same_pose_owner_geometry"] == "1" and
                int(r["roi_pixels"]) == shots[int(r["view"]), "static", 0]["roi_pixels"] and
                np.isfinite(float(r["temporal_rms"])), "Invalid static-noise diagnostic")
    motion = log_rows(text, "GI95 RECEIVER_MOTION ")
    require(len(motion) == 8 and all(r["moving_owners"] == "6" and r["actual_palette_verified"] == "1" for r in motion),
            "Missing actual six-owner motion coverage")
    errors = log_rows(text, "GI95 FROZEN_REFERENCE ")
    expected = {(v, f, owner + 1) for v in range(2) for f in CHECKPOINTS
                for owner, pixels in enumerate(shots[v, "all_props", f]["owner_pixels"]) if pixels}
    keyed = {(int(r["view"]), int(r["motion_frame"]), int(r["owner"])): r for r in errors}
    require(len(errors) == len(expected) and set(keyed) == expected, "Missing full-ROI frozen-reference owner metrics")
    for (view, frame, owner), r in keyed.items():
        require(r["same_pose_owner_geometry"] == "1" and r["reference_frames"] == "64" and
                int(r["pixels"]) == shots[view, "all_props", frame]["owner_pixels"][owner - 1] and
                np.isfinite([float(r["rms"]), float(r["nrmse"])]).all(), "Invalid frozen-reference diagnostic")
    validation = [l for l in text.splitlines() if "Validation Error" in l or "VUID-" in l]
    require(not validation, "Validation errors in GI95 artifact log")
    return dict(path=str(path), sha256=sha(path), static=static, receiver_motion=motion,
                full_roi_frozen_reference=errors, validation_error_lines=validation)


def inspect_identity(root, path, expected):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    require(data["status"] == "finished" and data["exit_code"] == 0 and data["exe_unchanged"] is True,
            "GI95 executable run identity is incomplete")
    require(sha(resolve(root, data["exe"])) == data["sha256"], "GI95 executable identity no longer matches")
    if expected:
        require(data["sha256"] == expected.upper(), "GI95 executable differs from expected frozen SHA")
    require("--gi95-motion-artifacts" in data["args"], "Identity is not a GI95 artifact run")
    return data


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("reference", type=Path)
    cli.add_argument("candidate", type=Path)
    cli.add_argument("--reference-log", required=True, type=Path)
    cli.add_argument("--candidate-log", required=True, type=Path)
    cli.add_argument("--reference-identity", required=True, type=Path)
    cli.add_argument("--candidate-identity", required=True, type=Path)
    cli.add_argument("--expected-reference-sha")
    cli.add_argument("--expected-candidate-sha")
    cli.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    cli.add_argument("--output", required=True, type=Path)
    args = cli.parse_args()
    root = args.root.resolve()
    ma, sa = load_manifest(args.reference)
    mb, sb = load_manifest(args.candidate)
    for name in ma.keys() - {"snapshots"}:
        require(ma[name] == mb[name], "GI95 manifest contract differs: " + name)
    result = dict(diagnostic_only=True, acceptance_thresholds_changed=False, old_gi90_acceptance="not_evaluated_by_this_tool",
                  integrity_checked=True, quality_pass_gate=None, reference_manifest_sha256=sha(args.reference),
                  candidate_manifest_sha256=sha(args.candidate),
                  reference_identity=inspect_identity(root, args.reference_identity, args.expected_reference_sha),
                  candidate_identity=inspect_identity(root, args.candidate_identity, args.expected_candidate_sha),
                  reference_log=inspect_log(args.reference_log, sa), candidate_log=inspect_log(args.candidate_log, sb),
                  snapshots=[], motion_to_frozen_reference=[], settle_to_frozen_reference=[])
    held = ({}, {})
    for shot_key in sorted(sa):
        a, b = sa[shot_key], sb[shot_key]
        for name in ("view_name", "roi_pixels", "float_records", "float_stride", "prop_offsets", "owner_pixels",
                     "view_matrix_column_major", "projection_column_major"):
            require(a[name] == b[name], "GI95 paired current pose/camera differs: " + name)
        ra, fa = load_records(root, a)
        rb, fb = load_records(root, b)
        require(geometry_equal(ra, rb), "Paired GI95 receiver owner/world/normal geometry changed")
        row = dict(view=shot_key[0], phase=shot_key[1], phase_frame=shot_key[2], view_name=a["view_name"],
                   geometry_owner_exact=True, reference_float=fa, candidate_float=fb, stages={}, png={})
        for stage, columns in STAGES.items():
            aa, bb = ra[:, columns], rb[:, columns]
            row["stages"][stage] = dict(**error_metrics(bb[:, :3], aa[:, :3]),
                                       reference_readable=int(np.sum(aa[:, 3] > 0)), candidate_readable=int(np.sum(bb[:, 3] > 0)),
                                       readability_changed=int(np.sum((aa[:, 3] > 0) != (bb[:, 3] > 0))))
        row["history_count"] = dict(reference_mean=float(ra[:, 3].mean()), candidate_mean=float(rb[:, 3].mean()))
        for stage in PNG_STAGES:
            pa, ia = load_png(root, a, stage)
            pb, ib = load_png(root, b, stage)
            require(np.array_equal(pa[:, :, 3], pb[:, :, 3]), "GI95 displayed alpha changed")
            row["png"][stage] = dict(**error_metrics(pb[:, :, :3] / 255., pa[:, :, :3] / 255.), reference=ia, candidate=ib)
            if stage in ("total", "direct_source"):
                row["png"][stage]["fixed_geometry_chroma_high_frequency"] = dict(
                    reference=high_frequency(pa, ra, a["view_matrix_column_major"]),
                    candidate=high_frequency(pb, rb, a["view_matrix_column_major"]))
        result["snapshots"].append(row)
        if shot_key[1] in ("all_props", "frozen_reference", "paused"):
            for storage, records in zip(held, (ra, rb)):
                storage[shot_key] = records.copy()
    for side, storage in zip(("reference", "candidate"), held):
        for view in range(2):
            for frame in CHECKPOINTS:
                a, b = storage[view, "all_props", frame], storage[view, "frozen_reference", frame]
                result["motion_to_frozen_reference"].append(dict(binary=side, view=view, motion_frame=frame,
                                                                  sampled_owners=per_owner_error(a, b)))
            for phase, frame in (("all_props", 95), ("paused", 63)):
                a, b = storage[view, phase, frame], storage[view, "frozen_reference", 95]
                result["settle_to_frozen_reference"].append(dict(binary=side, view=view, phase=phase,
                                                                  frozen_pose_motion_frame=95, sampled_owners=per_owner_error(a, b)))
    result["high_frequency_scope"] = ("Fixed sphere-owner record centers with abs(normal dot view)>0.25 and interior image coordinates; "
                                      "identical 4-neighbor full-PNG chroma Laplacian stencil in both binaries. Neighbor ownership is not exported. "
                                      "Display encoding, physical light boundaries, and specular detail can affect this diagnostic. No alias-removal pass claim.")
    result["motion_scope"] = "Full-ROI log errors and sampled linear-GI errors use each binary's separately converged 64-frame same-pose reference; moving pixel changes are not called temporal noise."
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(dict(diagnostic_only=True, integrity_checked=True, snapshots=len(result["snapshots"]),
                          full_png_pairs=len(result["snapshots"]) * 4, geometry_owner_exact=True,
                          old_gi90_acceptance="not_evaluated_by_this_tool", output=str(args.output))))


if __name__ == "__main__":
    main()
