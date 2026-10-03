"""Install reusable Lua delayed traps without changing any room geometry.

Only this installer's wrapper is replaced on repeat runs. The original room
module, including shooting/apple-ring wrappers and author edits, is retained.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from shutil import copy2

REPO = Path(__file__).resolve().parents[1]
EXAMPLE = REPO / "Asset/IWanna/Examples/delayed_traps.lua"
PREVIOUS_BEGIN = "-- BEGIN DELAYED_TRAPS_PREVIOUS_ROOM_LOGIC"
PREVIOUS_END = "-- END DELAYED_TRAPS_PREVIOUS_ROOM_LOGIC"
MODULE_BEGIN = "-- BEGIN LUA DELAYED TRAPS MODULE"
MODULE_END = "-- END LUA DELAYED TRAPS MODULE"


def prefab_definitions() -> dict[str, dict]:
    """Defaults are scalar editor fields except structural atlas metadata."""
    values = {
        "explosion_effect": {
            "role": "hazard", "image": "trap_explosion_atlas.png",
            "width": 8, "height": 8, "collide": False,
            "animation": {"columns": 6, "rows": 4, "duration": 1.0, "loop": False},
        },
        "laser_effect": {
            "role": "hazard", "image": "trap_laser_atlas.png",
            "width": 3.2, "height": 8, "collide": False,
            "animation": {"columns": 8, "rows": 1, "duration": 0.4, "loop": True},
        },
        "effect_anchor": {
            "role": "decoration", "image": "demo_ring_anchor.png",
            "width": 1.2, "height": 1.2, "collide": False, "visible": True,
        },
        "effect_warning": {
            "role": "decoration", "image": "demo_ring_anchor.png",
            "width": 2.2, "height": 2.2, "collide": False, "visible": True,
        },
    }
    for kind, width, height, start, duration, visual in (
        ("explosion", 8, 8, 0.08, 0.2, 1.0),
        ("laser", 3.2, 8, 0.1, 0.8, 1.1),
    ):
        values[f"delayed_{kind}_plate"] = {
            "role": "trigger", "image": "demo_ring_plate.png",
            "width": 2.5, "height": 0.9, "collide": True,
            "detectionEnabled": True, "detectionX": 0, "detectionY": -0.55,
            "detectionW": 2.3, "detectionH": 1.1,
            "event": f"delayed_{kind}", "effectAnchor": f"{kind}_anchor",
            "effectPrefab": f"{kind}_effect", "effectWidth": width,
            "effectHeight": height, "effectRotation": 0,
            "delay": 0.8, "damageStart": start, "damageDuration": duration,
            "visualDuration": visual, "cooldown": 1, "sound": f"trap_{kind}",
        }
    return {name: {"format": "IWANNA_PREFAB_1", "pixelArt": True, **value}
            for name, value in values.items()}


def wrap_script(source: str) -> str:
    """Keep the complete previous module, unwrapping only our own markers."""
    if PREVIOUS_BEGIN in source or PREVIOUS_END in source:
        if source.count(PREVIOUS_BEGIN) != 1 or source.count(PREVIOUS_END) != 1:
            raise ValueError("Ambiguous delayed-trap wrapper; refusing to discard room code")
        start = source.index(PREVIOUS_BEGIN) + len(PREVIOUS_BEGIN)
        end = source.index(PREVIOUS_END)
        if end < start:
            raise ValueError("Malformed delayed-trap wrapper markers")
        source = source[start:end]
        if source.startswith("\n"):
            source = source[1:]
    if not source.endswith("\n"):
        source += "\n"
    module = EXAMPLE.read_text(encoding="utf-8").rstrip() + "\n"
    return ("-- Delayed explosion / laser tasks are implemented entirely in Lua.\n"
            "local delayed_traps = (function()\n" + MODULE_BEGIN + "\n" + module
            + MODULE_END + "\nend)()\nlocal previous = (function()\n"
            + PREVIOUS_BEGIN + "\n" + source + PREVIOUS_END + "\nend)()\n" + '''
local result = {}
for key, callback in pairs(previous) do result[key] = callback end
result.on_trigger = function(ctx, event)
    if delayed_traps.handles_trigger(event.name) then
        delayed_traps.on_trigger(ctx, event)
    elseif previous.on_trigger then previous.on_trigger(ctx, event) end
end
result.on_timer = function(ctx, event)
    if delayed_traps.handles_timer(event.name) then
        delayed_traps.on_timer(ctx, event)
    elseif previous.on_timer then previous.on_timer(ctx, event) end
end
result.on_death = function(ctx, event)
    delayed_traps.cancel_all(ctx)
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
    backup = path.with_name(path.name + ".before_delayed_traps.bak")
    if path.exists() and not backup.exists():
        copy2(path, backup)


def copy_asset(source: Path, destination: Path) -> None:
    if destination.exists() and source.read_bytes() == destination.read_bytes():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    backup_once(destination)
    copy2(source, destination)


def write_prefabs(directory: Path) -> list[Path]:
    """Create missing templates; leave existing developer-edited templates intact."""
    directory.mkdir(parents=True, exist_ok=True)
    written = []
    for name, value in prefab_definitions().items():
        path = directory / f"{name}.prefab.json"
        if not path.exists():
            path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
            written.append(path)
    return written


def install_project(root: Path, room_ids: tuple[str, ...] = ("room_01",)) -> None:
    root = root.resolve()
    world = json.loads((root / "world.json").read_text(encoding="utf-8-sig"))
    if world.get("format") != "IWANNA_WORLD_2":
        raise ValueError("Delayed-trap installer expects an IWANNA_WORLD_2 project")
    room_dir = local_path(root, world["roomDirectory"])
    prefab_dir = local_path(root, world["prefabDirectory"])
    scripts = {}
    # Validate every input first. The installer never changes room JSON.
    for room_id in room_ids:
        if not room_id or Path(room_id).name != room_id or "/" in room_id or "\\" in room_id:
            raise ValueError(f"Expected a room ID, not a path: {room_id}")
        room = json.loads((room_dir / f"{room_id}.room.json").read_text(encoding="utf-8-sig"))
        if not isinstance(room.get("script"), str) or "scriptLua" in room:
            raise ValueError(f"{room_id} must use an external script before installing")
        script = local_path(root, room["script"])
        scripts[script] = wrap_script(script.read_text(encoding="utf-8-sig"))
    assets = [
        "images/trap_explosion_atlas.png", "images/trap_laser_atlas.png",
        "images/demo_ring_anchor.png", "images/demo_ring_plate.png",
        "audio/trap_explosion.wav", "audio/trap_laser.wav",
    ]
    for relative in assets:
        if not (REPO / "Asset/IWanna" / relative).is_file():
            raise FileNotFoundError(f"Build trap assets first: {relative}")
    write_prefabs(prefab_dir)
    for relative in assets:
        copy_asset(REPO / "Asset/IWanna" / relative, local_path(root, relative))
    for script, wrapped in scripts.items():
        if script.read_text(encoding="utf-8-sig") != wrapped:
            backup_once(script)
            script.write_text(wrapped, encoding="utf-8")
    copy_asset(EXAMPLE, local_path(root, "scripts/delayed_traps.lua"))
    print(f"Installed Lua delayed traps for {', '.join(room_ids)} in {root}; room geometry unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--room", action="append", help="Room ID; repeat for multiple rooms (default room_01)")
    args = parser.parse_args()
    install_project(args.project, tuple(args.room or ["room_01"]))
