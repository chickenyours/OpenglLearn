"""Small synthetic v1 packages only; never reads a real character payload."""
import copy
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("preview_subset", Path(__file__).parents[1] / "Tools/extract_preview_subset.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)

IDENTITY = [[int(c == r) for r in range(4)] for c in range(4)]


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def fixture(root):
    payload = bytearray(tool.MAGIC)

    def add(data, stride):
        value = {"offset": len(payload), "count": len(data) // stride, "stride": stride}
        payload.extend(data)
        return value

    meshes, originals = [], {}
    for index, name in enumerate(("Body", "Face", "Hair")):
        vertices = b"".join(struct.pack("<3d9f", index + v + .123456789012345, v / 3, .5,
                                        0, 1, 0, 0, 1, 1, 0, 0, 1) for v in range(3))
        skin = struct.pack("<4I4fB9f", index, 0, 0, 0, 1, 0, 0, 0, 0, *([0.] * 9)) * 3
        item = {"name": name, "materialIndex": index,
                "bounds": {"valid": True, "minimum": [index, 0, 0], "maximum": [index + 2, 1, 1]},
                "vertices": add(vertices, 60), "indices": add(struct.pack("<3I", 0, 1, 2), 4),
                "skin": add(skin, 69), "bindTransform": IDENTITY, "boneFromMesh": [IDENTITY] * 3}
        if index == 2:
            item["fullInfluenceOffsets"] = add(struct.pack("<4I", 0, 1, 2, 3), 4)
            item["fullInfluences"] = add(struct.pack("<If", 2, 1) * 3, 8)
        item["sourceVertexIds"] = add(struct.pack("<3I", 5, 6, 7), 4)
        meshes.append(item)
        originals[name] = {"vertices": vertices, "skin": skin}
    textures = []
    for index, (width, height, data, fmt) in enumerate((
        (1, 1, bytes([11, 22, 33, 255]), "RGBA8"),
        (256, 256, bytes([34, 56, 78, 255]) * (256 * 256), "RGBA8"),
        (2, 1, struct.pack("<8f", 1.234567, 2.5, .125, .3, 2, 1, .9, .8), "RGBA32F"))):
        textures.append({"name": f"Texture{index}", "width": width, "height": height,
                         "format": fmt, "hasAlpha": index == 2, "linearColor": index == 2,
                         "pixels": add(data, 1 if fmt == "RGBA8" else 4)})
    morph_data = [struct.pack("<II3d3f", 0, 0, .1, .2, .3, 0, 0, 0),
                  struct.pack("<II3d3f", 1, 0, 1.123456789012345, .2, .3, 0, 0, 0) +
                  struct.pack("<II3d3f", 2, 1, .4, .5, .6, 0, 0, 0)]
    morphs = [{"name": f"Morph{i}", "englishName": "", "panel": 0, "sourceType": 1,
               "deltas": add(data, 44)} for i, data in enumerate(morph_data)]
    materials = [{"name": f"Material{i}", "textures": [i, -1, -1, -1, -1, -1, -1, -1],
                  "baseColor": [1, 1, 1, 1], "emissive": [0, 0, 0], "uvTransform": [1, 1, 0, 0],
                  "metallic": 0, "roughness": .3, "ao": 1, "normalScale": 1,
                  "alphaMode": 2 if i == 2 else 0, "alphaCutoff": .5, "twoSided": False} for i in range(3)]
    mesh = {"assetType": "SkeletalMesh", "version": 1, "id": "mesh-source", "name": "Source",
            "skeletonId": "skeleton-source", "byteOrder": "little", "vertexStride": 60,
            "payload": "model.skmesh.bin", "payloadBytes": len(payload),
            "payloadHashFNV1a64": f"{tool.fnv_append(tool.FNV_OFFSET, payload):x}",
            "meshes": meshes, "textures": textures, "materials": materials, "morphs": morphs,
            "instances": [{"nodeName": m["name"], "meshIndex": i, "transform": IDENTITY} for i, m in enumerate(meshes)],
            "bounds": {"valid": True, "minimum": [0, 0, 0], "maximum": [100, 100, 100]}, "warnings": []}
    bone = {"englishName": "", "parent": -1, "restLocal": {"translation": [0, 0, 0], "scale": [1, 1, 1], "rotation": [0, 0, 0, 1]},
            "inverseBind": IDENTITY, "sourceFlags": 0, "fixedAxis": [0, 0, 0], "localAxisX": [1, 0, 0], "localAxisZ": [0, 0, 1],
            "inheritParent": -1, "inheritWeight": 0}
    skeleton = {"assetType": "Skeleton", "version": 1, "id": "skeleton-source", "name": "Source skeleton",
                "bones": [dict(bone, name=name) for name in ("Body", "Face", "Hair")]}
    rig = {"assetType": "IKRig", "version": 1, "id": "rig-source", "name": "Rig", "skeletonId": "skeleton-source",
           "chains": [], "humanoid": [], "diagnostics": []}
    animation = {"assetType": "AnimationSequence", "version": 1, "id": "anim-source", "name": "Expression",
                 "skeletonId": "skeleton-source", "source": "synthetic", "generated": False,
                 "duration": 1, "framesPerSecond": 30, "boneTracks": [], "diagnostics": [],
                 "morphTracks": [{"morph": i, "keys": [{"time": 0, "value": 1}]} for i in (0, 1)]}
    region = {"kind": "body", "confidence": 1, "authoredCloth": False, "requiresReview": False, "userOverride": True,
              "sourceMesh": "", "sourceMaterial": "", "clothPinGroup": "", "collections": [], "vertexGroups": [], "modifiers": [], "evidence": []}
    package = {"assetType": "SkeletalAssetPackage", "version": 1, "id": "package-source", "name": "Source", "sourcePath": "source.fbx", "sourceFormat": "fbx",
               "sourceRigidBodies": 0, "sourceJoints": 0, "warnings": [], "skeleton": "Skeletons/model.skeleton.json",
               "skeletalMesh": "SkeletalMeshes/model.skmesh.json", "ikRig": "IKRigs/model.ikrig.json", "animations": ["Animations/source.anim.json"],
               "assets": [], "meshRegions": [dict(region, mesh=i, sourceObject=name) for i, name in enumerate(("Body", "Face", "Hair"))]}
    write(root / package["skeleton"], skeleton)
    write(root / package["skeletalMesh"], mesh)
    (root / "SkeletalMeshes/model.skmesh.bin").write_bytes(payload)
    write(root / package["ikRig"], rig)
    write(root / package["animations"][0], animation)
    write(root / "Package.asset.json", package)
    profile = {"assetType": "PreviewSubsetProfile", "version": 1, "name": "HeadHair",
               "sourcePackageId": "package-source", "sourcePayloadHashFNV1a64": mesh["payloadHashFNV1a64"],
               "includeObjects": ["Face", "Hair"]}
    return package, mesh, profile, originals, morph_data


class SubsetTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="preview-subset-test-")
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.package, self.mesh, self.profile, self.originals, self.morphs = fixture(self.source)
        self.profile_path = self.root / "profile.json"
        write(self.profile_path, self.profile)

    def tearDown(self):
        self.temp.cleanup()

    def run_extract(self, **kwargs):
        return tool.extract(self.source, self.profile_path, self.root / "output", **kwargs)

    def test_exact_subset_and_hashes(self):
        original_hash = tool.hashlib.sha256((self.source / "SkeletalMeshes/model.skmesh.bin").read_bytes()).hexdigest()
        report = self.run_extract(verify_source=True)
        package = tool.read_json(self.root / "output/Package.asset.json")
        mesh = tool.read_json(self.root / "output/SkeletalMeshes/model.skmesh.json")
        binary = (self.root / "output/SkeletalMeshes/model.skmesh.bin").read_bytes()
        self.assertEqual(binary[:8], tool.MAGIC)
        self.assertEqual(mesh["payloadHashFNV1a64"], f"{tool.fnv_append(tool.FNV_OFFSET, binary):x}")
        self.assertEqual(mesh["payloadBytes"], len(binary))
        self.assertTrue(report["sourceFullPayloadChecksumVerified"])
        self.assertEqual([m["name"] for m in mesh["meshes"]], ["Face", "Hair"])
        self.assertEqual([m["materialIndex"] for m in mesh["meshes"]], [0, 1])
        self.assertEqual([m["textures"][0] for m in mesh["materials"]], [0, 1])
        self.assertEqual([t["name"] for t in mesh["textures"]], ["Texture1", "Texture2"])
        self.assertEqual(mesh["textures"][1]["format"], "RGBA32F")
        for m in mesh["meshes"]:
            for field in ("vertices", "skin"):
                r = m[field]
                self.assertEqual(binary[r["offset"]:r["offset"] + r["count"] * r["stride"]], self.originals[m["name"]][field])
        self.assertEqual(mesh["meshes"][1]["fullInfluenceOffsets"]["count"], 4)
        self.assertEqual([r["mesh"] for r in package["meshRegions"]], [0, 1])
        self.assertEqual([i["meshIndex"] for i in mesh["instances"]], [0, 1])
        self.assertEqual(len(mesh["morphs"]), 1)
        r = mesh["morphs"][0]["deltas"]
        delta = binary[r["offset"]:r["offset"] + r["count"] * 44]
        self.assertEqual(struct.unpack_from("<I", delta)[0], 0)
        self.assertEqual(struct.unpack_from("<I", delta, 44)[0], 1)
        self.assertEqual(delta[4:44], self.morphs[1][4:44])
        self.assertEqual(delta[48:], self.morphs[1][48:])
        animation = tool.read_json(self.root / "output" / package["animations"][0])
        self.assertEqual([t["morph"] for t in animation["morphTracks"]], [0])
        self.assertEqual(tool.read_json(self.root / "output" / package["skeleton"]), tool.read_json(self.source / self.package["skeleton"]))
        self.assertLess(mesh["bounds"]["maximum"][0], 100)
        self.assertEqual(original_hash, tool.hashlib.sha256((self.source / "SkeletalMeshes/model.skmesh.bin").read_bytes()).hexdigest())

    def test_selected_ranges_mode_and_existing_output(self):
        report = self.run_extract()
        self.assertFalse(report["sourceFullPayloadChecksumVerified"])
        with self.assertRaises(ValueError):
            self.run_extract()
        self.assertTrue((self.root / "output/Package.asset.json").is_file())

    def test_invalid_selections_and_binding(self):
        for change in ({"includeObjects": ["Face", "Face"]}, {"includeObjects": ["Unknown"]},
                       {"sourcePackageId": "wrong"}, {"sourcePayloadHashFNV1a64": "0"}, {"version": True}):
            with self.subTest(change=change):
                profile = dict(self.profile, **change)
                write(self.profile_path, profile)
                with self.assertRaises(ValueError):
                    self.run_extract()
                self.assertFalse((self.root / "output").exists())

    def test_invalid_payload_ranges(self):
        path = self.source / self.package["skeletalMesh"]
        for mutate in (lambda m: m["meshes"][1]["vertices"].update(offset=tool.MAX_BINARY),
                       lambda m: m["meshes"][1]["vertices"].update(offset=m["meshes"][0]["vertices"]["offset"]),
                       lambda m: m["meshes"][2].pop("fullInfluences")):
            changed = copy.deepcopy(self.mesh)
            mutate(changed)
            write(path, changed)
            with self.assertRaises(ValueError):
                self.run_extract()
            self.assertFalse((self.root / "output").exists())

    def test_full_checksum_and_cleanup(self):
        path = self.source / "SkeletalMeshes/model.skmesh.bin"
        with path.open("r+b") as stream:
            stream.seek(9)
            stream.write(b"\xff")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.run_extract(verify_source=True)
        self.assertFalse((self.root / "output").exists())
        self.assertFalse(list(self.root.glob("output.candidate-*")))

    def test_path_escape_and_duplicate_keys(self):
        package = dict(self.package, skeletalMesh="../outside.json")
        write(self.source / "Package.asset.json", package)
        with self.assertRaises(ValueError):
            self.run_extract()
        write(self.source / "Package.asset.json", self.package)
        self.profile_path.write_text('{"assetType":"PreviewSubsetProfile","assetType":"PreviewSubsetProfile"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.run_extract()

    def test_material_preview_override(self):
        override = {"sourceMaterialIndex": 1, "expectedName": "Material1", "reason": "Preview clear-film approximation, not refraction",
                    "baseColor": [1, 1, 1, .06], "roughness": .08, "alphaMode": 2}
        profile = dict(self.profile, materialOverrides=[override])
        write(self.profile_path, profile)
        report = self.run_extract()
        mesh = tool.read_json(self.root / "output/SkeletalMeshes/model.skmesh.json")
        self.assertEqual(mesh["materials"][0]["baseColor"], [1, 1, 1, .06])
        self.assertEqual(mesh["materials"][0]["roughness"], .08)
        self.assertEqual(mesh["materials"][0]["alphaMode"], 2)
        self.assertEqual(mesh["materials"][0]["textures"], [0, -1, -1, -1, -1, -1, -1, -1])
        self.assertEqual(report["materialOverrides"][0]["sourceMaterialIndex"], 1)
        self.assertEqual(report["materialOverrides"][0]["subsetMaterialIndex"], 0)
        self.assertEqual(report["materialOverrides"][0]["before"]["baseColor"], [1, 1, 1, 1])
        self.assertEqual(tool.read_json(self.source / self.package["skeletalMesh"])["materials"], self.mesh["materials"])
        package = tool.read_json(self.root / "output/Package.asset.json")
        self.assertTrue(any("Preview-only material" in value for value in package["warnings"]))

    def test_material_override_guard_and_atomicity(self):
        good = {"sourceMaterialIndex": 1, "expectedName": "Material1", "reason": "Explicit preview approximation", "alphaMode": 2}
        materials = copy.deepcopy(self.mesh["materials"][1:])
        original = copy.deepcopy(materials)
        invalid = [dict(good, expectedName="wrong"),
                   {k: v for k, v in good.items() if k != "expectedName"},
                   dict(good, sourceMaterialIndex=0), dict(good, unknown=1),
                   dict(good, baseColor=[1, 1, float("nan"), 1]),
                   dict(good, roughness=float("inf")), dict(good, alphaMode=True),
                   dict(good, baseColor=[1, 1, 1, 1.01])]
        for value in invalid:
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    tool.apply_material_overrides(materials, {1: 0, 2: 1}, [value])
                self.assertEqual(materials, original)
        bad_second = dict(good, sourceMaterialIndex=2, expectedName="Material2", roughness=float("nan"))
        with self.assertRaises(ValueError):
            tool.apply_material_overrides(materials, {1: 0, 2: 1}, [good, bad_second])
        self.assertEqual(materials, original)
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            tool.apply_material_overrides(materials, {1: 0, 2: 1}, [good, good])
        self.assertEqual(materials, original)

    def test_candidate_collision_never_reuses_existing_directory(self):
        first = tool.uuid.UUID(hex="1" * 32)
        second = tool.uuid.UUID(hex="2" * 32)
        destination = self.root / "preview"
        occupied = self.root / ("preview.candidate-" + first.hex)
        occupied.mkdir()
        marker = occupied / "foreign.txt"
        marker.write_text("preserve", encoding="utf-8")
        with mock.patch.object(tool.uuid, "uuid4", side_effect=[first, second]):
            candidate = tool.create_candidate(destination)
        self.assertEqual(candidate, self.root / ("preview.candidate-" + second.hex))
        self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")
        self.assertEqual(candidate.parent, destination.parent)

    @unittest.skipUnless(os.name == "nt", "Windows DACL inheritance regression")
    def test_windows_published_package_retains_inherited_dacl(self):
        # Only a 263-KiB synthetic package is published. No real cache is read.
        self.run_extract()
        advapi = ctypes.WinDLL("advapi32", use_last_error=True)
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        get_security = advapi.GetNamedSecurityInfoW
        get_security.argtypes = [ctypes.c_wchar_p, ctypes.c_int, ctypes.c_uint32,
                                 ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                 ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
        get_security.restype = ctypes.c_uint32
        get_control = advapi.GetSecurityDescriptorControl
        get_control.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint16), ctypes.POINTER(ctypes.c_uint32)]
        get_control.restype = ctypes.c_int
        kernel.LocalFree.argtypes = [ctypes.c_void_p]
        kernel.LocalFree.restype = ctypes.c_void_p
        for relative in ("output", "output/SkeletalMeshes", "output/SkeletalMeshes/model.skmesh.bin"):
            descriptor = ctypes.c_void_p()
            result = get_security(str(self.root / relative), 1, 4, None, None, None, None, ctypes.byref(descriptor))
            self.assertEqual(result, 0)
            try:
                control = ctypes.c_uint16()
                revision = ctypes.c_uint32()
                self.assertTrue(get_control(descriptor, ctypes.byref(control), ctypes.byref(revision)))
                self.assertFalse(control.value & 0x1000, f"Published asset has a private protected DACL: {relative}")
            finally:
                kernel.LocalFree(descriptor)


if __name__ == "__main__":
    unittest.main()
