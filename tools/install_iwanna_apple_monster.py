"""Install the Lua apple monster, aimed apple shots and attached pixel health bar."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from shutil import copy2
import struct
import zlib

REPO = Path(__file__).resolve().parents[1]
EXAMPLE = REPO / "Asset/IWanna/Examples/apple_monster.lua"
MODULE_BEGIN = "-- BEGIN LUA APPLE MONSTER MODULE"
MODULE_END = "-- END LUA APPLE MONSTER MODULE"
PREVIOUS_BEGIN = "-- BEGIN APPLE_MONSTER_PREVIOUS_ROOM_LOGIC"
PREVIOUS_END = "-- END APPLE_MONSTER_PREVIOUS_ROOM_LOGIC"


def prefab_definitions() -> dict[str, dict]:
    values = {
        "apple_monster_trap": {
            "role": "trigger", "image": "demo_ring_plate.png", "width": 2.5, "height": .9,
            "collide": True, "event": "summon_apple_monster", "health": 5,
            "monsterSize": 6, "wanderSpeed": 6, "wanderRadius": 14,
            "wanderHeight": 3, "hoverOffset": 3, "retargetSeconds": 1.1,
            "shotInterval": 1.4, "firstShotDelay": 1, "shotSpeed": 20,
            "shotSize": 1.4, "shotLifetime": 4, "spawnRadiusMin": 12, "spawnRadiusMax": 18,
            "barWidth": 8, "barHeight": .65, "barOffset": 4.5,
            "arenaMinX": -970, "arenaMaxX": 970, "arenaMinY": -970, "arenaMaxY": 970,
            "monsterPrefab": "apple_monster_actor", "shotPrefab": "apple_monster_shot",
            "seed": 0, "spawnSound": "apple", "shotSound": "apple",
            "hitSound": "Block Change", "deathSound": "Break",
            "detectionEnabled": True, "detectionX": 0, "detectionY": -1.2,
            "detectionW": 4, "detectionH": 3,
        },
        "apple_monster_actor": {
            "role": "trigger", "image": "demo_apple.png", "width": 6, "height": 6,
            "collide": True, "receivesShots": True, "event": "apple_monster_actor", "health": 5,
        },
        "apple_monster_shot": {
            "role": "hazard", "image": "demo_apple.png", "width": 1.4, "height": 1.4,
            "collide": True, "despawnAfter": 6,
        },
        "apple_health_frame": {"role": "decoration", "image": "apple_health_white.png", "width": 8.35, "height": 1},
        "apple_health_empty": {"role": "decoration", "image": "apple_health_red.png", "width": 8, "height": .65},
        "apple_health_fill": {"role": "decoration", "image": "apple_health_yellow.png", "width": 8, "height": .65},
    }
    return {name: {"format": "IWANNA_PREFAB_1", "pixelArt": True, "visible": True, **value}
            for name, value in values.items()}


def write_prefabs(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for name, value in prefab_definitions().items():
        path = directory / f"{name}.prefab.json"
        if not path.exists():
            path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")


def build_bar_assets() -> None:
    # Code-generated single-color texels; no artwork or platform library needed.
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data))+kind+data+struct.pack(">I", zlib.crc32(kind+data))
    for color, rgba in {"white": (255,255,255,255), "red": (184,24,38,255), "yellow": (255,230,0,255)}.items():
        data = b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR", struct.pack(">IIBBBBB",1,1,8,6,0,0,0))
        data += chunk(b"IDAT",zlib.compress(b"\0"+bytes(rgba)))+chunk(b"IEND",b"")
        path = REPO / f"Asset/IWanna/images/apple_health_{color}.png"
        if not path.exists():
            path.write_bytes(data)


def wrap_script(source: str) -> str:
    module = EXAMPLE.read_text(encoding="utf-8").rstrip()+"\n"
    markers = (MODULE_BEGIN, MODULE_END, PREVIOUS_BEGIN, PREVIOUS_END)
    if any(marker in source for marker in markers):
        if any(source.count(marker) != 1 for marker in markers):
            raise ValueError("Ambiguous apple monster wrapper; preserving room code")
        begin, end, previous_begin, previous_end = (source.index(marker) for marker in markers)
        if not begin < end < previous_begin < previous_end:
            raise ValueError("Malformed apple monster wrapper markers")
        return source[:begin+len(MODULE_BEGIN)]+"\n"+module+source[end:]
    if not source.endswith("\n"):
        source += "\n"
    return ("local apple_monster = (function()\n"+MODULE_BEGIN+"\n"+module+MODULE_END+
            "\nend)()\nlocal previous = (function()\n"+PREVIOUS_BEGIN+"\n"+source+
            PREVIOUS_END+"\nend)()\n"+'''
local result = {}
for key, callback in pairs(previous) do result[key] = callback end
result.on_entity_spawn = function(ctx, event)
    if previous.on_entity_spawn then previous.on_entity_spawn(ctx, event) end
    apple_monster.on_entity_spawn(ctx, event)
end
result.on_trigger = function(ctx, event)
    if apple_monster.handles_trigger(event.name) then apple_monster.on_trigger(ctx, event)
    elseif previous.on_trigger then previous.on_trigger(ctx, event) end
end
result.on_hit = function(ctx, event)
    if apple_monster.handles_actor(event.name) then apple_monster.on_hit(ctx, event)
    elseif previous.on_hit then previous.on_hit(ctx, event) end
end
result.on_timer = function(ctx, event)
    if apple_monster.handles_timer(event.name) then apple_monster.on_timer(ctx, event)
    elseif previous.on_timer then previous.on_timer(ctx, event) end
end
result.on_death = function(ctx, event)
    apple_monster.cancel_all(ctx)
    if previous.on_death then previous.on_death(ctx, event) end
end
return result
''')


def local_path(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if Path(relative).is_absolute() or not path.is_relative_to(root):
        raise ValueError(f"Project path must stay inside root: {relative}")
    return path


def backup_once(path: Path) -> None:
    backup = path.with_name(path.name+".before_apple_monster.bak")
    if path.exists() and not backup.exists():
        copy2(path, backup)


def copy_asset(source: Path, destination: Path) -> None:
    if destination.exists() and source.read_bytes() == destination.read_bytes():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    backup_once(destination)
    copy2(source, destination)


def install_project(root: Path, room_ids: tuple[str, ...]) -> None:
    root = root.resolve()
    world = json.loads((root / "world.json").read_text(encoding="utf-8-sig"))
    if world.get("format") != "IWANNA_WORLD_2":
        raise ValueError("Expected an IWANNA_WORLD_2 project")
    scripts = {}
    for room_id in room_ids:
        if not room_id or Path(room_id).name != room_id or "/" in room_id or "\\" in room_id:
            raise ValueError("Expected room ID, not a path")
        room_path = local_path(root, world["roomDirectory"])/f"{room_id}.room.json"
        room = json.loads(room_path.read_text(encoding="utf-8-sig"))
        if not isinstance(room.get("script"),str) or "scriptLua" in room:
            raise ValueError("Apple monster installation requires an external room script")
        path = local_path(root, room["script"])
        scripts[path] = wrap_script(path.read_text(encoding="utf-8-sig"))
    build_bar_assets()
    write_prefabs(local_path(root, world["prefabDirectory"]))
    for name in ["demo_apple.png", "demo_ring_plate.png", "apple_health_white.png", "apple_health_red.png", "apple_health_yellow.png"]:
        copy_asset(REPO/"Asset/IWanna/images"/name, local_path(root,"images/"+name))
    for name in ["apple.wav", "Block Change.wav", "Break.wav"]:
        copy_asset(REPO/"Asset/IWanna/audio"/name, local_path(root,"audio/"+name))
    for path, source in scripts.items():
        if path.read_text(encoding="utf-8-sig") != source:
            backup_once(path)
            path.write_text(source, encoding="utf-8")
    copy_asset(EXAMPLE, local_path(root,"scripts/apple_monster.lua"))
    print(f"Installed Lua apple monster in {root} for {', '.join(room_ids)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--room", action="append")
    parser.add_argument("--workshop", action="store_true")
    args = parser.parse_args()
    if not args.project and not args.workshop:
        parser.error("choose --project and/or --workshop")
    if args.workshop:
        build_bar_assets()
        write_prefabs(REPO/"Asset/IWanna/Workshop/prefabs")
    if args.project:
        install_project(args.project,tuple(args.room or ["room_01"]))
