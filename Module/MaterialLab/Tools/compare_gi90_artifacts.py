"""Read-only comparison of existing GI90 artifact manifests and bounded float records."""
import argparse
import json
import math
from pathlib import Path

import numpy as np

FIELDS = 24
SCHEMAS = {
    "GI90 artifacts v1": (("ceiling", "upright_seam", "low_sphere_bottom"), 24),
    "GI94 artifacts v1": (("lit_seam", "top_curve"), 28),
}
LUMA = np.array([.2126, .7152, .0722], dtype=np.float64)


def read_float(manifest_path, snapshot, width, height, expected_fields=FIELDS):
    path = Path(snapshot["float_file"])
    if not path.is_file():
        path = manifest_path.parent / path.name
    header = np.fromfile(path, dtype="<u4", count=8)
    assert len(header) == 8 and list(header[:4]) == [0x31414947, 1, width, height], path
    count, fields, roi = map(int, header[4:7])
    assert fields == expected_fields and count == snapshot["float_records"] and roi == snapshot["roi_pixels"]
    assert path.stat().st_size == 32 + count * fields * 4, f"Truncated float records: {path}"
    values = np.memmap(path, dtype="<f4", mode="r", offset=32, shape=(count, fields))
    assert np.isfinite(values).all(), f"Nonfinite artifact records: {path}"
    assert ((values[:, 0] >= 0) & (values[:, 0] < width) &
            (values[:, 1] >= 0) & (values[:, 1] < height)).all()
    assert (values[:, :3] == np.floor(values[:, :3])).all() and (values[:, 2] > 0).all()
    assert (values[:, 7] == 1).all()  # exported world homogeneous lane
    assert np.array_equal(values[:, 3], values[:, 23]), "History-count copies differ"
    assert np.array_equal(values[:, 15], values[:, 19]), "Before-AO readability differs"
    return values


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("reference", type=Path)
    cli.add_argument("candidate", type=Path)
    cli.add_argument("--output", required=True, type=Path)
    args = cli.parse_args()
    a = json.loads(args.reference.read_text(encoding="utf-8-sig"))
    b = json.loads(args.candidate.read_text(encoding="utf-8-sig"))
    for item in (a, b):
        assert item["format"] in SCHEMAS and item["diagnostic_only"] is True
        assert item["acceptance_thresholds_changed"] is False
        assert item["warm_frames"] == 64 and item["static_frames"] == 8
        names, _ = SCHEMAS[item["format"]]
        assert len(item["views"]) == len(names)
        assert tuple(view["name"] for view in item["views"]) == names
        if item["format"] == "GI94 artifacts v1":
            assert item["scene"] == "sealed_gi_room" and "primary specular" in item["direct_source_semantics"]
            assert all("direct_source_png" in shot for view in item["views"] for shot in view["snapshots"])
    assert a["format"] == b["format"], "Do not compare different camera fixtures"
    _, fields = SCHEMAS[a["format"]]
    result = dict(reference=str(args.reference), candidate=str(args.candidate),
        geometry_samples_equal=True, roi_metadata_equal=True, views=[],
        notes=["No new image/noise thresholds are applied. Original quality gates remain separate.",
               "Static temporal RMS uses every selected ROI pixel over eight unchanged-pose frames.",
               "Float records are bounded samples; identical records do not prove every unsampled receiver is identical.",
               "Motion images compare the same camera, object offset and trajectory phase; motion variation is not labelled noise."])
    for old, new in zip(a["views"], b["views"]):
        assert (old["view"], old["name"]) == (new["view"], new["name"])
        same_camera = (old["view_matrix_column_major"] == new["view_matrix_column_major"] and
                       old["projection_column_major"] == new["projection_column_major"])
        ns_a, ns_b = old["static_noise"], new["static_noise"]
        for noise in (ns_a, ns_b):
            assert noise["same_pose_owner_geometry"] is True and noise["samples"] == 8
            assert noise["roi_pixels"] > 0 and 0 <= noise["readable_samples"] <= 8 * noise["roi_pixels"]
            assert all(math.isfinite(noise[key]) for key in ("temporal_rms", "mean_luminance"))
        same_roi = same_camera and ns_a["roi_pixels"] == ns_b["roi_pixels"]
        view = dict(view=old["view"], name=old["name"], camera_equal=same_camera,
            roi_metadata_equal=same_roi,
            reference_static_noise=ns_a, candidate_static_noise=ns_b,
            static_temporal_rms_delta=ns_b["temporal_rms"] - ns_a["temporal_rms"],
            static_mean_luminance_delta=ns_b["mean_luminance"] - ns_a["mean_luminance"],
            static_readable_sample_delta=ns_b["readable_samples"] - ns_a["readable_samples"],
            snapshots=[])
        assert len(old["snapshots"]) == len(new["snapshots"]) == 5
        for sa, sb in zip(old["snapshots"], new["snapshots"]):
            same_pose = all(sa[k] == sb[k] for k in ("phase", "phase_frame", "object_offset"))
            assert same_pose, "Artifact trajectory phase differs"
            same_selection = all(sa[k] == sb[k] for k in ("roi_pixels", "float_records", "float_stride"))
            # Viewport extent is encoded in each dump, independently of JSON labels.
            pa = Path(sa["float_file"])
            if not pa.is_file():
                pa = args.reference.parent / pa.name
            h = np.fromfile(pa, dtype="<u4", count=8)
            assert len(h) == 8
            va = read_float(args.reference, sa, int(h[2]), int(h[3]), fields)
            vb = read_float(args.candidate, sb, int(h[2]), int(h[3]), fields)
            same_geometry = (same_selection and va.shape == vb.shape and
                np.array_equal(va[:, :3].view("<u4"), vb[:, :3].view("<u4")) and
                np.array_equal(va[:, 4:12].view("<u4"), vb[:, 4:12].view("<u4")))
            row = dict(phase=sa["phase"], phase_frame=sa["phase_frame"],
                same_pose=True, roi_pixels=sa["roi_pixels"], float_records=sa["float_records"],
                float_stride=sa["float_stride"], sampled_geometry_bitwise_equal=same_geometry,
                complete_roi_exported=sa["roi_pixels"] == sa["float_records"],
                all_float_components_finite=True,
                reference_readable_records=int(np.count_nonzero(va[:, 15] > 0)),
                candidate_readable_records=int(np.count_nonzero(vb[:, 15] > 0)))
            if same_geometry:
                delta = vb[:, 12:15].astype(np.float64) - va[:, 12:15]
                row.update(reference_mean_luminance=float((va[:, 12:15] @ LUMA).mean()),
                    candidate_mean_luminance=float((vb[:, 12:15] @ LUMA).mean()),
                    same_pose_mean_abs_rgb_difference=float(np.abs(delta).mean()),
                    same_pose_max_abs_rgb_difference=float(np.abs(delta).max()),
                    readability_changed_records=int(np.count_nonzero((va[:, 15] > 0) != (vb[:, 15] > 0))))
            if same_geometry and fields == 28:
                direct_delta = vb[:, 24:27].astype(np.float64) - va[:, 24:27]
                row.update(reference_direct_source_mean_luminance=float((va[:, 24:27] @ LUMA).mean()),
                    candidate_direct_source_mean_luminance=float((vb[:, 24:27] @ LUMA).mean()),
                    direct_source_mean_abs_rgb_difference=float(np.abs(direct_delta).mean()),
                    direct_source_max_abs_rgb_difference=float(np.abs(direct_delta).max()))
            result["geometry_samples_equal"] &= same_geometry
            same_roi &= same_selection
            view["snapshots"].append(row)
        view["roi_metadata_equal"] = same_roi
        result["roi_metadata_equal"] &= same_roi
        result["views"].append(view)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    lines = ["# GI90 artifact comparison (diagnostic only)", "",
        "| View | Reference static RMS | New static RMS | RMS delta | Reference mean | New mean | Readable delta |",
        "|---|---:|---:|---:|---:|---:|---:|"]
    for view in result["views"]:
        old, new = view["reference_static_noise"], view["candidate_static_noise"]
        lines.append(f"| {view['name']} | {old['temporal_rms']:.8g} | {new['temporal_rms']:.8g} | "
            f"{view['static_temporal_rms_delta']:+.8g} | {old['mean_luminance']:.8g} | "
            f"{new['mean_luminance']:.8g} | {view['static_readable_sample_delta']:+d} |")
    lines += ["", f"ROI metadata equal: {result['roi_metadata_equal']}.",
        f"Exported owner/world/normal geometry bitwise equal: {result['geometry_samples_equal']}."] + result["notes"]
    args.output.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    if not result["geometry_samples_equal"] or not result["roi_metadata_equal"]:
        raise SystemExit("Artifact receiver identity differs; do not attribute the difference solely to lighting.")


if __name__ == "__main__":
    main()
