#!/usr/bin/env python3
"""Install the generic RHI frame-rendering patch into an OpenglLearn checkout."""

from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tarfile
from datetime import datetime, timezone

PATCH_VERSION = "2026-09-17-rhi-frame-v1"
PAYLOAD_SHA256 = "9027b5ead47093b77da54cb2f3cfe3ca594a2d427c324dabcffe98fd9a710ae9"
EXPECTED_FILES = [
    "Module/ApplicationWindow/module.h",
    "Module/CMakeLists.txt",
    "Module/Render/Private/Backend/Opengl/backend.h",
    "Module/Render/Private/Backend/backend.h",
    "Module/Render/Private/rhi_device.h",
    "Module/Render/Private/rhi_resource_pool.h",
    "Module/Render/Public/RHICommand/FrameCommand/rhi_command_dispatcher.h",
    "Module/Render/Public/RHICommand/FrameCommand/rhi_frame_command.h",
    "Module/Render/Public/RHICommand/FrameCommand/rhi_frame_command_buffer.h",
    "Module/Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h",
    "Module/Render/Public/RHICommand/rhi_buffer_command.h",
    "Module/Render/Public/RHICommand/rhi_command.h",
    "Module/Render/Public/RHICommand/rhi_shader_command.h",
    "Module/Render/Public/RHICommand/rhi_texture_command.h",
    "Module/Render/Public/RHIResourceType/Buffer/uniform_buffer.h",
    "Module/Render/Public/RHIResourceType/Buffer/vertex_buffer.h",
    "Module/Render/Public/RHIResourceType/Pipeline/pipeline.h",
    "Module/Render/Public/RHIResourceType/Texture/texture.h",
    "Module/Render/Public/RHIResourceType/rhi_resource_type.h",
    "Module/Render/README_RHI_FRAME.md",
    "Module/Render/Test/frame_command_buffer_test.cpp",
    "Module/Render/Test/test2.cpp",
    "Module/Render/module.h",
]
PAYLOAD_B64 = (
    "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"
    "ucBHig3RDg9Yey0//RMOE6/TmWF/jKEKaZgaM7DDVIN6RIVhp0nKTThxDVkfO22tglyWw8B4PLiVKJ/outmIWx7+ykiuxQ6GOzeM"
    "vMxb2JmxzSj7aLEhECFjYGXJu76vMPFMigFQ6J4NgaQcRRuGFZCHA4B0jyXBny2SOAXK3GO288ornt6nourxguMV9Pi96UbLAy6l"
    "OdrE5yVlA5A2fr9bGxpip/8ZzxzkpIGWRgPqMJvhjeOHmYyRfNhYlV+3xyeWlx28QhaqNmORsnsAPUNG7QyebQ5uSd5pHTEt+xiO"
    "Wp+vl2usDUbnsHMIKO/1YRPT3uViiju47eOGpRKw6VtBv1HRcVmdRgEk4Ni/AhyleLgs/MJI9fG4DSwXN9xkax+pHSwwYiw2G1eC"
    "De5ILw8jLJmA1eBysYp2ttvHKyqbgNmm5WImd8nbx0uWTMBq80rmpNhsX958FIUTcPvNCnAT+/bLQk2UTeKry14CyAnAMugsKpyE"
    "3LK5vnGMsDwUFRBJiC57QbCcR7SPqgVIErLLXhnIEcfyWJ4onITcipYGedy4XOxkiAobetZbjgyqmVe1CmLSaPn1OBXN8PonheWt"
    "upLXSq+Sey+RdsW3dHaWiOIupHqewhTxjMwlTIupqJir18rrnT9jtui6oqHJi4pHqNH80Wn045R5iZWVUSjH/e2gP9RneMVjwpOf"
    "XbIPxeStjgJjb7mS0cRmxDJymmPqQaJdFj1Ijo/cK91T2umVPH/aJtyzWvKgriN8ZteKujW1Gg1Lx0n1vOeyEoifreCmh5VY/2Cv"
    "SXzs7BLt/SNo7TP1Gts8MlGWjyyWUVzo6M9xw3OX+qsdyuNbhjbK4ymc8mQuDMhRq5Znf3mkZxKbbMoKqS2JYBIoL90Qki08qeZv"
    "52GW+LhQhEyGsSV2C30kQ3iqmYsgx0cXA1Xx1ctPGOPYLXnrqBOYbY19bC2EGiywLTRBAs/49TwG5nKxsKSUDSNWW8l/IdYUtrpN"
    "7hSWWyaDwsfkMltiz2wUcjVzkdpiJ4HtKMcyCzThrDW2LeFasqXBVz3EsWA6YVymiMUxAZaSIQ1eliMvC3IxEXpioSaclGkyolGV"
    "rWiaKrWjNUsjog2S1l2cBE3NQW/RcObKNhuT+KqyuWnlpRxEW2xUAUpYpwrKwjX5OdzvQZjGszjrDrM89iCeM/gHFZLBeS37o8hZ"
    "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"
)


def load_payload() -> dict[str, bytes]:
    compressed = base64.b64decode(PAYLOAD_B64)
    actual = hashlib.sha256(compressed).hexdigest()
    if actual != PAYLOAD_SHA256:
        raise RuntimeError("embedded payload checksum mismatch")

    result: dict[str, bytes] = {}
    expected = set(EXPECTED_FILES)
    with tarfile.open(fileobj=io.BytesIO(compressed), mode="r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            path = member.name.replace("\\", "/")
            if path.startswith("./"):
                path = path[2:]
            if path not in expected:
                raise RuntimeError(f"unexpected payload path: {path}")
            source = archive.extractfile(member)
            if source is None:
                raise RuntimeError(f"cannot read payload path: {path}")
            result[path] = source.read()

    missing = expected.difference(result)
    if missing:
        raise RuntimeError("payload is missing: " + ", ".join(sorted(missing)))
    return result


def verify_root(root: Path) -> None:
    if not (root / "CMakeLists.txt").is_file():
        raise RuntimeError("CMakeLists.txt not found beside the patch script")
    if not (root / "Module" / "Render").is_dir():
        raise RuntimeError("Module/Render not found; place this script in repository root")


def changed_files(root: Path, payload: dict[str, bytes]) -> list[str]:
    changed: list[str] = []
    for relative, wanted in payload.items():
        target = root / relative
        if not target.is_file() or target.read_bytes() != wanted:
            changed.append(relative)
    return sorted(changed)


def install(root: Path, payload: dict[str, bytes], dry_run: bool) -> int:
    changed = changed_files(root, payload)
    if not changed:
        print(f"{PATCH_VERSION}: already installed")
        return 0

    print(f"{PATCH_VERSION}: {len(changed)} file(s) will be replaced")
    if dry_run:
        for relative in changed:
            print(f"  {relative}")
        return 0

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = root / f".rhi_frame_patch_backup_{stamp}"
    suffix = 1
    while backup.exists():
        backup = root / f".rhi_frame_patch_backup_{stamp}_{suffix}"
        suffix += 1

    existed: list[str] = []
    added: list[str] = []
    for relative in changed:
        source = root / relative
        if source.is_file():
            destination = backup / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            existed.append(relative)
        else:
            added.append(relative)

    backup.mkdir(parents=True, exist_ok=True)
    manifest = {
        "patch_version": PATCH_VERSION,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "replaced_files": existed,
        "added_files": added,
    }
    (backup / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    for relative in changed:
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".rhi-patch-tmp")
        temporary.write_bytes(payload[relative])
        os.replace(temporary, target)

    print(f"Installed successfully. Backup: {backup.name}")
    return 0


def check(root: Path, payload: dict[str, bytes]) -> int:
    changed = changed_files(root, payload)
    if changed:
        print("Patch is not fully installed:")
        for relative in changed:
            print(f"  {relative}")
        return 1
    print(f"{PATCH_VERSION}: installation verified")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install the generic RHI frame-rendering patch."
    )
    parser.add_argument(
        "--check", action="store_true",
        help="verify files without changing them",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="list files that would be replaced",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    try:
        verify_root(root)
        payload = load_payload()
        if args.check:
            return check(root, payload)
        return install(root, payload, args.dry_run)
    except Exception as error:
        print(f"Patch failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

