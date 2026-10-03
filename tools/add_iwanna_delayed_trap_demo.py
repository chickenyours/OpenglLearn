"""Add two optional trap demonstrations to MyIwana's upper west gallery."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from shutil import copy2


def write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def install(root: Path) -> None:
    path = root / "rooms/room_01.room.json"
    room = json.loads(path.read_text(encoding="utf-8-sig"))
    if room.get("metadata", {}).get("floorLayout") != "two_floors_segmented_exits_v1":
        raise ValueError("This demo expects the existing two-floor room_01 layout")
    e = room["entities"]
    def entity(prefab: str, x: float, y: float, width: float, height: float, **props) -> dict:
        return {"kind": "Entity", "prefab": prefab, "position": [x, y],
                "size": [width, height], "properties": props}
    added = {
        "trap_demo": {"kind": "Spawn", "position": [-3.75, -26.1], "properties": {}},
        "checkpoint_trap_demo": entity("checkpoint", -3.75, -27.5, 4, 4.8),
        "explosion_anchor_demo": entity("effect_anchor", 25, -28, 1.2, 1.2),
        "laser_anchor_demo": entity("effect_anchor", 69, -35, 1.2, 1.2),
        "explosion_plate_demo": entity("delayed_explosion_plate", 12, -25.45, 2.5, .9,
            effectAnchor="explosion_anchor_demo", effectWidth=14, effectHeight=14,
            delay=.8, damageStart=.08, damageDuration=.2, visualDuration=1, cooldown=1),
        "laser_plate_demo": entity("delayed_laser_plate", 52, -25.45, 2.5, .9,
            effectAnchor="laser_anchor_demo", effectWidth=4, effectHeight=20,
            effectRotation=0, delay=.8, damageStart=.1, damageDuration=.8,
            visualDuration=1.1, cooldown=1),
    }
    # Keep subsequent author edits to the demonstration on repeated runs.
    if not room.get("metadata", {}).get("delayedTrapDemo"):
        if set(added) & set(e):
            raise ValueError("Demo IDs already used; refusing to overwrite unrelated entities")
        backup = path.with_name(path.name + ".before_delayed_traps.bak")
        if not backup.exists():
            copy2(path, backup)
        e.update(added)
        room["metadata"]["delayedTrapDemo"] = "upper_west_gallery_v1"
        write(path, room)

    cases = []
    for name, x, wait in [("explosion", 12, 228), ("laser", 52, 240)]:
        cases.append({"name": f"wait then pass {name}", "room": "room_01", "spawn": "trap_demo",
            "position": [x, -26.1], "settleFrames": 1,
            "steps": [{"frames": wait}, {"move": 1, "frames": 140}],
            "expect": {"room": "room_01", "alive": True, "grounded": True,
                       "position": [x + 21, -26.1], "tolerance": [.3, .2]}})
        cases.append({"name": f"running into active {name} is lethal", "room": "room_01", "spawn": "trap_demo",
            "position": [x, -26.1], "settleFrames": 1,
            "steps": [{"move": 1, "frames": 160, "expect": {"alive": False}}],
            "expect": {"room": "room_01", "alive": False}})
    write(root / "DELAYED_TRAPS_INPUT_CHECK.json", {"format": "IWANNA_INPUT_REPLAY_1", "cases": cases})
    (root / "run_trap_demo.bat").write_text('@echo off\ncall "%~dp0run.bat" --room room_01 --spawn trap_demo %*\n', encoding="ascii")
    print(f"Trap demonstrations placed in the upper west gallery: {root}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("project", type=Path)
    install(p.parse_args().project.resolve())
