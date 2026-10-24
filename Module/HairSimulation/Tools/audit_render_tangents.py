#!/usr/bin/env python3
"""Read-only CPU audit of guide tangents against actual Diana render curves.

This does not start a game, GPU device, simulator or build. NumPy is the sole
non-stdlib dependency. The output distinguishes static source evidence from
the host paused/rest comparison; it is not a runtime-game tangent capture.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def audit(hairbin):
    raw = hairbin.read_bytes()
    header = struct.unpack_from("<8s6I6f", raw)
    magic, version, guide_count, node_count, strand_count, vertex_count, segment_count, *_ = header
    if magic != b"DHAIR001" or version != 1:
        raise ValueError("Expected DHAIR001 v1")
    cursor = 56

    def section(dtype, shape):
        nonlocal cursor
        value = np.frombuffer(raw, dtype=dtype, count=int(np.prod(shape)), offset=cursor).reshape(shape)
        cursor += value.nbytes
        return value

    guide_offsets = section("<u4", (guide_count + 1,))
    guides = section("<f4", (node_count, 4))[:, :3].astype(np.float64)
    section("<f4", (node_count, 4))  # rest quaternions
    offsets = section("<u4", (strand_count + 1,))
    points = section("<f4", (vertex_count, 4))[:, :3].astype(np.float64)
    binding_type = np.dtype([("nodes", "<u4", (4,)), ("weights", "<f4", (4,)), ("lerps", "<f4", (4,))])
    bindings = section(binding_type, (vertex_count,))
    section("<u4", (segment_count,))
    if cursor != len(raw) or offsets[0] != 0 or offsets[-1] != vertex_count:
        raise ValueError("Unexpected section size/topology")
    if np.any(np.diff(offsets) < 2) or guide_offsets[-1] != node_count:
        raise ValueError("Invalid strand topology")

    indices = bindings["nodes"][:, :3]
    guide_deltas = guides[indices + 1] - guides[indices]
    guide_lengths = np.linalg.norm(guide_deltas, axis=2, keepdims=True)
    if np.any(guide_lengths <= 1e-7):
        raise ValueError("Audit needs the native guide-orientation fallback for coincident guide nodes")
    guide_directions = guide_deltas / guide_lengths
    legacy = np.sum(guide_directions * bindings["weights"][:, :3, None], axis=1)
    legacy /= np.linalg.norm(legacy, axis=1, keepdims=True)

    previous = np.arange(vertex_count) - 1
    following = np.arange(vertex_count) + 1
    previous[offsets[:-1]] = offsets[:-1]
    following[offsets[1:] - 1] = offsets[1:] - 1
    incoming, outgoing = points - points[previous], points[following] - points
    incoming[offsets[:-1]] = outgoing[offsets[:-1]]
    outgoing[offsets[1:] - 1] = incoming[offsets[1:] - 1]
    a, b = np.linalg.norm(incoming, axis=1), np.linalg.norm(outgoing, axis=1)
    supported = (a > 1e-8) & (b > 1e-8)
    derivative = incoming * np.sqrt(np.maximum(b, 1e-30) / np.maximum(a, 1e-30))[:, None]
    derivative += outgoing * np.sqrt(np.maximum(a, 1e-30) / np.maximum(b, 1e-30))[:, None]
    length = np.linalg.norm(derivative, axis=1)
    supported &= length > 1e-14
    actual = derivative / np.maximum(length[:, None], 1e-30)
    degrees = np.degrees(np.arccos(np.clip(np.sum(actual * legacy, axis=1), -1, 1)))

    def statistics(mask):
        values = degrees[mask & supported]
        return {"count": int(values.size),
                "degreesQuantiles_0_50_90_95_99_100": np.percentile(values, [0, 50, 90, 95, 99, 100]).tolist(),
                "over15DegreesPercent": float(100 * np.mean(values > 15)),
                "over30DegreesPercent": float(100 * np.mean(values > 30))}

    return {"hairbin": str(hairbin), "hairbinSha256": hashlib.sha256(raw).hexdigest(),
            "counts": {"guides": guide_count, "nodes": node_count, "strands": strand_count,
                       "vertices": vertex_count, "segments": segment_count},
            "nativeUndefinedDegeneratePoints": int(np.sum(~supported)),
            "all": statistics(np.ones(vertex_count, dtype=bool)),
            "highestY10Percent": statistics(points[:, 1] >= np.percentile(points[:, 1], 90))}


def main():
    repository = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hairbin", type=Path, default=repository / "Asset/HairSimulation/diana_lod0.hairbin")
    parser.add_argument("--source", type=Path, default=Path("E:/REA/pragmata/shader/disassembly/emissive/StrandsDualIBLOpt.compute.dxil.txt"))
    parser.add_argument("--output", type=Path, default=Path("E:/REA/pragmata/hair_tangents/cpu_tangent_audit.json"))
    args = parser.parse_args()
    result = {"schema": "diana.render-tangent-audit.v1", "scope": "Offline paused/rest host input comparison; no runtime-game observation or GPU execution.",
              "source": {"path": str(args.source), "sha256": sha256(args.source),
                         "ssa": "%95..226 actual render neighbors; %142..221 centripetal derivative; %222..226 normalization",
                         "formula": "normalize(incoming*sqrt(length(outgoing)/length(incoming)) + outgoing*sqrt(length(incoming)/length(outgoing)))",
                         "normalizedEquivalent": "normalize(normalize(incoming)+normalize(outgoing)) for nonzero nonreversing secants",
                         "boundaries": "First/last flags extrapolate a missing neighbor: Pprev=2*P-Pnext or Pnext=2*P-Pprev.",
                         "hostExtension": "Coincident/reversing secants have an explicitly finite fallback; original expression is undefined there."},
              "host": {"legacy": "Normalize weighted directions of three guide intervals; guide_interpolate.comp is preserved unchanged.",
                       "new": "Separate Render::Compute pass derives directions from actual interpolated render positions and per-strand neighbor pairs.",
                       "shaderSha256": sha256(repository / "Module/HairSimulation/Shaders/render_tangents.comp"),
                       "legacyShaderSha256": sha256(repository / "Module/HairSimulation/Shaders/guide_interpolate.comp"),
                       "doesNotChange": ["render positions", "physical radius", "arc", "guide physics", "source colors"]},
              "measurements": audit(args.hairbin),
              "limits": ["CPU float64 rest-asset comparison; actual GPU interpolation has float32 rounding.",
                         "Highest-Y points are a crown candidate, not a recovered named scalp region.",
                         "This quantifies direction inputs; it does not prove the magnitude of any screenshot appearance change.",
                         "Source curve derivative is transcribed; original runtime LOD/groom pose and probe fields remain unobserved."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "outputSha256": sha256(args.output), "measurements": result["measurements"]}, indent=2))


if __name__ == "__main__":
    main()
