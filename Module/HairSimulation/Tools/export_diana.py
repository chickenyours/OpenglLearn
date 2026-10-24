"""Export existing research extracts to portable HairSimulation assets.

Offline tool only. Requires Python 3.10+ and numpy. The application reads the
resulting .hairbin/.glb directly and never imports this script or game parsers.
Usage: python export_diana.py --research-root <pragmata-research-directory>

This deliberately supports only validated STRD v31 / retail Pragmata MESH
251121828 / SBD 251121870 layouts. Bounds and record spans are checked rather
than guessing offsets for other versions. Original inputs are never modified.
Mesh field definitions cross-checked against the community RE Mesh Editor:
https://github.com/TrueShadow01/REME-RE-Engine-Blender-Addon
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np

HEADER = struct.Struct("<8s6I6f")
BINDING = np.dtype([("nodes", "<u4", 4), ("weights", "<f4", 4), ("lerps", "<f4", 4)])


def span(data, offset, count, dtype):
    dtype = np.dtype(dtype)
    if offset < 0 or count < 0 or offset + count * dtype.itemsize > len(data):
        raise ValueError(f"Invalid source span {offset}+{count}*{dtype.itemsize}")
    return np.frombuffer(data, dtype=dtype, count=count, offset=offset)


def u32(data, offset):
    return int(span(data, offset, 1, "<u4")[0])


def u64(data, offset):
    return int(span(data, offset, 1, "<u8")[0])


def read_strands(data):
    if data[:4] != b"STRD" or len(data) < 244:
        raise ValueError("Not STRD v31")
    counts, vertices, indices, roots, mappings, guides, secondary = (
        span(data, offset, 4, "<u4") for offset in (4, 20, 36, 52, 68, 84, 100))
    uv_size, guide_count, secondary_count = struct.unpack_from("<3I", data, 116)
    closest, extras = span(data, 152, 4, "<u4"), span(data, 168, 5, "<u4")
    groom_size = u32(data, 212)
    # All LODs are serialized as independent complete buffer groups.
    expected = 244 + sum(map(int, vertices)) + sum(map(int, indices)) + sum(map(int, roots)) + sum(map(int, mappings)) + 6 * sum(map(int, closest)) + uv_size + sum(map(int, extras)) + groom_size
    if expected != len(data) or secondary_count or np.any(secondary):
        raise ValueError("Unsupported STRD payload or secondary guide layout")
    vc, sc = int(vertices[0]) // 16, int(roots[0]) // 4
    if vertices[0] % 16 or roots[0] % 4 or vc - sc != counts[0] or indices[0] != counts[0] * 4 or mappings[0] != vc * 4 or closest[0] != vc * 4:
        raise ValueError("Inconsistent LOD0 counts")
    off = 244
    records = span(data, off, vc * 4, "<f4").reshape(-1, 4)
    widths = span(data, off, vc * 8, "<u2").reshape(-1, 8)[:, 6].astype(np.float32) / 65535.0
    off += int(vertices[0])
    segment_words = span(data, off, int(counts[0]), "<u4")
    off += int(indices[0])
    starts = span(data, off, sc, "<u4")
    strand_offsets = np.append(starts, vc).astype("<u4")
    off += int(roots[0])
    mapping = span(data, off, vc, "<u4")
    off += int(mappings[0])
    words = span(data, off, vc * 6, "<u4").reshape(6, -1).T
    if strand_offsets[0] != 0 or np.any(np.diff(strand_offsets.astype(np.int64)) < 2) or not np.isfinite(records[:, :3]).all():
        raise ValueError("Invalid curve geometry")
    if guide_count < 1 or guide_count >= sc or starts[guide_count] != guides[0]:
        raise ValueError("Guide prefix disagrees with offsets")
    lengths = np.diff(strand_offsets)
    if not np.array_equal(mapping, np.repeat(np.arange(sc), lengths)):
        raise ValueError("Invalid strand mapping")
    segments = segment_words & 0x0fffffff
    expected_segments = np.concatenate([np.arange(a, b - 1, dtype="<u4") for a, b in zip(strand_offsets[:-1], strand_offsets[1:])])
    if not np.array_equal(segments, expected_segments):
        raise ValueError("Source segment topology is not consecutive strands")
    low, high = words & 65535, words >> 16
    ids = np.stack((low[:, 0], high[:, 0], low[:, 1]), axis=1)
    local = np.stack((high[:, 1], low[:, 2], high[:, 2]), axis=1)
    halves = np.stack((low[:, 3], high[:, 3], low[:, 4], high[:, 4], low[:, 5], high[:, 5]), axis=1).astype("<u2").view("<f2").astype("<f4")
    weights, lerps = halves[:, :3], halves[:, 3:]
    if np.any(ids >= guide_count) or np.any(local + 1 >= lengths[ids]) or not np.isfinite(halves).all() or np.any(weights < 0) or np.any(lerps < 0) or np.any(lerps > 1):
        raise ValueError("Invalid source guide interpolation")
    bindings = np.zeros(vc, BINDING)
    bindings["nodes"][:, :3] = starts[ids] + local
    bindings["weights"][:, :3] = weights / weights.sum(axis=1, keepdims=True)
    bindings["lerps"][:, :3] = lerps
    # Source fillTriangle vertex DXIL offsets each ribbon side by
    # +/-0.5*(packedLow16/65535)*hm_scaleWidth: the packed quantity is full
    # width. DHAIR001/Render positionRadius stores physical radius in metres.
    render = np.column_stack((records[:, :3], .5 * widths)).astype("<f4")
    return render, strand_offsets, segments, bindings, guide_count


def quat_multiply(a, b):
    return np.r_[a[3] * b[:3] + b[3] * a[:3] + np.cross(a[:3], b[:3]), a[3] * b[3] - np.dot(a[:3], b[:3])]


def between(a, b):
    c = float(np.dot(a, b))
    if c < -0.999999:
        axis = np.cross(a, [1, 0, 0] if abs(a[0]) < .9 else [0, 1, 0])
        axis /= np.linalg.norm(axis)
        return np.r_[axis, 0.0]
    q = np.r_[np.cross(a, b), 1.0 + c]
    return q / np.linalg.norm(q)


def guide_data(render, offsets, count):
    end = int(offsets[count])
    rest = render[:end].copy()
    frames = np.zeros((end, 4), "<f4")
    for a, b in zip(offsets[:count], offsets[1:count + 1]):
        a, b = int(a), int(b)
        delta = np.diff(rest[a:b, :3].astype(np.float64), axis=0)
        lengths = np.linalg.norm(delta, axis=1)
        if np.any(lengths < 1e-8):
            raise ValueError("Degenerate guide edge")
        tangent = np.vstack((delta / lengths[:, None], delta[-1] / lengths[-1]))
        rest[a:b, 3] = np.r_[0, np.cumsum(lengths)]
        q = between(np.array([0., 0., 1.]), tangent[0])
        frames[a] = q
        for i in range(1, b - a):
            q2 = quat_multiply(between(tangent[i - 1], tangent[i]), q)
            q2 /= np.linalg.norm(q2)
            if np.dot(q, q2) < 0:
                q2 = -q2
            frames[a + i] = q2
            q = q2
    return rest, frames


def read_head(data):
    if data[:4] != b"MESH" or u32(data, 4) != 250707828 or u32(data, 8) != len(data):
        raise ValueError("Unsupported retail Pragmata mesh")
    main, buffer, skeleton, bone_remap, names_offset = (u64(data, o) for o in (48, 88, 120, 136, 152))
    if data[main] < 1:
        raise ValueError("No head LODs")
    index_dtype = "<u4" if data[main + 6] else "<u2"
    elements, vertex_offset = u64(data, buffer), u64(data, buffer + 8)
    vertex_size = u32(data, buffer + 28)
    element_count = struct.unpack_from("<H", data, buffer + 34)[0]
    element_table = [struct.unpack_from("<HHI", data, elements + i * 8) for i in range(element_count)]
    position = next(e for e in element_table if e[0] == 0)
    position_end = min(e[2] for e in element_table if e[2] > position[2])
    if position[1] != 12 or (position_end - position[2]) % 12:
        raise ValueError("Unsupported position format")
    n = (position_end - position[2]) // 12
    positions = span(data, vertex_offset + position[2], n * 3, "<f4").reshape(-1, 3).copy()
    normal_element = next(e for e in element_table if e[0] == 1)
    normals = span(data, vertex_offset + normal_element[2], n * 8, "i1").reshape(-1, 8)[:, :3].astype(np.float32) / 127.0
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-9)
    if not np.isfinite(positions).all() or np.max(np.abs(positions)) > 100:
        raise ValueError("Invalid mesh positions")
    lod = u64(data, main + 64)
    groups = [u64(data, lod + 16 + i * 8) for i in range(data[lod])]
    faces, material_ids = [], []
    for group in groups:
        for i in range(data[group + 1]):
            sub = group + 16 + i * 32
            mat, quad, vertex_buffer_index, _ = struct.unpack_from("<4B", data, sub)
            face_count, face_start, vertex_start = struct.unpack_from("<3I", data, sub + 8)
            if quad or vertex_buffer_index or face_count % 3:
                raise ValueError("Unsupported head submesh")
            index_size = np.dtype(index_dtype).itemsize
            tri = span(data, vertex_offset + vertex_size + face_start * index_size, face_count, index_dtype).astype("<u4").reshape(-1, 3) + vertex_start
            if np.max(tri) >= len(positions):
                raise ValueError("Head triangle out of bounds")
            faces.append(tri)
            material_ids.append(mat)
    bone_count = u32(data, skeleton)
    remap = span(data, bone_remap, bone_count, "<u2")
    name_count = struct.unpack_from("<H", data, 20)[0]
    offsets = span(data, names_offset, name_count, "<u8")
    names = [data[int(o):data.index(0, int(o))].decode("utf8") for o in offsets]
    bone_names = [names[int(i)] for i in remap]
    head_index = bone_names.index("Head")
    world_matrices = u64(data, skeleton + 32)
    head_matrix = span(data, world_matrices + head_index * 64, 16, "<f4").reshape(4, 4).copy()
    return positions, normals, faces, material_ids, head_matrix


def write_head(out, positions, normals, submeshes, material_ids):
    # OBJ is deliberately plain and supports the existing Model Assimp path.
    with (out / "diana_head.obj").open("w", encoding="ascii") as file:
        file.write("# Diana actual retail head, rest pose, metres, Y up\n")
        for p in positions:
            file.write("v %.9g %.9g %.9g\n" % tuple(p))
        for p in normals:
            file.write("vn %.9g %.9g %.9g\n" % tuple(p))
        for j, faces in enumerate(submeshes):
            file.write(f"o DianaHead_{j}_Material{material_ids[j]}\n")
            for face in faces:
                a, b, c = face + 1
                file.write(f"f {a}//{a} {b}//{b} {c}//{c}\n")
    # GLB uses the same unmodified coordinates, no textures or fabricated rig.
    payload = bytearray()
    views, accessors = [], []
    def add(array, component, kind, bounds=False):
        while len(payload) % 4:
            payload.append(0)
        offset = len(payload)
        payload.extend(array.tobytes())
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": array.nbytes})
        accessor = {"bufferView": len(views) - 1, "componentType": component, "count": len(array), "type": kind}
        if bounds:
            accessor.update(min=array.min(0).tolist(), max=array.max(0).tolist())
        accessors.append(accessor)
        return len(accessors) - 1
    p = add(positions.astype("<f4"), 5126, "VEC3", True)
    n = add(normals.astype("<f4"), 5126, "VEC3")
    primitives = [{"attributes": {"POSITION": p, "NORMAL": n}, "indices": add(face.ravel().astype("<u4"), 5125, "SCALAR"), "material": 0, "mode": 4} for face in submeshes]
    doc = {"asset": {"version": "2.0", "generator": "HairSimulation portable offline exporter"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "DianaHead", "mesh": 0}], "meshes": [{"name": "Diana actual head rest mesh", "primitives": primitives}], "materials": [{"name": "Neutral preview clay", "pbrMetallicRoughness": {"baseColorFactor": [.65, .50, .43, 1], "metallicFactor": 0, "roughnessFactor": .75}, "doubleSided": True}], "buffers": [{"byteLength": len(payload)}], "bufferViews": views, "accessors": accessors}
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    payload += b"\0" * (-len(payload) % 4)
    glb = struct.pack("<4sII", b"glTF", 2, 28 + len(js) + len(payload)) + struct.pack("<I4s", len(js), b"JSON") + js + struct.pack("<I4s", len(payload), b"BIN\0") + payload
    (out / "diana_head.glb").write_bytes(glb)


def export(args):
    source = args.research_root / "extracted/natives/stm/character/ch/ch01/ch0100"
    paths = {"strands": source / "22/ch0100_22.strands.31", "gpus": source / "22/ch0100_22_default.gpus.13", "binding": source / "22/ch0100_22_strandsbindingdata.sbd.251121870", "mesh": source / "10/ch0100_10.mesh.251121828", "collision": source / "10/ch0100_10.clsp.3"}
    data = {key: path.read_bytes() for key, path in paths.items()}
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    render, offsets, segments, bindings, guide_count = read_strands(data["strands"])
    guides, frames = guide_data(render, offsets, guide_count)
    low, high = render[:, :3].min(0), render[:, :3].max(0)
    header = HEADER.pack(b"DHAIR001", 1, guide_count, len(guides), len(offsets) - 1, len(render), len(segments), *low, *high)
    parts = [header, offsets[:guide_count + 1].tobytes(), guides.tobytes(), frames.tobytes(), offsets.tobytes(), render.tobytes(), bindings.tobytes(), segments.astype("<u4").tobytes()]
    asset = b"".join(parts)
    (out / "diana_lod0.hairbin").write_bytes(asset)
    pos, normal, faces, mat, head_matrix = read_head(data["mesh"])
    write_head(out, pos, normal, faces, mat)
    # Preserve actual root binding triangles and weights as a separate compact
    # portable table (DBIND001, version,count, then uint3 triangle + float3 bary).
    bd = data["binding"]
    if bd[:4] != b"SDBD" or u32(bd, 4) != 251121870:
        raise ValueError("Unsupported SBD")
    lods, quantized, uri_size = struct.unpack_from("<HHI", bd, 8)
    sizes = span(bd, 16 + uri_size, lods, "<u4")
    binding_offset = 16 + uri_size + lods * 4
    if quantized or sizes[0] != 20 * (len(offsets) - 1) or binding_offset + sum(map(int, sizes)) != len(bd):
        raise ValueError("Unexpected SBD root binding layout")
    raw = span(bd, binding_offset, int(sizes[0]) // 4, "<u4").reshape(-1, 5)
    tri, bary = raw[:, :3] // 12, raw[:, 3:5].view("<f4")
    if np.any(raw[:, :3] % 12) or np.max(tri) >= len(pos) or not np.isfinite(bary).all():
        raise ValueError("Invalid SBD root triangles")
    records = np.zeros(len(tri), dtype=[("triangle", "<u4", 3), ("barycentric", "<f4", 3)])
    records["triangle"] = tri
    # Native pair is (triangle vertex 1 weight, triangle vertex 2 weight).
    # Verified by reconstructing all 17,500 roots against the real head mesh.
    records["barycentric"][:, 0] = 1 - bary.sum(axis=1)
    records["barycentric"][:, 1:] = bary
    root_surface_positions = (pos[tri] * records["barycentric"][:, :, None]).sum(axis=1)
    binding_residuals = np.linalg.norm(root_surface_positions - render[offsets[:-1], :3], axis=1)
    if binding_residuals.max() > .001:
        raise ValueError("Root bindings do not agree with actual head geometry")
    (out / "diana_roots.bindingbin").write_bytes(struct.pack("<8s2I", b"DBIND001", 1, len(tri)) + records.tobytes())
    # Existing research JSON is evidence, not a runtime dependency.
    parsed = json.loads((args.research_root / "formats/parsed_resources.json").read_text(encoding="utf8"))
    controller_record = next(r for r in parsed if r["path"].lower().endswith("cospa0000\\ch05000strandscontrolleruserdata.user.3"))
    controller = next(i["fields"] for i in controller_record["instances"] if i["type"] == "app.StrandsControllerUserData")
    paths["controller"] = args.research_root / "extracted/natives/stm/character/prefab/ch05000/cospa0000/ch05000strandscontrolleruserdata.user.3"
    data["controller"] = paths["controller"].read_bytes()
    collision_record = next(r for r in parsed if r["path"].lower().endswith("ch0100_10.clsp.3"))
    capsules = []
    for shape in collision_record["shapes"]:
        if shape["kind_name"] != "TAPERED_CAPSULE":
            raise ValueError("Unexpected head collider")
        v = shape["values"]
        a = (np.r_[v[:3], 1] @ head_matrix)[:3]
        b = (np.r_[v[4:7], 1] @ head_matrix)[:3]
        capsules.append({"a": a.tolist(), "radiusA": v[3], "b": b.tolist(), "radiusB": v[7], "joint": "Head", "sourceJointHash": shape["joint_hash"], "source": "CLSP v3 tapered capsule transformed with real mesh Head rest matrix"})
    preset_record = next(r for r in parsed if r["path"].lower().endswith("ch0100_22_default.gpus.13"))
    preset_record = {k: v for k, v in preset_record.items() if k != "path"}
    decoded = json.loads((args.research_root / "formats/default_diana_prefab_decoded.json").read_text(encoding="utf8"))
    # Preserve the entire decoded component/controller evidence. Absolute source
    # paths are replaced below; original resource-relative URIs remain useful.
    metadata = {"format": "DHAIR001", "version": 1, "unit": "metre", "upAxis": "Y", "asset": "diana_lod0.hairbin", "headPreview": "diana_head.glb", "counts": {"guides": guide_count, "guideNodes": len(guides), "strands": len(offsets) - 1, "renderVertices": len(render), "segments": len(segments)}, "bounds": {"min": low.tolist(), "max": high.tolist()}, "rootBounds": {"min": render[offsets[:-1], :3].min(0).tolist(), "max": render[offsets[:-1], :3].max(0).tolist()}, "head": {"vertices": len(pos), "triangles": sum(len(x) for x in faces), "boundsMin": pos.min(0).tolist(), "boundsMax": pos.max(0).tolist(), "headRestMatrixRowVector": head_matrix.tolist()}, "rootBindingValidation": {"meanDistanceMetres": float(binding_residuals.mean()), "maxDistanceMetres": float(binding_residuals.max()), "barycentricConvention": "Native pair (u,v) maps to triangle weights (1-u-v,u,v)"}, "capsules": capsules, "gravityPresets": {"normal": [0, -30, 0], "low": [0, -1.6, 0], "zero": [0, 0, 0]}, "presetGPUS": preset_record, "derivedData": {"restOrientation": "Quaternion xyzw, +Z tangent, parallel transport twist. Reconstructed from real guide geometry; original runtime frame initialization is not recovered.", "bindingWeights": "Source half-float weights normalized to sum to one in float32; original three guide-node offsets and interpolation factors retained.", "headMaterial": "Neutral clay preview; original textures and facial animation not included."}, "runtimeDependencies": ["diana_lod0.hairbin", "diana_head.glb"], "provenance": [{"kind": key, "relativeResource": path.relative_to(args.research_root / "extracted").as_posix(), "sha256": hashlib.sha256(data[key]).hexdigest(), "bytes": len(data[key])} for key, path in paths.items()], "outputSha256": hashlib.sha256(asset).hexdigest()}
    metadata["gravityPresets"] = {"normal": controller["DefaultGravity"], "low": controller["LowGravity"], "zero": controller["ZeroGravity"]}
    metadata["derivedData"]["renderRadius"] = "0.5 * source packed UNORM16 full width in metres; native fillTriangle offsets each ribbon side by half the full width."
    metadata["sourceStrandWidthsMetres"] = dict(zip(("min", "max", "average"), struct.unpack_from("<3f", data["strands"], 200)))
    (out / "diana_metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf8")
    # Keep all decoded original parameters, stripped of workstation paths.
    evidence_text = json.dumps(decoded, ensure_ascii=False, indent=2).replace(str(args.research_root).replace("\\", "\\\\"), "<research-root>")
    (out / "diana_component_evidence.json").write_text(evidence_text, encoding="utf8")
    controller_record = dict(controller_record, path=paths["controller"].relative_to(args.research_root / "extracted").as_posix())
    (out / "diana_controller_evidence.json").write_text(json.dumps(controller_record, ensure_ascii=False, indent=2), encoding="utf8")
    print(json.dumps({"output": str(out), "hairBytes": len(asset), "counts": metadata["counts"], "head": metadata["head"], "capsules": capsules}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[3] / "Asset/HairSimulation")
    export(parser.parse_args())


if __name__ == "__main__":
    main()
