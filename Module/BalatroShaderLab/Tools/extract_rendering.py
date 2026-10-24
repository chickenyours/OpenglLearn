"""Read-only extraction of the rendering sources in Balatro's fused LÖVE archive.

Usage: python extract_rendering.py --exe <Balatro.exe> --out <new directory>
The input executable is never launched or modified. No external packages needed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def extract(executable: Path, destination: Path) -> dict:
    destination.mkdir(parents=True, exist_ok=False)
    data = executable.read_bytes()
    with zipfile.ZipFile(executable) as archive:
        entries = archive.infolist()
        first_header = min(entry.header_offset for entry in entries)
        overlay = data[first_header:]
        (destination / "Balatro.payload.zip").write_bytes(overlay)
        records = []
        for entry in entries:
            if entry.is_dir():
                continue
            name = PurePosixPath(entry.filename)
            if name.is_absolute() or ".." in name.parts or ":" in entry.filename or "\\" in entry.filename:
                raise ValueError(f"Unsafe archive name: {entry.filename!r}")
            if name.suffix not in (".lua", ".fs", ".png") and str(name) != "version.jkr":
                continue
            # ZipFile.read validates the entry's CRC in addition to our SHA-256.
            contents = archive.read(entry)
            target = destination / "source" / str(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents)
            records.append({"path": str(name), "size": len(contents),
                            "sha256": sha256(contents),
                            "header_offset": entry.header_offset,
                            "crc32": f"{entry.CRC:08x}"})
        manifest = {"source": str(executable.resolve()), "sha256": sha256(data),
                    "zip_members": len(entries), "extracted": records}
        (destination / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        provenance = {"parent_sha256": sha256(data), "offset": first_header,
                      "size": len(overlay), "sha256": sha256(overlay),
                      "note": "Exact suffix from the first ZIP local header; no game execution."}
        (destination / "overlay.json").write_text(json.dumps(provenance, indent=2), encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True,
                        help="A new directory (existing directories are rejected).")
    args = parser.parse_args()
    result = extract(args.exe, args.out)
    print(f"Verified {len(result['extracted'])} rendering sources/assets from {result['zip_members']} ZIP entries")
    print(f"Input SHA-256: {result['sha256']}")
