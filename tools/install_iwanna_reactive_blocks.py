"""Install Lua hidden-wall and breakaway-block rules without editing room layout.

The complete previous room module stays inside one wrapper. Updating an existing
installation replaces only our embedded Lua module, preserving newer outer
wrappers as well as the original room callbacks.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from shutil import copy2

REPO = Path(__file__).resolve().parents[1]
EXAMPLE = REPO / "Asset/IWanna/Examples/reactive_blocks.lua"
PREVIOUS_BEGIN = "-- BEGIN REACTIVE_BLOCKS_PREVIOUS_ROOM_LOGIC"
PREVIOUS_END = "-- END REACTIVE_BLOCKS_PREVIOUS_ROOM_LOGIC"
MODULE_BEGIN = "-- BEGIN LUA REACTIVE BLOCKS MODULE"
MODULE_END = "-- END LUA REACTIVE BLOCKS MODULE"


def prefab_definitions() -> dict[str, dict]:
    return {
        "hidden_wall": {
            "format": "IWANNA_PREFAB_1", "role": "solid", "pixelArt": True,
            "image": "terrain_moss_00.png", "width": 2.5, "height": 2.5,
            "collide": True, "visible": False, "opacity": 1,
            "event": "reveal_hidden_wall", "sound": "Block Change",
        },
        "trap_block": {
            "format": "IWANNA_PREFAB_1", "role": "solid", "pixelArt": True,
            "image": "terrain_ember_alt_15.png", "width": 2.5, "height": 2.5,
            "collide": True, "visible": True, "opacity": 1,
            "event": "break_trap_block", "sound": "Break", "duration": 0.5,
            "popMin": 5, "popMax": 7, "driftMin": 1, "driftMax": 3,
            "gravity": 45, "step": 1 / 60,
        },
    }


def wrap_script(source: str) -> str:
    module = EXAMPLE.read_text(encoding="utf-8").rstrip() + "\n"
    markers = (MODULE_BEGIN, MODULE_END, PREVIOUS_BEGIN, PREVIOUS_END)
    if any(marker in source for marker in markers):
        if any(source.count(marker) != 1 for marker in markers):
            raise ValueError("Ambiguous reactive-block wrapper; refusing to discard room code")
        begin, end, previous_begin, previous_end = (source.index(marker) for marker in markers)
        if not begin < end < previous_begin < previous_end:
            raise ValueError("Malformed reactive-block wrapper markers")
        # Replacing just the module is safe even if another installer has since
        # wrapped this installation. No preceding/following user code is lost.
        return source[:begin + len(MODULE_BEGIN)] + "\n" + module + source[end:]
    if not source.endswith("\n"):
        source += "\n"
    return ("-- Contact-driven hidden walls and breakaway blocks use Lua rules.\n"
            "local reactive_blocks = (function()\n" + MODULE_BEGIN + "\n" + module
            + MODULE_END + "\nend)()\nlocal previous = (function()\n"
            + PREVIOUS_BEGIN + "\n" + source + PREVIOUS_END + "\nend)()\n" + '''
local result = {}
for key, callback in pairs(previous) do result[key] = callback end
result.on_trigger = function(ctx, event)
    if reactive_blocks.handles_trigger(event.name) then
        reactive_blocks.on_trigger(ctx, event)
    elseif previous.on_trigger then previous.on_trigger(ctx, event) end
end
result.on_timer = function(ctx, event)
    if reactive_blocks.handles_timer(event.name) then
        reactive_blocks.on_timer(ctx, event)
    elseif previous.on_timer then previous.on_timer(ctx, event) end
end
result.on_death = function(ctx, event)
    reactive_blocks.cancel_all(ctx)
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
    backup = path.with_name(path.name + ".before_reactive_blocks.bak")
    if path.exists() and not backup.exists():
        copy2(path, backup)


def copy_asset(source: Path, destination: Path) -> None:
    if destination.exists() and source.read_bytes() == destination.read_bytes():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    backup_once(destination)
    copy2(source, destination)


def write_prefabs(directory: Path) -> list[Path]:
    """Create missing templates; retain all existing developer customizations."""
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
        raise ValueError("Reactive-block installer expects an IWANNA_WORLD_2 project")
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
    assets = ["images/terrain_moss_00.png", "images/terrain_ember_alt_15.png",
              "audio/Block Change.wav", "audio/Break.wav"]
    for relative in assets:
        if not (REPO / "Asset/IWanna" / relative).is_file():
            raise FileNotFoundError(f"Missing reactive-block asset: {relative}")
    write_prefabs(prefab_dir)
    for relative in assets:
        copy_asset(REPO / "Asset/IWanna" / relative, local_path(root, relative))
    for script, wrapped in scripts.items():
        if script.read_text(encoding="utf-8-sig") != wrapped:
            backup_once(script)
            script.write_text(wrapped, encoding="utf-8")
    copy_asset(EXAMPLE, local_path(root, "scripts/reactive_blocks.lua"))
    print(f"Installed Lua reactive blocks for {', '.join(room_ids)} in {root}; room geometry unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--room", action="append", help="Room ID; repeat for multiple rooms (default room_01)")
    parser.add_argument("--workshop", action="store_true", help="Create missing repository Workshop templates")
    args = parser.parse_args()
    if not args.project and not args.workshop:
        parser.error("choose --project PATH and/or --workshop")
    if args.workshop:
        write_prefabs(REPO / "Asset/IWanna/Workshop/prefabs")
    if args.project:
        install_project(args.project, tuple(args.room or ["room_01"]))
