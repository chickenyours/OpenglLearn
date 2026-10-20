"""Extract an exact, standalone v1 preview package without loading its payload.

Only selected mesh/skin/texture ranges are read (64 KiB maximum per read).
The skeleton is retained verbatim. Morph deltas and animation morph references
are filtered/remapped. No image or vertex data is decoded or quantized.
Run the native skeletal_asset_import --inspect on the result before delivery.
"""
from __future__ import annotations

import argparse
import copy
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import struct
import sys
import uuid

CHUNK = 64 * 1024
MAX_BINARY = 2 * 1024**3
MAX_JSON = 64 * 1024**2
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1
MAGIC = b"SKASSET1"
FIELDS = {"vertices": 60, "indices": 4, "skin": 69,
          "sourceVertexIds": 4, "fullInfluenceOffsets": 4, "fullInfluences": 8}


def need(ok, message):
    if not ok:
        raise ValueError(message)


def integer(value, minimum=0, maximum=MAX_BINARY):
    need(type(value) is int and minimum <= value <= maximum, "Invalid bounded integer")
    return value


def text(value, empty=False):
    need(isinstance(value, str) and (empty or value) and len(value.encode("utf-8")) <= 16384
         and "\0" not in value, "Invalid/oversized string")
    return value


def unique_pairs(pairs):
    out = {}
    for key, value in pairs:
        need(key not in out, f"Duplicate JSON key: {key}")
        out[key] = value
    return out


def regular(path, directory=False):
    path = Path(os.path.abspath(path))
    for part in [*reversed(path.parents), path]:
        info = part.lstat()
        need(not stat.S_ISLNK(info.st_mode) and not getattr(info, "st_file_attributes", 0) & 0x400,
             f"Symlink/reparse-point path is not permitted: {part}")
    need(path.is_dir() if directory else path.is_file(), f"Not a regular {'directory' if directory else 'file'}: {path}")
    return path


def child(root, reference):
    value = text(reference).replace("\\", "/")
    parts = PurePosixPath(value).parts
    need(parts and not value.startswith("/") and all(p not in ("", ".", "..") and ":" not in p
         and not p.endswith((".", " ")) and not any(c in p for c in '<>"|?*') for p in parts),
         f"Unsafe package reference: {value}")
    return regular(Path(root).joinpath(*parts))


def read_json(path, limit=MAX_JSON):
    path = regular(path)
    need(0 < path.stat().st_size <= limit, f"Oversized/empty JSON: {path}")
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique_pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"Nonfinite JSON: {value}")))


def header(document, kind):
    need(isinstance(document, dict) and document.get("assetType") == kind
         and type(document.get("version")) is int and document["version"] == 1,
         f"Expected {kind} format version 1")


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        stream.write("\n")


def fnv_append(state, data):
    for byte in data:
        state = ((state ^ byte) * FNV_PRIME) & MASK64
    return state


class Writer:
    def __init__(self, path):
        self.stream = path.open("xb", buffering=CHUNK)
        self.size = 0
        self.hash = FNV_OFFSET
        self.next_progress = 64 * 1024**2

    def write(self, data):
        need(len(data) <= MAX_BINARY - self.size, "Subset payload exceeds unchanged 2 GiB format limit")
        self.hash = fnv_append(self.hash, data)
        need(self.stream.write(data) == len(data), "Short subset payload write")
        self.size += len(data)
        if self.size >= self.next_progress:
            print(f"Subset payload copied/hashed: {self.size / 1024**2:.0f} MiB", file=sys.stderr, flush=True)
            self.next_progress += 64 * 1024**2

    def close(self):
        self.stream.flush()
        os.fsync(self.stream.fileno())
        self.stream.close()


def binary_range(value, stride, size, limit=100000000):
    need(isinstance(value, dict) and set(value) == {"offset", "count", "stride"}, "Invalid binary range object")
    offset = integer(value["offset"], 8, size)
    count = integer(value["count"], 0, limit)
    need(type(value["stride"]) is int and value["stride"] == stride and count <= (size - offset) // stride,
         "Binary range stride/bounds mismatch")
    return offset, count, count * stride


def all_ranges(mesh, size):
    spans = []
    for item in mesh["meshes"]:
        for name, stride in FIELDS.items():
            if name in item:
                offset, count, length = binary_range(item[name], stride, size)
                if count:
                    spans.append((offset, offset + length))
        for name in ("vertices", "indices", "skin", "sourceVertexIds"):
            need(name in item, f"Required mesh range missing: {name}")
        vertices = item["vertices"]["count"]
        need(vertices > 0 and item["indices"]["count"] > 0 and item["indices"]["count"] % 3 == 0
             and item["skin"]["count"] == vertices and item["sourceVertexIds"]["count"] in (0, vertices), "Invalid source mesh counts")
        need(("fullInfluenceOffsets" in item) == ("fullInfluences" in item), "Full skin ranges must be paired")
        if "fullInfluenceOffsets" in item:
            need(item["fullInfluenceOffsets"]["count"] == vertices + 1 and item["fullInfluences"]["count"] > 0, "Invalid complete skin counts")
    for texture in mesh["textures"]:
        width = integer(texture["width"], 1, 32768)
        height = integer(texture["height"], 1, 32768)
        need(texture["format"] in ("RGBA8", "RGBA32F"), "Unsupported texture precision")
        offset, count, length = binary_range(texture["pixels"], 1 if texture["format"] == "RGBA8" else 4, size, width * height * 4)
        need(count == width * height * 4, "Texture dimensions disagree with payload")
        spans.append((offset, offset + length))
    for morph in mesh["morphs"]:
        offset, count, length = binary_range(morph["deltas"], 44, size)
        if count:
            spans.append((offset, offset + length))
    spans.sort()
    need(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), "Overlapping source payload ranges")


def copy_range(source, writer, value, stride, size):
    offset, count, length = binary_range(value, stride, size, MAX_BINARY if stride in (1, 4) else 100000000)
    out = {"offset": writer.size, "count": count, "stride": stride}
    source.seek(offset)
    remaining = length
    while remaining:
        data = source.read(min(CHUNK, remaining))
        need(data, "Source payload truncated during selected-range copy")
        writer.write(data)
        remaining -= len(data)
    return out


def array(value, maximum=100000):
    need(isinstance(value, list) and len(value) <= maximum, "Invalid/oversized array")
    return value


def apply_material_overrides(materials, material_map, overrides):
    """Apply explicit preview approximations atomically; map keys are source ints.

    This helper can also patch only the derived skmesh JSON after extraction;
    no geometry/payload ID, texture bytes, or original source package changes.
    """
    plans, applied, seen = [], [], set()
    required = {"sourceMaterialIndex", "expectedName", "reason"}
    properties = {"baseColor", "roughness", "alphaMode"}
    for override in array(overrides):
        need(isinstance(override, dict) and required <= set(override)
             and set(override) <= required | properties, "Invalid/unknown material override fields")
        old = integer(override["sourceMaterialIndex"], 0, (1 << 32) - 1)
        need(old not in seen, f"Duplicate source material override: {old}")
        seen.add(old)
        need(old in material_map, f"Override references an unused source material: {old}")
        index = integer(material_map[old], 0, len(materials) - 1)
        expected = text(override["expectedName"])
        reason = text(override["reason"])
        need(materials[index].get("name") == expected, f"Material override name guard failed for source material {old}")
        updates = {}
        for field in properties & set(override):
            value = override[field]
            need(field in materials[index], f"Source material lacks expected property: {field}")
            if field == "baseColor":
                need(isinstance(value, list) and len(value) == 4, "Override baseColor must have four channels")
                need(all(type(v) in (int, float) and math.isfinite(v) and 0 <= v <= 1 for v in value),
                     "Override baseColor must be finite in [0,1]")
                updates[field] = [float(v) for v in value]
            elif field == "roughness":
                need(type(value) in (int, float) and math.isfinite(value) and 0 <= value <= 1,
                     "Override roughness must be finite in [0,1]")
                updates[field] = float(value)
            else:
                updates[field] = integer(value, 0, 2)
        need(updates, "Material override must change at least one supported property")
        applied.append({"sourceMaterialIndex": old, "subsetMaterialIndex": index,
                        "expectedName": expected, "reason": reason,
                        "before": {field: copy.deepcopy(materials[index][field]) for field in updates},
                        "after": copy.deepcopy(updates)})
        plans.append((index, updates))
    for index, updates in plans:
        materials[index].update(updates)
    return applied


def matrix(value):
    need(isinstance(value, list) and len(value) == 4 and all(isinstance(c, list) and len(c) == 4 for c in value), "Invalid transform matrix")
    need(all(type(v) in (int, float) and math.isfinite(v) for c in value for v in c), "Nonfinite transform matrix")
    return value


def subset_bounds(meshes, instances):
    points = []
    references = instances or [{"meshIndex": i, "transform": [[int(c == r) for r in range(4)] for c in range(4)]} for i in range(len(meshes))]
    for instance in references:
        item = meshes[instance["meshIndex"]]
        bounds = item["bounds"]
        need(bounds.get("valid") is True and len(bounds["minimum"]) == len(bounds["maximum"]) == 3, "Invalid local mesh bounds")
        transform = matrix(instance["transform"])
        for bits in range(8):
            p = [bounds["maximum" if bits & (1 << axis) else "minimum"][axis] for axis in range(3)] + [1]
            result = [sum(transform[c][r] * p[c] for c in range(4)) for r in range(4)]
            need(all(math.isfinite(v) for v in result) and abs(result[3]) > 1e-20, "Invalid transformed preview bounds")
            points.append([result[r] / result[3] for r in range(3)])
    need(points, "Subset has no world-space bounds")
    return {"valid": True, "minimum": [min(p[a] for p in points) for a in range(3)], "maximum": [max(p[a] for p in points) for a in range(3)]}


def publish(candidate, destination):
    need(not destination.exists(), "Output appeared before publication")
    if os.name == "nt":
        os.rename(candidate, destination)  # Windows never replaces an existing destination.
    elif sys.platform.startswith("linux"):
        libc = ctypes.CDLL(None, use_errno=True)
        need(hasattr(libc, "renameat2"), "Atomic no-replace directory publication requires renameat2")
        rename = libc.renameat2
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(candidate), -100, os.fsencode(destination), 1):
            raise OSError(ctypes.get_errno(), "Atomic no-replace subset publication failed")
    else:
        raise ValueError("Atomic no-replace publication is supported on Windows/Linux")


def create_candidate(destination):
    """Create a unique sibling with the destination parent's inherited ACL.

    Current Python tempfile.mkdtemp uses Windows mode 0700, installing a private
    protected DACL. Renaming such a directory preserves that DACL, potentially
    excluding the desktop user when a sandbox account created the package.
    Ordinary mkdir(0777) inherits the project directory's intended permissions.
    """
    destination = Path(destination)
    regular(destination.parent, directory=True)
    for _ in range(32):
        candidate = destination.parent / (destination.name + ".candidate-" + uuid.uuid4().hex)
        try:
            candidate.mkdir(mode=0o777)
        except FileExistsError:
            continue
        return regular(candidate, directory=True)
    raise FileExistsError("Cannot allocate a unique subset candidate directory")


def extract(source_package, profile_path, output, verify_source=False):
    manifest_path = Path(source_package)
    if manifest_path.is_dir():
        manifest_path /= "Package.asset.json"
    manifest_path = regular(manifest_path)
    root = manifest_path.parent
    manifest = read_json(manifest_path)
    header(manifest, "SkeletalAssetPackage")
    source_id = text(manifest["id"])
    profile = read_json(profile_path)
    header(profile, "PreviewSubsetProfile")
    need(set(profile) <= {"assetType", "version", "name", "includeObjects", "sourcePackageId", "sourcePayloadHashFNV1a64", "materialOverrides"}, "Unknown subset profile field")
    name = text(profile["name"])
    wanted = [text(v) for v in array(profile["includeObjects"])]
    need(wanted and len(set(wanted)) == len(wanted), "Object selection is empty or contains duplicate names")
    mesh_path = child(root, manifest["skeletalMesh"])
    mesh = read_json(mesh_path)
    header(mesh, "SkeletalMesh")
    need(mesh.get("byteOrder") == "little" and mesh.get("vertexStride") == 60, "Unsupported binary encoding")
    for field in ("meshes", "instances", "materials", "textures", "morphs"):
        array(mesh[field])
    payload_path = child(mesh_path.parent, mesh["payload"])
    before = payload_path.stat()
    size = integer(mesh["payloadBytes"], 8, MAX_BINARY)
    need(size == before.st_size, "Source payload byte size mismatch")
    old_hash = text(mesh["payloadHashFNV1a64"])
    need(1 <= len(old_hash) <= 16 and all(c in "0123456789abcdef" for c in old_hash), "Invalid v1 FNV hash")
    if "sourcePackageId" in profile:
        need(profile["sourcePackageId"] == source_id, "Profile targets a different source package")
    if "sourcePayloadHashFNV1a64" in profile:
        need(profile["sourcePayloadHashFNV1a64"] == old_hash, "Profile targets a different source payload hash")
    all_ranges(mesh, size)
    skeleton_path = child(root, manifest["skeleton"])
    skeleton = read_json(skeleton_path)
    header(skeleton, "Skeleton")
    need(mesh["skeletonId"] == skeleton["id"] and 0 < len(array(skeleton["bones"])) <= 100000, "Skeleton binding mismatch")
    rig_path = child(root, manifest["ikRig"])
    rig = read_json(rig_path)
    header(rig, "IKRig")
    need(rig["skeletonId"] == skeleton["id"], "IK rig skeleton mismatch")
    regions = array(manifest.get("meshRegions", []))
    by_object = {}
    mapped = set()
    for region in regions:
        index = integer(region["mesh"], 0, len(mesh["meshes"]) - 1)
        need(index not in mapped, "Duplicate source mesh region")
        mapped.add(index)
        if region.get("sourceObject"):
            by_object.setdefault(text(region["sourceObject"]), set()).add(index)
    for instance in mesh["instances"]:
        index = integer(instance["meshIndex"], 0, len(mesh["meshes"]) - 1)
        if index not in mapped:
            by_object.setdefault(text(instance["nodeName"]), set()).add(index)
    for object_name in wanted:
        need(object_name in by_object, f"Unknown source object: {object_name}")
        exported = {i["nodeName"] for i in mesh["instances"] if i["meshIndex"] in by_object[object_name]}
        need(len(exported) <= 1, f"Ambiguous source object exports multiple scene nodes: {object_name}")
        for node_name in exported:
            need(sum(b["name"] == node_name for b in skeleton["bones"]) <= 1, f"Ambiguous duplicate source node name: {node_name}")
    selected = sorted({index for item in wanted for index in by_object[item]})
    mesh_map = {old: new for new, old in enumerate(selected)}
    material_ids = sorted({integer(mesh["meshes"][i]["materialIndex"], 0, len(mesh["materials"]) - 1) for i in selected})
    material_map = {old: new for new, old in enumerate(material_ids)}
    materials = [copy.deepcopy(mesh["materials"][i]) for i in material_ids]
    material_overrides = apply_material_overrides(materials, material_map, profile.get("materialOverrides", []))
    texture_ids = set()
    for material in materials:
        need(len(array(material["textures"], 8)) == 8, "Material must have exactly eight texture slots")
        for index in material["textures"]:
            integer(index, -1, len(mesh["textures"]) - 1)
            if index >= 0:
                texture_ids.add(index)
    texture_ids = sorted(texture_ids)
    texture_map = {old: new for new, old in enumerate(texture_ids)}
    for material in materials:
        material["textures"] = [texture_map[i] if i >= 0 else -1 for i in material["textures"]]
    destination = Path(os.path.abspath(output))
    need(not destination.exists() and destination.name and not destination.is_relative_to(root), "Output must be a new directory outside the source package")
    destination.parent.mkdir(parents=True, exist_ok=True)
    regular(destination.parent, directory=True)
    candidate = create_candidate(destination)
    writer = None
    try:
        (candidate / "SkeletalMeshes").mkdir()
        writer = Writer(candidate / "SkeletalMeshes/model.skmesh.bin")
        writer.write(MAGIC)
        output_meshes, textures, morphs = [], [], []
        morph_map = {}
        with payload_path.open("rb", buffering=CHUNK) as source:
            need(source.read(8) == MAGIC, "Invalid source payload signature")
            if verify_source:
                source.seek(0)
                checksum = FNV_OFFSET
                while data := source.read(CHUNK):
                    checksum = fnv_append(checksum, data)
                need(f"{checksum:x}" == old_hash, "Source payload checksum mismatch")
            for old in selected:
                item = copy.deepcopy(mesh["meshes"][old])
                item["materialIndex"] = material_map[item["materialIndex"]]
                for field, stride in FIELDS.items():
                    if field in item:
                        item[field] = copy_range(source, writer, item[field], stride, size)
                output_meshes.append(item)
            for old in texture_ids:
                texture = copy.deepcopy(mesh["textures"][old])
                texture["pixels"] = copy_range(source, writer, texture["pixels"], 1 if texture["format"] == "RGBA8" else 4, size)
                textures.append(texture)
            for old, morph in enumerate(mesh["morphs"]):
                offset, count, length = binary_range(morph["deltas"], 44, size)
                source.seek(offset)
                start, kept = writer.size, 0
                for _ in range(count):
                    record = source.read(44)
                    need(len(record) == 44, "Truncated source morph delta")
                    old_mesh, vertex = struct.unpack_from("<II", record)
                    need(old_mesh < len(mesh["meshes"]) and vertex < mesh["meshes"][old_mesh]["vertices"]["count"], "Invalid source morph vertex reference")
                    if old_mesh in mesh_map:
                        writer.write(struct.pack("<I", mesh_map[old_mesh]) + record[4:])
                        kept += 1
                if kept:
                    item = copy.deepcopy(morph)
                    item["deltas"] = {"offset": start, "count": kept, "stride": 44}
                    morph_map[old] = len(morphs)
                    morphs.append(item)
        writer.close()
        after = payload_path.stat()
        need((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
             (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns), "Source payload changed during extraction")
        suffix = hashlib.sha256(json.dumps([source_id, old_hash, selected], separators=(",", ":")).encode()).hexdigest()[:16]
        package = copy.deepcopy(manifest)
        package.update(id=source_id + "/subset-" + suffix, name=name,
                       skeleton="Skeletons/model.skeleton.json", skeletalMesh="SkeletalMeshes/model.skmesh.json", ikRig="IKRigs/model.ikrig.json")
        package["meshRegions"] = [dict(copy.deepcopy(r), mesh=mesh_map[r["mesh"]]) for r in regions if r["mesh"] in mesh_map]
        instances = [dict(copy.deepcopy(i), meshIndex=mesh_map[i["meshIndex"]]) for i in mesh["instances"] if i["meshIndex"] in mesh_map]
        output_mesh = copy.deepcopy(mesh)
        output_mesh.update(id=mesh["id"] + "/subset-" + suffix, name=name, meshes=output_meshes,
                           instances=instances, materials=materials, textures=textures, morphs=morphs,
                           bounds=subset_bounds(output_meshes, instances), payload="model.skmesh.bin",
                           payloadBytes=writer.size, payloadHashFNV1a64=f"{writer.hash:x}")
        write_json(candidate / "Skeletons/model.skeleton.json", skeleton)
        write_json(candidate / "IKRigs/model.ikrig.json", rig)
        write_json(candidate / "SkeletalMeshes/model.skmesh.json", output_mesh)
        package["assets"] = [{"assetType": kind, "id": identity, "path": relative} for kind, identity, relative in (
            ("Skeleton", skeleton["id"], package["skeleton"]), ("SkeletalMesh", output_mesh["id"], package["skeletalMesh"]), ("IKRig", rig["id"], package["ikRig"]))]
        package["animations"] = []
        for index, reference in enumerate(array(manifest["animations"], 10000)):
            animation = read_json(child(root, reference), 256 * 1024**2)
            header(animation, "AnimationSequence")
            need(animation["skeletonId"] == skeleton["id"], "Animation skeleton mismatch")
            tracks, visited = [], set()
            for track in array(animation["morphTracks"]):
                target = integer(track["morph"], 0, len(mesh["morphs"]) - 1)
                need(target not in visited, "Duplicate source animation morph track")
                visited.add(target)
                if target in morph_map:
                    tracks.append(dict(track, morph=morph_map[target]))
            animation["morphTracks"] = tracks
            for track in array(animation["boneTracks"]):
                integer(track["bone"], 0, len(skeleton["bones"]) - 1)
            animation.pop("targetLayoutHash", None)
            animation["id"] = text(animation["id"]) + "/subset-" + suffix
            relative = f"Animations/{index}_Subset.anim.json"
            write_json(candidate / relative, animation)
            package["animations"].append(relative)
            package["assets"].append({"assetType": "AnimationSequence", "id": animation["id"], "path": relative})
        warning = f"Exact preview subset: {len(selected)}/{len(mesh['meshes'])} meshes, {len(materials)} materials, {len(textures)} referenced textures; complete original skeleton retained."
        package["warnings"] = list(package.get("warnings", [])) + [warning]
        if material_overrides:
            package["warnings"].append("Preview-only material approximations are applied; source package and binary pixels remain unchanged. This is not source-authored refraction: " +
                                       "; ".join(f"{item['expectedName']}: {item['reason']}" for item in material_overrides))
        if not verify_source:
            package["warnings"].append("Subset extraction read selected ranges only; original full-payload FNV was not recomputed. Validate the derived package with the native inspector.")
        write_json(candidate / "Package.asset.json", package)
        all_ranges(output_mesh, writer.size)
        report = {"assetType": "PreviewSubsetReport", "version": 1, "sourcePackageId": source_id,
                  "sourcePayloadHashFNV1a64": old_hash, "sourceFullPayloadChecksumVerified": verify_source,
                  "includedObjects": wanted, "meshRemap": mesh_map, "materialRemap": material_map, "textureRemap": texture_map,
                  "morphRemap": morph_map, "sourcePayloadBytes": size, "payloadBytes": writer.size,
                  "materialOverrides": material_overrides,
                  "payloadHashFNV1a64": f"{writer.hash:x}", "skeletonBoneCount": len(skeleton["bones"]),
                  "vertices": sum(i["vertices"]["count"] for i in output_meshes),
                  "triangles": sum(i["indices"]["count"] // 3 for i in output_meshes),
                  "textures": len(textures), "boundsMethod": "conservative selected local-AABB corners transformed by unchanged instance matrices",
                  "requiresNativeInspectorValidation": True}
        write_json(candidate / "Subset.report.json", report)
        publish(candidate, destination)
        candidate = None
        return report
    finally:
        if writer and not writer.stream.closed:
            writer.stream.close()
        if candidate is not None:
            need(candidate.parent == destination.parent and candidate.name.startswith(destination.name + ".candidate-"), "Unsafe candidate cleanup path")
            regular(candidate, directory=True)
            shutil.rmtree(candidate)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--verify-source", action="store_true", help="Optional full original payload FNV scan; otherwise only selected ranges are read")
    args = parser.parse_args()
    report = extract(args.package, args.profile, args.output, args.verify_source)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(f'Next: skeletal_asset_import --inspect "{args.output / "Package.asset.json"}"')


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f"Preview subset extraction failed: {error}", file=sys.stderr)
        sys.exit(1)
