#!/usr/bin/env python3
"""Import only sounds actually bound by the supplied Brotato scene/prefabs.

WAV effects are copied byte for byte. The one Vorbis music file is decoded to
PCM16 WAV with a local ffmpeg executable so the runtime needs no new codec.
The Unity source is read only. Output is deterministic for a given ffmpeg build.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import wave


SOUNDS = [
    ("wand", "whoosh_swish_high_big_02.wav", .732, False,
     "Assets/PrefabInstance/bullet_wand - Copy.prefab", "7686417301862889912", "w1 projectile PlayOnAwake"),
    ("gun", "gun_submachine_auto_shot_02.wav", .504, False,
     "Assets/Brotato.unity", "607546830", "w5.Shoot: Shoot AudioSource.Play"),
    ("burst", "gun_shotgun_sawed_off_shot_02.wav", .504, False,
     "Assets/Brotato.unity", "979097169", "w6.Shoot: Shoot AudioSource.Play"),
    ("enemy_death", "bullet_impact_body_flesh_08.wav", .618, False,
     "Assets/AI/NpcGo.prefab", "2557155676306231469", "EnemyNPC.OnCollisionEnter2D: lethal weapon hit; torch overrides gain to 0.3"),
    ("fire_death", "fireball_impact_burn_04.wav", 1., False,
     "Assets/AI/NpcGo.prefab", "96436021161931846", "EnemyNPC.OnCollisionEnter2D: torch additionally plays DeadFire"),
    ("material", "water_drop_drip_single_04.wav", 1., False,
     "Assets/Brotato.unity", "263728995", "PlayerController: collision/trigger with add material"),
    ("button", "button_press.wav", .765, False,
     "Assets/Brotato.unity", "492025373", "UI Button persistent Play call: BtnClicked"),
    ("music", "extreme-chaos by 2050 Artlist.ogg", .594, True,
     "Assets/Brotato.unity", "1607677721", "MusicBackground PlayOnAwake throughout the scene"),
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "Asset/Brotato")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()
    args.source = args.source.resolve()
    args.output = args.output.resolve()
    if args.output == args.source or args.source in args.output.parents:
        parser.error("--output must be outside the read-only Unity source directory")
    output = args.output / "Audio"
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    for name, filename, gain, loop, binding_path, source_id, trigger in SOUNDS:
        source = args.source / "Assets/AudioClip" / filename
        meta = source.with_suffix(source.suffix + ".meta").read_text(encoding="utf-8-sig")
        guid = re.search(r"^guid: (\w+)$", meta, re.M).group(1)
        binding_text = (args.source / binding_path).read_text(encoding="utf-8-sig")
        binding = re.search(rf"^--- !u!82 &{source_id}\n(.*?)(?=^--- !u!|\Z)", binding_text, re.M | re.S)
        if not binding or f"guid: {guid}" not in binding.group(1):
            raise ValueError(f"AudioSource binding changed: {binding_path}#{source_id}")
        gain_match = re.search(r"^  m_Volume: ([\d.]+)$", binding.group(1), re.M)
        loop_match = re.search(r"^  Loop: (\d)$", binding.group(1), re.M)
        if float(gain_match.group(1)) != gain or bool(int(loop_match.group(1))) != loop:
            raise ValueError(f"AudioSource gain/loop changed: {binding_path}#{source_id}")
        target = output / f"{name}.wav"
        if source.suffix == ".wav":
            shutil.copyfile(source, target)
            conversion = "byte-for-byte WAV copy"
        else:
            subprocess.run([args.ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(source),
                            "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                            "-c:a", "pcm_s16le", str(target)], check=True)
            conversion = "Vorbis decoded by ffmpeg to PCM16 WAV; original rate/channels retained"
        with wave.open(str(target)) as wav:
            if wav.getnchannels() not in (1, 2) or wav.getsampwidth() != 2:
                raise ValueError(f"Unsupported imported PCM: {target}")
            info = dict(sample_rate=wav.getframerate(), channels=wav.getnchannels(),
                        bits_per_sample=wav.getsampwidth() * 8, frames=wav.getnframes(),
                        seconds=wav.getnframes() / wav.getframerate())
        entries.append(dict(name=name, file=f"Audio/{name}.wav", source=source.relative_to(args.source).as_posix(),
                            source_guid=guid, source_sha256=digest(source), output_sha256=digest(target),
                            binding=binding_path, binding_file_id=source_id, trigger=trigger,
                            gain=gain, loop=loop, conversion=conversion, **info))
    manifest = {
        "format_version": 1,
        "scope": "Only AudioSources bound in Brotato.unity and the active enemy/wand prefabs",
        "notes": [
            "Torch, knife and laser have no firing AudioSource binding; no invented firing clips.",
            "Player hurt, level-up and end-wave WAV assets exist but have no active gameplay binding.",
            "Music starts at the serialized 0.594. The three source Music settings panels are initially inactive; opening one lets SliderShow overwrite gain from PlayerPrefs (first-use default 0.75). Music settings are not migrated yet.",
            "Game short voices freeze on pause and are cleared on death, wave transition and return home; this is a migration lifecycle improvement over Unity Time.timeScale alone.",
        ],
        "sounds": entries,
    }
    (args.output / "audio_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Imported {len(entries)} bound sounds to {output}")


if __name__ == "__main__":
    main()
