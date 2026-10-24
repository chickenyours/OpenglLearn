"""Apply source-backed shading mappings to an already extracted Diana profile.

No game access or codec is needed. Keep source records intact; the optional eye
patch is produced by the focused shader/texture extraction in the REA workspace.
"""
import argparse
import json
from pathlib import Path


def upgrade(profile, eye_patch, hair_appearance=None, hair_ntt=None, hair_dslut=None):
    for entry in profile["materials"]:
        if entry["category"] != "skin":
            continue
        source = entry["source"]
        props = {p["name"]: p["values"] for p in source["properties"]}
        scalar = lambda key, default: float(props.get(key, [default])[0])
        control = next((t for t in source["textureBindings"]
                        if t["slot"] == "SSSSheenOcclusionCavityMap"), None)
        values = entry.setdefault("parameters", {})
        values.update(reSkinEnabled=True, useSkinControlMap=False,
                      useReSkinControlMap=bool(control),
                      reSkinSpecularReflectance=scalar("SpecularReflectance", 1),
                      reSkinTranslucency=scalar("Translucency", 0),
                      skinDiffusionEnabled=scalar("SSS_Channel", 0) == 3,
                      skinDiffusionBlend=scalar("SSS_BlendRate", 1),
                      skinScatterStrength=0., skinSpecularMix=0.)
        if control:
            member = ("natives/stm/" + control["path"] + ".251111100").lower()
            decoded = next(t for t in profile["sourceEvidence"]["decodedTextures"]
                           if t["member"].lower() == member)
            entry["textures"]["distortion"] = decoded["png"]
        reconstruction = entry.setdefault("reconstruction", [])
        reconstruction[:] = [s for s in reconstruction if not s.startswith("SSS_Channel=3")
                              and not s.startswith("Source-mapped RE skin")]
        reconstruction.append(
            "Source-mapped RE skin: dielectric F0=.04*SpecularReflectance*SSOC.A; "
            "SSOC.R controls diffusion blend and translucency. Channel3 enables "
            "depth/normal/material-protected spatial diffuse scattering. The "
            "former generic wrap and arbitrary second lobe are disabled; source "
            "profile kernel selection and runtime scene transport remain limited.")
    profile["strands"].update(
        physicalScattering=True, sourceDirectScattering=True, ior=1.55, longitudinalRoughness=.5,
        azimuthalRoughness=.8, absorptionScale=1., multipleScattering=1.,
        scatteringDensity=1., albedoBlendRate=.51, indirectLightingMultiplier=.8,
        spatialVolume=True, normalizedVolumeWidth=True, receiveOpaqueShadows=True,
        volumeHardwareFiltering=True, volumePreserveNearField=True, sourceSpatialIbl=False, strandMotionVectors=True, useNativeMomentLut=True,
        volumeExactIntegration=False, volumeProxyBlockers=True,
        volumeStableOpaqueCache=True, volumePointIbl=False,
        volumeOpaqueBlockers=True, volumeDepthThreshold=.05000000074505806, castDirectionalShadows=True,
        areaPreservingCoverage=True, minWidthMetres=0., maxWidthMetres=.001,
        volumeVoxelSize=.005, volumeResolution=64,
        volumeAverageWidth=0.00013852179108653218,
        volumeSkyTransmission=True, dualScatteringIbl=True, backscatterScale=1.,
        volumeRasterWidthScale=1.5, volumeTraceStep=1.1, volumeBiasVoxels=1.,
        specularStrength=1., secondarySpecular=1., transmission=1.)
    if eye_patch:
        entries = {m["name"]: m for m in profile["materials"]}
        for patch in eye_patch["materials"]:
            entry = entries[patch["name"]]
            for key, value in patch.items():
                if key in ("parameters", "textures"):
                    entry.setdefault(key, {}).update(value)
                elif key not in ("name", "source"):
                    entry[key] = value
        profile["sourceEvidence"]["eyeShading"] = eye_patch.get("evidence", {})
    for name, patch in (("hairAppearance", hair_appearance), ("hairNtt", hair_ntt)):
        if patch:
            profile["strands"].update(patch.get("strands", {}))
            profile.setdefault("strandAssets", {}).update(patch.get("strandAssets", {}))
            profile["sourceEvidence"][name] = patch.get("evidence", {})
    key = (hair_dslut or {}).get("params", {
        "ior": 1.5499999523162842, "f0": .04652056470513344,
        "azimuthalRoughness": .800000011920929, "cuticleShift": .019999999552965164})
    profile["strands"]["nativeMomentParameters"] = [
        key["ior"], key["f0"], key["azimuthalRoughness"], key["cuticleShift"]]
    profile.setdefault("strandAssets", {})["nativeMomentTable"] = "hair-dslut.lutbin"
    profile["sourceEvidence"]["hairDslut"] = {
        "profile": "hair-dslut-profile.json", "dimensions": [64, 64, 16],
        "storage": "RGBA16F", "atlas": [64, 1024],
        "materialKey": profile["strands"]["nativeMomentParameters"],
        "sourceShaderSha256": (hair_dslut or {}).get("sourceSha256"),
        "assetSha256": (hair_dslut or {}).get("assetSha256"),
        "sampling": "Texel-centered theta/roughness/absorption; eight-neighbor trilinear. Roughness clamps to [.18,.6].",
        "f0Writer": "Native CPU computes ((1-IOR)/(1+IOR))^2; direct/IBL single still use fixed1.5/.04.",
        "fallback": "Changing IOR, azimuthal roughness or cuticle tilt invalidates the immutable bake key and uses the legacy16x16 current-roughness slice.",
    }
    profile["revision"] = 13
    profile["sourceEvidence"]["shadingRevision"] = {
        "revision": 13,
        "hairPointTransport": "hair-point-transport.md",
        "hairCompletion": "hair-rendering-completion.md",
        "hairDslut": "hair-dslut-profile.json",
        "hairQuality": "hair-quality-reconstruction.md",
        "hairDirections": "hair-direction-reconstruction.md",
        "hairDirect": "hair-direct-evidence.json",
        "display": "hair-display-evidence.json",
        "skin": "skin-diffusion-evidence.json",
        "eye": "eye-shading-evidence.json",
        "hair": "hair-scattering-evidence.json",
        "hairVolume": "hair-dsvolume-evidence.json",
        "hairIbl": "hair-ibl-evidence.json",
        "hairWidth": "hair-width-evidence.json",
        "hairCoverage": "hair-coverage-evidence.json",
        "hairColor": "hair-color-evidence.json",
        "hairNtt": "hair-ntt-evidence.json",
        "hairLighting": "hair-lighting-evidence.json",
        "transport": "Raster direct light and precomputed environment; no RT/realtime GI.",
    }
    profile["sourceEvidence"]["strandReconstruction"] = (
        "Recovered absorption, R/TT/TRT longitudinal offsets/widths, IOR and "
        "precomputed-light colorization; spatial strand-density and light-path "
        "atlases reconstruct DSVolume self-occlusion. Source dual-direction IBL "
        "restores local/propagated backscatter, angular lobes and variance terms. Source A/B/C gradients "
        "use authored arc/random bytes, original texels, cumulative selection and linear-RGB HSV operations; "
        "the endpoint weighted means remain a comparison fallback. Native64x64x16 DSLUT "
        "retains half storage and dynamic roughness-neighbor sampling; CPU generation "
        "follows the shipped estimator without claiming native-GPU bit identity. Separate "
        "premultiplied hair color/coverage history uses last-executed per-fiber motion.")
    profile["sourceEvidence"]["limitations"] = [
        "Source profile selection and runtime scene overrides are not captured.",
        "Screen-space skin diffusion preserves specular, depth and material boundaries; "
        "out-of-screen transport and original pass equivalence are not established.",
        "Eye raster POM uses the recovered shader; original light tracing/caustic "
        "passes and scene-dependent reflection illumination are not executed.",
        "Eye-shell UV masks/depth eligibility/blur follow source shaders; runtime "
        "projection/depth units and scene-normal/area-light reflection inputs remain adapted.",
        "Full source DSVolume density/mip/scene-depth transport remain "
        "reconstruction limits; the host volume computes actual strand self-occlusion.",
        "Hair source IBL uses a host SH environment by default; the optional ST30_101 study "
        "supplies extracted HDR and a six-axis source-formula adapter. Original spatial probe selection, "
        "runtime gains and third-chapter location remain unverified. Optional point IBL queries actual "
        "shading positions and directions; the default voxel-mean paths and the point mode both use "
        "the host density reconstruction, without native 3D mips/jitter. "
        "Legacy ambient and forward-only af^N modes remain comparison controls; source hair colors are unchanged.",
        "View-dependent cloth sheen/cavity and animated wrinkles are not restored.",
        "Hair source material ColorMap is disabled and TransColor_Rate is zero; the dynamic global BAB "
        "buffer was not captured and may override material color in the original runtime.",
        "Area-preserving analytic ribbon filtering repairs a host coverage loss; native mixed "
        "hardware/Wu software rasterization, sorted OIT layers, depth coverage resolve and temporal "
        "filtering are not fully reproduced. Runtime LOD factor and active native temporal/MSAA state remain unobserved.",
        "The full native DSLUT dimensions, sampling axes, half storage and CPU F0 writer are recovered; "
        "offline float32 quadrature may differ from native GPU fast-math/FMA. Material key changes "
        "fall back explicitly to the prior16x16 slice; no on-launch65536-texel CPU bake is performed.",
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("--eye-patch", type=Path)
    parser.add_argument("--hair-appearance", type=Path)
    parser.add_argument("--hair-ntt", type=Path)
    parser.add_argument("--hair-dslut", type=Path)
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    eye = json.loads(args.eye_patch.read_text(encoding="utf-8")) if args.eye_patch else None
    read_patch = lambda path: json.loads(path.read_text(encoding="utf-8")) if path and path.is_file() else None
    appearance = read_patch(args.hair_appearance or args.profile.parent / "hair-appearance-profile.json")
    ntt = read_patch(args.hair_ntt or args.profile.parent / "hair-ntt-profile.json")
    dslut = read_patch(args.hair_dslut or args.profile.parent / "hair-dslut-profile.json")
    upgrade(profile, eye, appearance, ntt, dslut)
    for entry in profile["materials"]:
        for name in entry.get("textures", {}).values():
            path = Path(name)
            if path.is_absolute() or ".." in path.parts or not (args.profile.parent / path).is_file():
                raise ValueError(f"Missing or unsafe texture path: {name}")
    for name in profile.get("strandAssets", {}).values():
        path = Path(name)
        if path.is_absolute() or ".." in path.parts or not (args.profile.parent / path).is_file():
            raise ValueError(f"Missing or unsafe strand asset path: {name}")
    temporary = args.profile.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(profile, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(args.profile)
    print("Diana shading profile revision 13 written; stable opaque cache enabled; point IBL opt-in; source records preserved.")


if __name__ == "__main__":
    main()
