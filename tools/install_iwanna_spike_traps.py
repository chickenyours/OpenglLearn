"""Install reusable Lua telescopic/flying spikes without changing room geometry."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from shutil import copy2

REPO = Path(__file__).resolve().parents[1]
EXAMPLE = REPO / "Asset/IWanna/Examples/spike_traps.lua"
PREVIOUS_BEGIN = "-- BEGIN SPIKE_TRAPS_PREVIOUS_ROOM_LOGIC"
PREVIOUS_END = "-- END SPIKE_TRAPS_PREVIOUS_ROOM_LOGIC"
MODULE_BEGIN = "-- BEGIN LUA SPIKE TRAPS MODULE"
MODULE_END = "-- END LUA SPIKE TRAPS MODULE"


def prefab_definitions() -> dict[str, dict]:
    controller = {
        "role": "trigger", "image": "demo_ring_plate.png", "width": 2.5,
        "height": 0.9, "collide": True, "visible": True, "opacity": 1,
        "effectAnchor": "$self", "effectPrefab": "spike_trap_effect",
        "detectionEnabled": True, "sound": "apple", "cooldown": 1,
    }
    telescope = {
        **controller, "event": "telescopic_spike", "restLength": 0.25,
    }
    values = {
        "floor_spike_trap": {
            **telescope, "orientation": "up", "length": 9, "thickness": 1,
            "extendSeconds": 0.18, "holdSeconds": 0.15,
            "retractSeconds": 0.35, "delay": 0.25,
            "detectionX": 0, "detectionY": -4.5, "detectionW": 3, "detectionH": 9,
        },
        "left_spike_trap": {
            **telescope, "orientation": "left", "length": 24, "thickness": 2.5,
            "extendSeconds": 0.22, "holdSeconds": 0.3,
            "retractSeconds": 0.5, "delay": 0.35,
            "detectionX": -12, "detectionY": 0, "detectionW": 24, "detectionH": 8,
        },
        "flying_spike_trap": {
            **controller, "event": "flying_spikes", "orientation": "left",
            "length": 2.8, "thickness": 1, "laneCount": 5, "laneSpacing": 4,
            "count": 2, "speed": 28, "warningSeconds": 0.6,
            "flightDistance": 18, "lifetime": 1.2, "minSpawnDistance": 6, "seed": 0,
            "detectionX": -9, "detectionY": 8, "detectionW": 18, "detectionH": 20,
        },
        "spike_trap_effect": {
            "role": "hazard", "image": "demo_spike.png", "width": 1,
            "height": 9, "collide": False, "visible": True, "opacity": 1,
        },
        "spike_trap_warning": {
            "role": "decoration", "image": "demo_spike.png", "width": 1,
            "height": 9, "collide": False, "visible": True, "opacity": 0.25,
        },
    }
    return {name: {"format": "IWANNA_PREFAB_1", "pixelArt": True, **value}
            for name, value in values.items()}


def wrap_script(source: str) -> str:
    module = EXAMPLE.read_text(encoding="utf-8").rstrip() + "\n"
    markers = (MODULE_BEGIN, MODULE_END, PREVIOUS_BEGIN, PREVIOUS_END)
    if any(marker in source for marker in markers):
        if any(source.count(marker) != 1 for marker in markers):
            raise ValueError("Ambiguous spike-trap wrapper; refusing to discard room code")
        begin, end, previous_begin, previous_end = (source.index(marker) for marker in markers)
        if not begin < end < previous_begin < previous_end:
            raise ValueError("Malformed spike-trap wrapper markers")
        return source[:begin + len(MODULE_BEGIN)] + "\n" + module + source[end:]
    if not source.endswith("\n"):
        source += "\n"
    return ("-- Lua telescopic and lane-based flying spike abilities.\n"
            "local spike_traps = (function()\n" + MODULE_BEGIN + "\n" + module
            + MODULE_END + "\nend)()\nlocal previous = (function()\n"
            + PREVIOUS_BEGIN + "\n" + source + PREVIOUS_END + "\nend)()\n" + '''
local result = {}
for key, callback in pairs(previous) do result[key] = callback end
result.on_trigger = function(ctx, event)
    if spike_traps.handles_trigger(event.name) then
        spike_traps.on_trigger(ctx, event)
    elseif previous.on_trigger then previous.on_trigger(ctx, event) end
end
result.on_timer = function(ctx, event)
    if spike_traps.handles_timer(event.name) then
        spike_traps.on_timer(ctx, event)
    elseif previous.on_timer then previous.on_timer(ctx, event) end
end
result.on_death = function(ctx, event)
    spike_traps.cancel_all(ctx)
    if previous.on_death then previous.on_death(ctx, event) end
end
return result
''')


def local_path(root: Path, relative: str) -> Path:
    value = Path(relative)
    path = (root / value).resolve()
    if value.is_absolute() or not path.is_relative_to(root):
        raise ValueError(f"Project path must stay inside its root: {relative}")
    return path


def backup_once(path: Path) -> None:
    backup = path.with_name(path.name + ".before_spike_traps.bak")
    if path.exists() and not backup.exists():
        copy2(path, backup)


def copy_asset(source: Path, destination: Path) -> None:
    if destination.exists() and source.read_bytes() == destination.read_bytes():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    backup_once(destination)
    copy2(source, destination)


def write_prefabs(directory: Path) -> list[Path]:
    directory.mkdir(parents=True, exist_ok=True)
    written = []
    for name, value in prefab_definitions().items():
        path = directory / f"{name}.prefab.json"
        if not path.exists():
            path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
            written.append(path)
    return written


def install_project(root: Path, room_ids: tuple[str, ...] = ("room_02",)) -> None:
    root = root.resolve()
    world = json.loads((root / "world.json").read_text(encoding="utf-8-sig"))
    if world.get("format") != "IWANNA_WORLD_2":
        raise ValueError("Spike-trap installer expects an IWANNA_WORLD_2 project")
    room_dir = local_path(root, world["roomDirectory"])
    prefab_dir = local_path(root, world["prefabDirectory"])
    scripts = {}
    for room_id in room_ids:
        if not room_id or Path(room_id).name != room_id or "/" in room_id or "\\" in room_id:
            raise ValueError(f"Expected a room ID, not a path: {room_id}")
        room = json.loads((room_dir / f"{room_id}.room.json").read_text(encoding="utf-8-sig"))
        if not isinstance(room.get("script"), str) or "scriptLua" in room:
            raise ValueError(f"{room_id} must use an external script before installing")
        script = local_path(root, room["script"])
        scripts[script] = wrap_script(script.read_text(encoding="utf-8-sig"))
    assets = ["images/demo_spike.png", "images/demo_ring_plate.png", "audio/apple.wav"]
    for relative in assets:
        if not (REPO / "Asset/IWanna" / relative).is_file():
            raise FileNotFoundError(f"Missing spike-trap asset: {relative}")
    write_prefabs(prefab_dir)
    for relative in assets:
        copy_asset(REPO / "Asset/IWanna" / relative, local_path(root, relative))
    for script, wrapped in scripts.items():
        if script.read_text(encoding="utf-8-sig") != wrapped:
            backup_once(script)
            script.write_text(wrapped, encoding="utf-8")
    copy_asset(EXAMPLE, local_path(root, "scripts/spike_traps.lua"))
    print(f"Installed Lua spike traps for {', '.join(room_ids)} in {root}; room geometry unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--room", action="append", help="Room ID; repeat for multiple rooms (default room_02)")
    parser.add_argument("--workshop", action="store_true", help="Create missing repository Workshop templates")
    args = parser.parse_args()
    if not args.project and not args.workshop:
        parser.error("choose --project PATH and/or --workshop")
    if args.workshop:
        write_prefabs(REPO / "Asset/IWanna/Workshop/prefabs")
    if args.project:
        install_project(args.project, tuple(args.room or ["room_02"]))
