"""Package audited source hair colors into portable Model/Render inputs.

Input is the retained output of the read-only source color audit. Textures are
decoded once from their original sRGB bytes before linear bilinear filtering.
No game runtime or graphics API is needed by this packaging step.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def package(research, output):
    evidence = json.loads((research / "hair-color-evidence.json").read_text(encoding="utf-8"))
    textures = json.loads((research / "gradient_texels.json").read_text(encoding="utf-8"))
    if [t["slot"] for t in textures] != ["ColorGradient_A", "ColorGradient_B", "ColorGradient_C"]:
        raise ValueError("Source gradient order must be A, B, C")
    width = textures[0]["width"]
    pixels = []
    for texture in textures:
        if texture["width"] != width or texture["height"] != 1 or texture["format"] != 29:
            raise ValueError("Expected three equal-width source RGBA8 sRGB gradients")
        raw = bytes(value for texel in texture["rgba8"] for value in texel)
        if len(raw) != width * 4 or hashlib.sha256(raw).hexdigest() != texture["texelBytesSha256"]:
            raise ValueError("Source gradient texel identity mismatch")
        for texel in texture["rgba8"]:
            linear = []
            for value in texel[:3]:
                value /= 255.
                linear.append(value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4)
            pixels.extend((*linear, texel[3] / 255.))
    table = struct.pack("<8s4I", b"DHSLUT01", 1, width, 3, 4) + struct.pack(f"<{len(pixels)}f", *pixels)
    coordinates = (research / "diana_colorcoords.bin").read_bytes()
    magic, version, vertices, strands, stride = struct.unpack_from("<8s4I", coordinates)
    if (magic, version, vertices, strands, stride) != (b"DHCOL001", 1, 436975, 17500, 2) or len(coordinates) != 24 + vertices * 2:
        raise ValueError("Unexpected source appearance topology or payload")
    source_parameters = evidence["sourceMdf"]["parameters"]
    scalar = lambda key: float(source_parameters[key][0])
    output.mkdir(parents=True, exist_ok=True)
    (output / "hair-gradients.lutbin").write_bytes(table)
    (output / "hair-colorcoords.bin").write_bytes(coordinates)
    fragment = {
        "strands": {
            "sourceColor": True,
            "gradientWeights": [scalar(key) for key in ("ColorA_Weight", "ColorB_Weight", "ColorC_Weight")],
            "colorRandomOffsets": [scalar(key) for key in ("RandomColor_Offset", "Hue_Offset", "Saturate_Offset", "Value_Offset")],
            "hsvVariation": [scalar(key) for key in ("Hue_Random", "Saturate_Random", "Value_Random")],
        },
        "strandAssets": {"gradientTable": "hair-gradients.lutbin", "colorCoordinates": "hair-colorcoords.bin"},
        "evidence": {
            "source": "hair-color-evidence.json",
            "colorCoordinatesSha256": hashlib.sha256(coordinates).hexdigest(),
            "gradientTableSha256": hashlib.sha256(table).hexdigest(),
            "colorSpace": "Original sRGB bytes decoded once before filtering; scene-linear float32 RGBA atlas",
            "sourceColorMapEnabled": bool(scalar("Use_ColorMap")),
            "sourceMaterialTransColorRate": scalar("TransColor_Rate"),
            "runtimeBabOverrideCaptured": False,
        },
    }
    (output / "hair-appearance-profile.json").write_text(json.dumps(fragment, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"vertices": vertices, "strands": strands, "gradientDimensions": [width, 3], "output": str(output)}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    package(args.research, args.output)
