"""Package the audited ST30_101 local environment for the Diana preview.

No decoding, filtering, gamma, exposure, projection roll, or material gain is
applied here: the input RGB32F has already been prepared in the host latlong
convention. The six coefficients are the last source mip's axis-center values,
not the manifest's separate host cosine-convolved E/pi integration.

ST30_101 has not been established as the user's third chapter or screenshot
location. Native LOD8 clamps to the last source mip only if runtime extra mip
generation does not occur. Dynamic spatial probes, interpolation/injection,
local OBB blending, packed exponents, and original scene exposure are still
unrecovered. The retained -7 EV is a host unit adapter, not source exposure.

Example (run from the repository root; does not overwrite Asset/Diana):
  python Module/Model/Tools/package_diana_hair_lighting.py --output output/Diana/Appearance/repackage-check --verify-existing Asset/Diana
"""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path, PureWindowsPath
import struct
import sys


SOURCE_MEMBER = "natives/stm/light/cubemap/st/st30/st30_101/lc_st30_101_00.tex.251111100"
EXPECTED_RGB_SHA256 = "4f418e4806ab7e7dccd01adb925ff17fc1f3eb477df20175b364bb69da1ba66b"
AXES = ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]
TABLE_NAME = "lc-st30-101-00.lutbin"
PROFILE_NAME = "st30-101-lighting.json"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def locate_manifest(research, name):
    for candidate in (research / "decoded_hdr" / name, research / name):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"Cannot find {name} in {research} or its decoded_hdr directory")


def finite_radiance(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and 0 <= value <= 60000


def package(research, output, verify_existing=None):
    manifest_path = locate_manifest(research, "host_environment_manifest.json")
    mip_path = locate_manifest(research, "source_cube_mip_audit.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    mip_audit = json.loads(mip_path.read_text(encoding="utf-8"))
    records = [record for record in manifest["records"] if record["sourceMember"] == SOURCE_MEMBER]
    require(len(records) == 1, "Expected exactly one audited ST30_101 LC_00 record")
    record = records[0]
    require(mip_audit["sourceTexMember"] == SOURCE_MEMBER, "Source mip audit member mismatch")
    require(record["sourceTexSha256"] == mip_audit["sourceTexSha256"], "Source TEX identities disagree")
    require(record["axisOrder"] == mip_audit["axisOrder"] == AXES, "Expected +X,-X,+Y,-Y,+Z,-Z coefficient order")
    width, height, channels = record["width"], record["height"], record["channels"]
    require((width, height, channels) == (1024, 512, 3), "Expected the audited 1024x512 RGB32F latlong")
    require(record["sourceSceneExposureApplied"] is False and record["materialIndirectMultiplierApplied"] is False,
            "Input radiance already has an exposure or material gain applied")
    require(record["projectionRollColumns"] == width // 2 and record["sourceRotationAppliedDegrees"] == 0,
            "Unexpected prepared host projection or source rotation")
    require(record["projection"] == "host BakeSkyEnvironment: u=fract(atan2(z,x)/(2pi)), v=acos(y)/pi; +Y up",
            "Input is not in the audited host latlong convention")

    # Prefer the portable adjacent file when a research directory is relocated.
    raw_name = PureWindowsPath(record["rawRgb32f"]).name
    require(raw_name == "lc_st30_101_00_host_latlong.rgb32f", "Unexpected prepared RGB file name")
    raw_path = manifest_path.parent / raw_name
    if not raw_path.is_file():
        raw_path = Path(record["rawRgb32f"])
    raw = raw_path.read_bytes()
    require(len(raw) == width * height * channels * 4, "RGB32F payload dimensions do not match the manifest")
    raw_sha = sha(raw)
    require(raw_sha == record["rawRgb32fSha256"] == EXPECTED_RGB_SHA256, "Prepared source RGB32F SHA256 mismatch")
    rgb = array("f")
    rgb.frombytes(raw)
    require(rgb.itemsize == 4, "This Python array does not provide 32-bit floats")
    if sys.byteorder != "little":
        rgb.byteswap()
    require(all(finite_radiance(value) for value in rgb), "RGB32F radiance must be finite, nonnegative and <=60000")

    mips = mip_audit["mips"]
    require(bool(mips) and [mip["sourceMip"] for mip in mips] == list(range(len(mips))),
            "Expected ordered consecutive source mips")
    require([(mip["width"], mip["height"]) for mip in mips] == [(256 >> i, 256 >> i) for i in range(5)],
            "Expected the audited five source mips, 256..16")
    require(mip_audit["nativeConsumer"]["localSampleLod"] == 8, "Native local producer LOD changed; re-audit its clamping assumption")
    last_mip = mips[-1]
    coefficients = last_mip["axisCenterRGB"]
    require(len(coefficients) == 6 and all(len(face) == 3 for face in coefficients), "Expected six RGB axis-center coefficients")
    require(all(finite_radiance(value) for face in coefficients for value in face), "Invalid source mip coefficient")
    require(mip_audit["transformInstance"]["fields"]["Rotation"] == [0, 0, 0, 1],
            "Source cubemap rotation is no longer identity")

    rgba = array("f", [0]) * (width * height * 4)
    rgba[0::4] = rgb[0::3]
    rgba[1::4] = rgb[1::3]
    rgba[2::4] = rgb[2::3]
    rgba[3::4] = array("f", [1]) * (width * height)
    if sys.byteorder != "little":
        rgba.byteswap()
    table = struct.pack("<8s4I", b"DHSLUT01", 1, width, height, 4) + rgba.tobytes()
    table_sha = sha(table)
    require(len(table) == 24 + width * height * 16, "Invalid DHSLUT01 payload size")
    profile = {
        "schema": "model.hair-lighting.v1",
        "label": "ST30_101 / LC_00 candidate",
        "environmentTable": TABLE_NAME,
        "indirectCube": coefficients,
        "producerMultiplier": 1.0,
        "hostExposureEV": -7.0,
        "rotationRadians": 0.0,
        "source": {
            "member": SOURCE_MEMBER,
            "texSha256": record["sourceTexSha256"],
            # Retain the existing profile key: this is the lossless RGB32F
            # input identity, not the separately quantized Radiance .hdr file.
            "hdrSha256": raw_sha,
            "tableSha256": table_sha,
            "cubeCoefficients": f"Original source last available mip{last_mip['sourceMip']} face-center bilinear values, +X,-X,+Y,-Y,+Z,-Z. Native producer LOD8 clamps here only if no runtime mip extension.",
            "placement": "Source local cubemap world rotation identity; OBB blending and runtime packed exponent not reconstructed.",
            "chapterMapping": "ST30_101 candidate; a direct third-chapter mapping and screenshot location have not been established.",
            "hostExposure": "-7 EV is an explicit unit adapter for the existing preview lights; original scene exposure and probe field not captured.",
            "directLights": "Existing Studio direct lights are retained to isolate the imported IBL.",
        },
    }
    # Stable UTF-8/no BOM/CRLF preserves the currently published JSON bytes
    # across platforms, in addition to preserving its parsed content.
    profile_bytes = (json.dumps(profile, indent=2) + "\n").replace("\n", "\r\n").encode("utf-8")
    if verify_existing is not None:
        require(output.resolve() != verify_existing.resolve(), "Verification output must be separate from the existing assets")
        require((verify_existing / TABLE_NAME).read_bytes() == table, "Repacked table differs from the existing asset")
        require(json.loads((verify_existing / PROFILE_NAME).read_text(encoding="utf-8")) == profile,
                "Repacked lighting profile differs semantically from the existing asset")
    output.mkdir(parents=True, exist_ok=True)
    (output / TABLE_NAME).write_bytes(table)
    (output / PROFILE_NAME).write_bytes(profile_bytes)
    result = {
        "output": str(output),
        "sourceRgb32f": str(raw_path),
        "sourceRgb32fSha256": raw_sha,
        "dimensions": [width, height, 4],
        "sourceCoefficientMip": last_mip["sourceMip"],
        "tableSha256": table_sha,
        "tableBytes": len(table),
        "profileSha256": sha(profile_bytes),
        "existingTableByteIdentical": verify_existing is not None,
        "existingProfileSemanticallyIdentical": verify_existing is not None,
        "existingProfileByteIdentical": verify_existing is not None and (verify_existing / PROFILE_NAME).read_bytes() == profile_bytes,
        "limits": [
            "ST30_101 / third-chapter and screenshot-location mapping remains unconfirmed.",
            "Native LOD8 clamps to source mip4 only if no runtime mip extension or prefiltering occurs.",
            "Dynamic spatial probes, interpolation/injection, OBB blending, packed exponents and original scene exposure remain unrecovered.",
            "Host exposure -7 EV is an explicit preview-unit adapter, not recovered source exposure.",
        ],
    }
    print(json.dumps(result, indent=2))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=Path("E:/REA/pragmata/hair_lighting"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-existing", type=Path,
                        help="Optional existing asset directory to compare before writing the separate output directory")
    args = parser.parse_args()
    package(args.research, args.output, args.verify_existing)
