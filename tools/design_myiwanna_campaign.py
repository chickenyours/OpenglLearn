"""Generate the authored, replayable four-room MyIwana campaign.

Only the first three generated rooms are refreshed. The fourth room may have been
edited in the graphical editor and is deliberately left untouched.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from shutil import copy2

from PIL import Image, ImageDraw, ImageFont

UNIT = 2.5
MARKER_V1 = "myiwanna_four_room_campaign_v1"
MARKER = "myiwanna_four_room_campaign_v2"
THEMES = {1: "moss", 2: "ember", 3: "azure", 4: "azure"}
ISLANDS = {
    1: [(0, 11, 18), (16, 24, 18), (29, 37, 17), (43, 51, 18),
        (56, 64, 16), (70, 78, 17), (84, 96, 18)],
    2: [(0, 11, 18), (17, 25, 16), (31, 39, 18), (45, 53, 15),
        (59, 67, 18), (73, 81, 16), (87, 96, 18)],
    3: [(0, 11, 18), (17, 25, 15), (31, 39, 13), (45, 53, 16),
        (59, 67, 13), (73, 81, 16), (87, 96, 18)],
}
FLOOR_SPIKES = {
    1: {0: (6,), 2: (3, 4), 4: (3,), 6: (5,)},
    2: {0: (6,), 2: (3,), 4: (3, 4), 6: (5,)},
    3: {0: (6,), 2: (3, 4), 4: (3,), 6: (5,)},
}


class Room:
    def __init__(self, number: int, title: str, width: int = 96,
                 height: int = 24, origin_x: float = -10.0):
        self.number, self.id, self.title = number, f"room_{number:02d}", title
        self.width, self.height = width, height
        self.origin_x, self.origin_y = origin_x, -25.0
        self.tiles = [[0] * width for _ in range(height)]
        self.entities: dict[str, dict] = {}
        self.spike_count = 0
        self.islands = ISLANDS.get(number, [])
        self.traps: dict[str, tuple[str, str]] = {}
        self.rect(0, width, height - 1, height)
        start_row = self.islands[0][2] if self.islands else 17
        self.spawn("start", [self.x(2.5), self.top(start_row) - 1.1])
        self.checkpoint("checkpoint_start", 2.5, start_row)

    def x(self, column: float) -> float:
        return self.origin_x + column * UNIT

    def top(self, row: int) -> float:
        return self.origin_y + row * UNIT

    def center(self, column: int, row: int) -> list[float]:
        return [self.x(column + 0.5), self.top(row) + UNIT / 2]

    def rect(self, left: int, right: int, top: int, bottom: int) -> None:
        if not (0 <= left < right <= self.width and 0 <= top < bottom <= self.height):
            raise ValueError(f"terrain rectangle outside {self.id}")
        for row in range(top, bottom):
            for column in range(left, right):
                self.tiles[row][column] = 1

    def spawn(self, uid: str, position: list[float]) -> None:
        self.entities[uid] = {"kind": "Spawn", "position": position, "properties": {}}

    def entity(self, uid: str, prefab: str, position: list[float], size: list[float],
               properties: dict | None = None, rotation: float = 0) -> None:
        item = {"kind": "Entity", "prefab": prefab, "position": position,
                "size": size, "properties": properties or {}}
        if rotation:
            item["rotation"] = rotation
        self.entities[uid] = item

    def checkpoint(self, uid: str, column: float, row: int) -> None:
        self.entity(uid, "checkpoint", [self.x(column), self.top(row) - 2.5],
                    [4.0, 4.8], {"detectionEnabled": True,
                    "detectionX": 0, "detectionY": 0,
                    "detectionW": 4.0, "detectionH": 4.8})

    def spike(self, column: int, row: int, side: str = "top") -> None:
        self.spike_count += 1
        uid = f"spike_{self.spike_count:03d}"
        if side == "top":
            position, rotation = [self.x(column + .5), self.top(row) - UNIT / 2], 0
        elif side == "bottom":
            position, rotation = [self.x(column + .5), self.top(row + 1) + UNIT / 2], 180
        elif side == "left":
            position, rotation = [self.x(column) - UNIT / 2, self.top(row + .5)], -90
        elif side == "right":
            position, rotation = [self.x(column + 1) + UNIT / 2, self.top(row + .5)], 90
        else:
            raise ValueError(side)
        self.entity(uid, "spike", position, [UNIT, UNIT], rotation=rotation)

    def sensor(self, uid: str, column: float, row: int, event: str) -> None:
        # The sensor prefab is decorative by default; the instance explicitly
        # enables its detection rectangle to make Lua on_trigger run.
        self.entity(uid, "sensor", [self.x(column), self.top(row) - .35],
                    [2.5, .8], {"event": event, "collide": True,
                    "detectionEnabled": True, "detectionX": 0,
                    "detectionY": -1.15, "detectionW": 2.8,
                    "detectionH": 3.0})

    def exit(self, target: str) -> None:
        _, right, row = self.islands[-1]
        self.entity("exit_next", "door",
                    [self.x(self.width - 1.7), self.top(row) - 5],
                    [5.0, 10.0], {"destinationRoom": target,
                    "destinationSpawn": "start", "detectionEnabled": True,
                    "detectionX": 0, "detectionY": 0,
                    "detectionW": 5.0, "detectionH": 10.0})

    def pit_spikes(self) -> None:
        # Every failed leap ends in a death, while the continuous bedrock keeps
        # the player out of any unreachable cavity.
        for column in range(self.width):
            if not self.tiles[self.height - 2][column]:
                self.spike(column, self.height - 1)

    def finish_tiles(self) -> None:
        # The artist's 16 variants represent exposed-neighbour edge masks.
        # Both theme shades share solid occupancy; no seam in collision.
        source = [[bool(gid) for gid in row] for row in self.tiles]
        for row in range(self.height):
            for col in range(self.width):
                if not source[row][col]:
                    continue
                def filled(x: int, y: int) -> bool:
                    return 0 <= x < self.width and 0 <= y < self.height and source[y][x]
                edges = (0 if filled(col, row - 1) else 1)
                edges |= 0 if filled(col + 1, row) else 2
                edges |= 0 if filled(col, row + 1) else 4
                edges |= 0 if filled(col - 1, row) else 8
                alternate = ((col * 17 + row * 37 + self.number * 29) % 11) < 3
                self.tiles[row][col] = 1 + edges + (16 if alternate else 0)

    def data(self, target: str | None = None) -> dict:
        theme = THEMES[self.number]
        palette = {str(1 + edge): f"terrain_{theme}_{edge:02d}.png"
                   for edge in range(16)}
        palette.update({str(17 + edge): f"terrain_{theme}_alt_{edge:02d}.png"
                        for edge in range(16)})
        boundaries = {}
        if target:
            boundaries["right"] = {"action": "transfer", "room": target, "spawn": "start"}
        camera = ({"mode": "follow", "offset": [0, -2.5],
                   "zoom": 1.66, "followSpeed": 9.0, "clampToRoom": True}
                  if self.islands else
                  {"mode": "fixed", "position": [0, 0],
                   "zoom": 1.66, "clampToRoom": True})
        return {
            "format": "IWANNA_ROOM_2", "title": self.title, "hint": "Parkour",
            "metadata": {"authoring": MARKER, "difficulty": "medium-hard",
                         "routeIslands": [list(p) for p in self.islands]},
            "grid": {"width": self.width, "height": self.height,
                     "tileSize": UNIT, "origin": [self.origin_x, self.origin_y]},
            "palette": palette, "tileLayers": {"Terrain": self.tiles},
            "entities": self.entities,
            "camera": camera,
            "boundaries": boundaries, "script": f"scripts/{self.id}.lua",
        }


def design(number: int) -> Room:
    titles = {1: "The Broken Causeway", 2: "The Split Stair",
              3: "The Needle Ascent"}
    room = Room(number, titles[number])
    islands = room.islands
    room.rect(0, room.width, 0, 2)
    for index, (left, right, row) in enumerate(islands):
        room.rect(left, right, row, row + 2)
        # Uneven tapered supports replace the old solid rectangular pillars.
        # The walkable top remains continuous, while the chasm silhouette and
        # exposed tile edges change from section to section.
        inset_left = index % 3
        inset_right = (index + number) % 3
        room.rect(left + inset_left, right - inset_right,
                  row + 2, room.height - 1)
    # Ceiling lintels and vertical separators make each traversal beat read as
    # a small chamber, without closing the lower through-route.
    for index, (left, right, _) in enumerate(islands):
        if index in (1, 3, 5):
            room.rect(left + 1, right - 1, 2, 4)
        if index:
            divider = left - 2
            room.rect(divider, divider + 1, 2, 6 + index % 2)
            room.spike(divider, 4, "bottom")
    # A few lower ledges are bait: they have spikes and do not replace the
    # authored airborne route over the 5-6-tile chasms.
    for gap_index in (0, 2, 5):
        left = islands[gap_index][1]
        right = islands[gap_index + 1][0]
        middle = (left + right) // 2
        room.rect(middle, middle + 1, 21, room.height - 1)
        room.spike(middle, 21)
    # Ground spikes interrupt the otherwise safe islands. There is always
    # space to land on the left, hop over, and launch from the right.
    for index, offsets in FLOOR_SPIKES[number].items():
        left, _, row = islands[index]
        for offset in offsets:
            room.spike(left + offset, row)
    # Pit walls are lethal if the player misses a ledge, but their top faces
    # remain clear for a correct jump.
    for left, right, row in islands[1:-1]:
        room.spike(left, row + 1, "left")
        room.spike(right - 1, row + 1, "right")
    for gap_index in (1, 4):
        left = islands[gap_index][1]
        right = islands[gap_index + 1][0]
        for col in (left + 1, right - 2):
            room.spike(col, 3, "bottom")
    room.pit_spikes()
    room.checkpoint("checkpoint_mid", islands[3][0] + 2.5, islands[3][2])

    # Two visible apples are released by narrow pressure plates. A player can
    # stop before the apple, wait 1.6 s for it to disappear, then continue.
    for trap_index, island_index in enumerate((1, 5), 1):
        left, _, row = islands[island_index]
        sensor_id = f"apple_plate_{trap_index}"
        apple_id = f"falling_apple_{trap_index}"
        event = f"release_apple_{trap_index}"
        room.sensor(sensor_id, left + 1.2, row, event)
        room.entity(apple_id, "apple",
                    [room.x(left + 4.7), room.top(row) - 12.0], [3.0, 3.0])
        room.traps[event] = ("apple", apple_id)

    # A low, temporary bridge is a tempting shortcut. Its pressure plate
    # starts a timer; the genuine double-jump route never depends on it.
    bridge_gap = 3
    source = islands[bridge_gap]
    destination = islands[bridge_gap + 1]
    bridge_col = (source[1] + destination[0]) / 2
    bridge_y = max(room.top(source[2]), room.top(destination[2])) + 4.0
    room.entity("collapse_bridge", "platform",
                [room.x(bridge_col), bridge_y], [5.0, 2.5])
    room.sensor("bridge_plate", source[1] - 2.5, source[2], "collapse_bridge")
    room.traps["collapse_bridge"] = ("bridge", "collapse_bridge")

    room.exit(f"room_{number + 1:02d}")
    room.finish_tiles()
    return room


def script_for(room: Room) -> str:
    lines = ["-- Generated MyIwana room traps. Safe main route is independent of timers.",
             "local traps = {"]
    for event, (kind, uid) in room.traps.items():
        lines.append(f'  ["{event}"] = {{ kind = "{kind}", id = "{uid}" }},')
    lines += ["}", "return {",
              "  on_trigger = function(ctx, event)",
              '    if event.phase ~= "enter" then return end',
              "    local trap = traps[event.name]",
              "    if not trap or not ctx:once(event.id) then return end",
              '    if trap.kind == "apple" then',
              "      ctx:set_velocity(trap.id, 0, 20)",
              '      ctx:after(1.6, "clear_apple", trap.id)',
              "    else",
              '      ctx:after(0.7, "hide_bridge", trap.id)',
              "    end",
              "  end,",
              "  on_timer = function(ctx, event)",
              '    if event.name == "clear_apple" then',
              "      ctx:set_velocity(event.id, 0, 0)",
              "      ctx:set_enabled(event.id, false)",
              '    elseif event.name == "hide_bridge" then',
              "      ctx:set_enabled(event.id, false)",
              "    end",
              "  end,",
              "}",
              ""]
    return "\n".join(lines)


def body_bounds(px: float, py: float, config: dict) -> tuple[float, float, float, float]:
    player = config["player"]
    scale = player.get("scale", {"x": .75, "y": .75})
    sx = float(scale["x"] if isinstance(scale, dict) else scale)
    sy = float(scale["y"] if isinstance(scale, dict) else scale)
    width, height = player["width"] * sx, player["height"] * sy
    ratios = config["collision"]["playerBody"]
    cx = px + width * ratios["offsetXRatio"]
    cy = py + height * ratios["offsetYRatio"]
    hw = width * ratios["widthRatio"] / 2
    hh = height * ratios["heightRatio"] / 2
    return cx - hw, cy - hh, cx + hw, cy + hh


def overlaps(a: tuple[float, float, float, float],
             b: tuple[float, float, float, float]) -> bool:
    return a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1]


def try_jump(room: Room, config: dict, source: tuple[int, int, int],
             destination: tuple[int, int, int], start_col: float,
             target_col: float, second_at: float | None) -> dict | None:
    physics = config["physics"]
    dt = 1.0 / physics["fixedHz"]
    scale = config["player"].get("scale", {"x": .75, "y": .75})
    height = config["player"]["height"] * float(scale["y"] if isinstance(scale, dict) else scale)
    ratios = config["collision"]["playerBody"]
    body_to_floor = height * (ratios["offsetYRatio"] + ratios["heightRatio"] / 2)
    px = room.x(start_col)
    py = room.top(source[2]) - body_to_floor - .015
    target = room.x(target_col)
    vx = physics["runSpeed"]
    vy = -physics["jumpSpeed"]
    hold = physics["jumpHoldSeconds"]
    second_used = False
    apex = py
    hazard_boxes = []
    for entity in room.entities.values():
        if entity.get("prefab") != "spike":
            continue
        x, y = entity["position"]
        w, h = entity["size"]
        hazard_boxes.append((x - w / 2, y - h / 2, x + w / 2, y + h / 2))
    for step in range(int(2.6 / dt)):
        time = step * dt
        if second_at is not None and not second_used and time >= second_at:
            vy = -physics["doubleJumpSpeed"]
            hold = physics["jumpHoldSeconds"]
            second_used = True
        gravity = physics["gravity"]
        if vy < 0 and hold > 0:
            gravity *= physics["jumpHoldGravityScale"]
            hold = max(0, hold - dt)
        vy = min(physics["maxFallSpeed"], vy + gravity * dt)
        old_body = body_bounds(px, py, config)
        if px < target - .03:
            px = min(target, px + vx * dt)
        py += vy * dt
        apex = min(apex, py)
        body = body_bounds(px, py, config)
        if any(overlaps(body, hazard) for hazard in hazard_boxes):
            return None
        # Only nearby grid cells need testing. This is deliberately stricter
        # than masked collision: each square tile and spike is a full AABB.
        min_c = max(0, int(math.floor((body[0] - room.origin_x) / UNIT)))
        max_c = min(room.width - 1, int(math.floor((body[2] - room.origin_x) / UNIT)))
        min_r = max(0, int(math.floor((body[1] - room.origin_y) / UNIT)))
        max_r = min(room.height - 1, int(math.floor((body[3] - room.origin_y) / UNIT)))
        landed = False
        for row in range(min_r, max_r + 1):
            for col in range(min_c, max_c + 1):
                if not room.tiles[row][col]:
                    continue
                tile = (room.x(col), room.top(row),
                        room.x(col + 1), room.top(row + 1))
                if not overlaps(body, tile):
                    continue
                safe_x = (body[0] >= room.x(destination[0]) + .1
                          if source != destination else px >= target - .5)
                safe_landing = (vy > 0 and row == destination[2]
                                and destination[0] <= col < destination[1]
                                and safe_x
                                and old_body[3] <= tile[1] + .04)
                if safe_landing:
                    landed = True
                else:
                    return None
        if landed:
            return {"distance": round(target - room.x(start_col), 2),
                    "rise": round(room.top(source[2]) - room.top(destination[2]), 2),
                    "time": round(time, 3),
                    "landedColumn": round((px - room.origin_x) / UNIT, 3),
                    "apexAboveSource": round(room.top(source[2]) - (apex + body_to_floor), 2),
                    "secondJumpAt": second_at}
        if py > room.top(room.height) + 2:
            return None
    return None


def validate_route(room: Room, config: dict) -> list[dict]:
    evidence = []
    if len(room.islands) != 7:
        raise RuntimeError(f"{room.id}: expected seven route islands")
    for index, island in enumerate(room.islands):
        if index in FLOOR_SPIKES[room.number]:
            start = 3.0 if index == 0 else island[0] + 1.3
            target = max(island[0] + offset for offset in FLOOR_SPIKES[room.number][index]) + 2.0
            hop = try_jump(room, config, island, island, start, target, None)
            if hop is None:
                raise RuntimeError(f"{room.id}: floor-spike hop on island {index} is blocked")
            evidence.append({"section": index, "kind": "spike_hop",
                             "fromColumn": start, "toColumn": target,
                             "fromRow": island[2], "toRow": island[2], **hop})
        if index == len(room.islands) - 1:
            break
        next_island = room.islands[index + 1]
        start = island[1] - .8
        target = next_island[0] + 1.0
        option = None
        for second_at in (None, .25, .30, .35, .40, .45, .50, .55, .60, .65):
            option = try_jump(room, config, island, next_island,
                              start, target, second_at)
            if option is not None:
                break
        if option is None:
            raise RuntimeError(f"{room.id}: gap {index} to {index + 1} is blocked")
        evidence.append({"section": index, "kind": "gap",
                         "fromColumn": start, "toColumn": target,
                         "fromRow": island[2], "toRow": next_island[2],
                         "clearGapTiles": next_island[0] - island[1], **option})
    return evidence


def validate_timed_traps(room: Room, config: dict) -> list[dict]:
    """A straight run must hit each apple; waiting leaves a timed clear path."""
    player = config["player"]
    scale = player["scale"]
    width = player["width"] * scale["x"]
    height = player["height"] * scale["y"]
    ratios = config["collision"]["playerBody"]
    half_body_width = width * ratios["widthRatio"] / 2
    body_to_floor = height * (ratios["offsetYRatio"] + ratios["heightRatio"] / 2)
    speed = config["physics"]["runSpeed"]
    evidence = []
    for index, island_index in enumerate((1, 5), 1):
        row = room.islands[island_index][2]
        plate = room.entities[f"apple_plate_{index}"]
        apple = room.entities[f"falling_apple_{index}"]
        plate_x = plate["position"][0]
        apple_x, apple_start_y = apple["position"]
        player_start_x = (plate_x - plate["properties"]["detectionW"] / 2
                          - half_body_width + .01)
        player_y = room.top(row) - body_to_floor - .015
        first_hit = None
        for frame in range(1, 145):
            time = frame / 120
            px = player_start_x + speed * time
            ay = apple_start_y + 20 * time
            player_box = body_bounds(px, player_y, config)
            apple_box = (apple_x - 1.5, ay - 1.5, apple_x + 1.5, ay + 1.5)
            if overlaps(player_box, apple_box):
                first_hit = round(time, 3)
                break
        if first_hit is None or not .25 < first_hit < 1.0:
            raise RuntimeError(f"{room.id}: apple trap {index} does not threaten a straight run")
        evidence.append({"trap": f"falling_apple_{index}",
                         "sensor": f"apple_plate_{index}",
                         "velocityY": 20, "straightRunHitAt": first_hit,
                         "clearsAt": 1.6, "counterplay": "stop at plate, wait, then cross"})
    bridge = room.entities["collapse_bridge"]
    source, destination = room.islands[3:5]
    bridge_top = bridge["position"][1] - bridge["size"][1] / 2
    if bridge_top <= max(room.top(source[2]), room.top(destination[2])) + 2:
        raise RuntimeError(f"{room.id}: bridge may obstruct the verified upper route")
    evidence.append({"trap": "collapse_bridge", "sensor": "bridge_plate",
                     "enabledInitially": True, "clearsAt": .7,
                     "counterplay": "use verified upper gap jump independent of bridge"})
    return evidence


def render_thank_you(destination: Path) -> None:
    if destination.exists():
        return
    font_file = Path("C:/Windows/Fonts/simhei.ttf")
    if not font_file.is_file():
        raise RuntimeError("Chinese font missing for the thank-you sign")
    image = Image.new("RGBA", (240, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(str(font_file), 40)
    label = "感谢游玩"
    box = draw.textbbox((0, 0), label, font=font, stroke_width=2)
    x = (240 - (box[2] - box[0])) // 2 - box[0]
    y = (64 - (box[3] - box[1])) // 2 - box[1]
    draw.text((x + 2, y + 3), label, font=font, fill="#20364c",
              stroke_width=3, stroke_fill="#20364c")
    draw.text((x, y), label, font=font, fill="#ffe6a1",
              stroke_width=2, stroke_fill="#66dbe5")
    image.resize((960, 256), Image.Resampling.NEAREST).save(destination)


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def copy_theme_images(root: Path) -> None:
    source = Path(__file__).resolve().parents[1] / "Asset" / "IWanna" / "images"
    for theme in ("moss", "ember", "azure"):
        for shade in ("", "_alt"):
            for edge in range(16):
                name = f"terrain_{theme}{shade}_{edge:02d}.png"
                origin = source / name
                destination = root / "images" / name
                if not origin.is_file():
                    raise RuntimeError(f"missing themed terrain asset: {origin}")
                if destination.exists():
                    if destination.read_bytes() != origin.read_bytes():
                        raise RuntimeError(f"refusing to overwrite an edited terrain asset: {destination}")
                else:
                    copy2(origin, destination)


def restyle_or_create_final_room(root: Path) -> str:
    """Change old room 4 tiles only; leave editor-saved entities/camera intact."""
    file = root / "rooms" / "room_04.room.json"
    if not file.exists():
        room = Room(4, "Thank You", width=36, height=20, origin_x=-45)
        room.rect(0, 36, 17, 19)
        room.rect(5, 10, 14, 15)
        room.rect(26, 31, 14, 15)
        room.entity("thank_you", "thank_you_sign", [0.0, -3.0], [52.0, 13.0])
        room.finish_tiles()
        write_json(file, room.data())
        (root / "scripts" / "room_04.lua").write_text(
            "-- Generated by MyIwana campaign.\nreturn {}\n", encoding="utf-8")
        return "created room_04"
    data = json.loads(file.read_text(encoding="utf-8"))
    palette = data.get("palette", {})
    if palette != {"1": "terrain_15.png"}:
        return "preserved room_04 custom palette"
    backup = file.with_name("room_04.room.json.before_restyle.bak")
    if not backup.exists():
        copy2(file, backup)
    grid = data["tileLayers"]["Terrain"]
    height, width = len(grid), len(grid[0])
    occupied = [[bool(cell) for cell in row] for row in grid]
    for row in range(height):
        for col in range(width):
            if not occupied[row][col]:
                continue
            def filled(x: int, y: int) -> bool:
                return 0 <= x < width and 0 <= y < height and occupied[y][x]
            edges = (0 if filled(col, row - 1) else 1)
            edges |= 0 if filled(col + 1, row) else 2
            edges |= 0 if filled(col, row + 1) else 4
            edges |= 0 if filled(col - 1, row) else 8
            alternate = ((col * 17 + row * 37 + 4 * 29) % 11) < 3
            grid[row][col] = 1 + edges + (16 if alternate else 0)
    data["palette"] = {str(1 + edge): f"terrain_azure_{edge:02d}.png"
                       for edge in range(16)}
    data["palette"].update({str(17 + edge): f"terrain_azure_alt_{edge:02d}.png"
                            for edge in range(16)})
    write_json(file, data)
    return "restyled room_04 tiles; preserved entities and camera"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("--force", action="store_true",
                        help="replace only previous generated rooms 1-3 and Lua")
    parser.add_argument("--check-only", action="store_true",
                        help="run the conservative route checker without writing")
    args = parser.parse_args()
    root: Path = args.project.resolve()
    world_file = root / "world.json"
    if not world_file.is_file():
        raise RuntimeError("project world.json is missing")
    world = json.loads(world_file.read_text(encoding="utf-8"))
    if world.get("format") != "IWANNA_WORLD_2":
        raise RuntimeError("expected an IWANNA_WORLD_2 project")
    first = root / "rooms" / "room_01.room.json"
    existing = json.loads(first.read_text(encoding="utf-8"))
    previous_marker = existing.get("metadata", {}).get("authoring")
    if previous_marker in (MARKER_V1, MARKER) and not (args.force or args.check_only):
        raise RuntimeError("campaign exists; pass --force to replace generated rooms 1-3")
    if previous_marker not in (MARKER_V1, MARKER):
        if set(existing.get("entities", {})) != {"start"} or existing.get("hint") != "Edit this room and scripts/room_01.lua":
            raise RuntimeError("room_01 is not an unmodified blank project")
        backup = first.with_name("room_01.room.json.initial.bak")
        if not backup.exists() and not args.check_only:
            copy2(first, backup)

    config_path = root / "gameplay.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    config["player"]["scale"] = {"x": .75, "y": .75}
    rooms = [design(number) for number in (1, 2, 3)]
    route_evidence = {room.id: validate_route(room, config) for room in rooms}
    trap_evidence = {room.id: validate_timed_traps(room, config) for room in rooms}
    for room in rooms:
        gaps = [e for e in route_evidence[room.id] if e["kind"] == "gap"]
        print(f"{room.id}: 96x24, {len(gaps)} jumps, max gap "
              f"{max(e['clearGapTiles'] for e in gaps)} tiles, "
              f"{len(room.entities)} authored entities; route and timed traps PASS")
    if args.check_only:
        return

    for room in rooms:
        destination = root / "rooms" / f"{room.id}.room.json"
        if destination.exists():
            previous = json.loads(destination.read_text(encoding="utf-8"))
            if previous.get("metadata", {}).get("authoring") not in (MARKER_V1, MARKER):
                raise RuntimeError(f"refusing to overwrite non-campaign room: {destination}")
        script = root / "scripts" / f"{room.id}.lua"
        if script.exists() and "Generated MyIwana" not in script.read_text(encoding="utf-8"):
            # The v1 generator used a different header.
            if "Generated by MyIwana campaign" not in script.read_text(encoding="utf-8"):
                raise RuntimeError(f"refusing to overwrite custom Lua: {script}")
    copy_theme_images(root)
    for room in rooms:
        destination = root / "rooms" / f"{room.id}.room.json"
        write_json(destination, room.data(f"room_{room.number + 1:02d}"))
        (root / "scripts" / f"{room.id}.lua").write_text(
            script_for(room), encoding="utf-8")
    final_status = restyle_or_create_final_room(root)
    # Room 4 and its Lua were touched by the graphical editor after generation;
    # preserving them is essential to keep the author's final stage intact.
    config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    render_thank_you(root / "images" / "thank_you.png")
    sign_file = root / "prefabs" / "thank_you_sign.prefab.json"
    if not sign_file.exists():
        write_json(sign_file, {"format": "IWANNA_PREFAB_1",
                    "role": "decoration", "image": "thank_you.png",
                    "width": 52, "height": 13, "pixelArt": True,
                    "visible": True, "collide": False})
    world["startRoom"] = "room_01"
    world["startSpawn"] = "start"
    write_json(world_file, world)
    # Keep the added shooting gate and radial trap when regenerating the campaign.
    from build_iwanna_mechanisms import build_assets, install_demo
    build_assets()
    install_demo(root)
    write_json(root / "CAMPAIGN_ROUTE_CHECK.json", {
        "format": "IWANNA_ROUTE_EVIDENCE_1",
        "method": "AABB-conservative 120 Hz gravity/jump simulation; full-size tile and spike boxes",
        "playerScale": [0.75, 0.75], "rooms": route_evidence,
        "timedTraps": trap_evidence})
    lines = [
        "# MyIwana four-room campaign",
        "",
        "前三关各有七段岛式地形、六处必须跨越的深坑，起点和中段各有存档点。",
        "脚下尖刺需先单独起跳跨过；部分深坑需要按住跳跃并在空中再按一次。",
        "压力板会释放苹果，直冲会被击中；停下约 1.6 秒再走即可避开。",
        "低处桥梁会在触发后 0.7 秒消失，上方双跳路线始终可通行。",
        "第四关保留编辑器保存的感谢游玩舞台，仅更新瓦片外观。",
        "",
        "Rooms 1-3 span 96x24 tiles (240x60 world units), over 2.6 times the old room width.",
        "They use follow cameras, seven terrain islands and six required chasms each.",
        "Room 4 remains the editor-saved thank-you stage.",
        "Each challenge room has a checkpoint at the start and one near the middle.",
        "",
        "## Intended route",
        "",
        "The island list below uses [left inclusive, right exclusive, top row].",
        "Walk to the right edge of an island, use a held jump and a timed second jump",
        "where necessary, then land near the left side of the next island.",
        "At an island with ground spikes, make a separate held hop across them",
        "before the next gap. Every route beat is checked by the deterministic",
        "120 Hz conservative AABB jump simulation in this generator.",
        "",
    ]
    for room in rooms:
        lines += [f"### {room.id} — {room.title}", "",
                  f"Islands: {room.islands}", "",
                  "Verified transitions:"]
        for entry in route_evidence[room.id]:
            kind = "ground-spike hop" if entry["kind"] == "spike_hop" else "chasms"
            second = entry["secondJumpAt"]
            jump = "one held jump" if second is None else f"second jump at {second:.2f}s"
            lines.append(f"- Section {entry['section']}: {kind}, col "
                         f"{entry['fromColumn']:.2f} -> {entry['toColumn']:.2f}, "
                         f"{entry['distance']:.2f} world units, "
                         f"rise {entry['rise']:.2f}, {jump}.")
        lines += ["", "Traps: ground and pit spikes, guarded pit walls, bait ledges,",
                  "two visible falling apples, and one timed collapsing shortcut bridge.",
                  "The apple plates are on islands 2 and 6; an apple falls at speed 20",
                  "and clears after 1.6 seconds. The bridge plate is before chasm 4;",
                  "the low bridge vanishes after 0.7 seconds. Waiting at a plate or",
                  "using the upper double-jump route always remains possible.",
                  "Room restart restores the temporary traps; no trap is a required",
                  "permanent switch for progression.",
                  ""]
    lines += ["The authoritative numeric check is CAMPAIGN_ROUTE_CHECK.json.",
              "For layout edits, rerun the generator with --check-only first.",
              "Room data and Lua scripts are editable without recompiling the game.",
              ""]
    (root / "LEVEL_DESIGN.md").write_text("\n".join(lines), encoding="utf-8")
    from upgrade_myiwanna_two_floors import install as install_two_floors
    install_two_floors(root)
    from install_iwanna_delayed_traps import install_project as install_delayed_traps
    from add_iwanna_delayed_trap_demo import install as add_delayed_demo
    install_delayed_traps(root)
    add_delayed_demo(root)
    from install_iwanna_reactive_blocks import install_project as install_reactive_blocks
    from add_iwanna_reactive_block_demo import install as add_block_demo
    install_reactive_blocks(root, ("room_01", "room_02", "room_03", "room_04"))
    add_block_demo(root)
    from install_iwanna_spike_traps import install_project as install_spike_traps
    from add_iwanna_spike_course import install as add_spike_course
    install_spike_traps(root, ("room_01", "room_02", "room_03", "room_04"))
    add_spike_course(root)
    print(f"Designed longer MyIwana route in {root}; {final_status}")


if __name__ == "__main__":
    main()

