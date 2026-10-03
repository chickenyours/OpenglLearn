"""Add a small, optional hidden-wall and break-block demo to the upper gallery."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from shutil import copy2


def install(root: Path) -> None:
    path = root / "rooms/room_01.room.json"
    room = json.loads(path.read_text(encoding="utf-8-sig"))
    if room.get("metadata", {}).get("floorLayout") != "two_floors_segmented_exits_v1":
        raise ValueError("The block demo expects the existing two-floor room_01")
    if not room.get("metadata", {}).get("reactiveBlockDemo"):
        def entity(prefab: str, x: float, y: float, w: float, h: float) -> dict:
            return {"kind": "Entity", "prefab": prefab, "position": [x, y],
                    "size": [w, h], "properties": {}}
        added = {
            "block_demo": {"kind": "Spawn", "position": [75, -26.1], "properties": {}},
            "checkpoint_block_demo": entity("checkpoint", 75, -27.5, 4, 4.8),
            "hidden_wall_demo": entity("hidden_wall", 81.25, -26.25, 2.5, 2.5),
            "trap_block_demo_1": entity("trap_block", 86.25, -26.25, 2.5, 2.5),
            "trap_block_demo_2": entity("trap_block", 88.75, -26.25, 2.5, 2.5),
        }
        if set(added) & set(room["entities"]):
            raise ValueError("Demo IDs already exist; refusing to overwrite authored entities")
        backup = path.with_name(path.name + ".before_reactive_blocks.bak")
        if not backup.exists():
            copy2(path, backup)
        room["entities"].update(added)
        room["metadata"]["reactiveBlockDemo"] = "upper_west_gallery_v1"
        path.write_text(json.dumps(room, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (root / "run_block_demo.bat").write_text(
        '@echo off\ncall "%~dp0run.bat" --room room_01 --spawn block_demo %*\n', encoding="ascii")
    # The solid gallery floor remains beneath both disappearing blocks. These
    # demonstrations cannot remove a required landing or block either exit.
    replay = {"format": "IWANNA_INPUT_REPLAY_1", "cases": [
        {"name": "hidden wall stops player on contact", "room": "room_01", "spawn": "block_demo",
         "settleFrames": 20, "steps": [{"move": 1, "frames": 45}],
         "expect": {"room": "room_01", "alive": True, "grounded": True,
                    "position": [79.58, -26.1], "tolerance": [.25, .2]}},
        {"name": "broken blocks immediately allow passage", "room": "room_01", "spawn": "block_demo",
         "position": [83.5, -26.1], "settleFrames": 10,
         "steps": [{"move": 1, "frames": 60}, {"frames": 60}],
         "expect": {"room": "room_01", "alive": True, "grounded": True,
                    "position": [92.2, -26.1], "tolerance": [.4, .2]}},
    ]}
    (root / "REACTIVE_BLOCKS_INPUT_CHECK.json").write_text(
        json.dumps(replay, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Reactive block demonstrations installed in the upper west gallery: {root}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    install(parser.parse_args().project.resolve())
