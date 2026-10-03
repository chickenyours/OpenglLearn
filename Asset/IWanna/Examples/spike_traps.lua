-- Reusable spike abilities implemented using generic entity/timer APIs only.
-- Coordinates are supplied by authored anchor entities, never room constants.
local traps = {}
local timer_event = "lua_spike_trap_task"
local step_seconds = 1 / 120
local max_tasks = 8
local tasks, busy, random_streams = {}, {}, {}
local sequence, active_count = 0, 0
local directions = {
    up = {x = 0, y = -1, rotation = 0},
    right = {x = 1, y = 0, rotation = 90},
    down = {x = 0, y = 1, rotation = 180},
    left = {x = -1, y = 0, rotation = -90},
}

local function number(p, name, fallback, low, high, integer)
    local v = p[name]
    if v == nil then v = fallback end
    assert(type(v) == "number" and v == v and v >= low and v <= high
        and (not integer or v % 1 == 0), "invalid spike trap " .. name)
    return v
end

local function text(p, name, fallback, empty)
    local v = p[name]
    if v == nil then v = fallback end
    assert(type(v) == "string" and #v <= 128 and (empty or #v > 0),
        "invalid spike trap " .. name)
    return v
end

local function exists(ctx, id) return ctx:get_position(id) ~= nil end

local function remove(ctx, id)
    if exists(ctx, id) then ctx:set_collision(id, false); ctx:destroy(id) end
end

local function resume_after(ctx, task, phase, seconds)
    task.phase = phase
    ctx:after(seconds, timer_event, task.token)
end

local function release(task)
    tasks[task.token] = nil
    if busy[task.source] == task.token then busy[task.source] = nil end
    active_count = active_count - 1
end

local function clean(ctx, task)
    remove(ctx, task.effect)
    remove(ctx, task.warning)
    for _, lane in ipairs(task.lanes or {}) do
        remove(ctx, lane.warning)
        remove(ctx, lane.shot)
    end
end

local function cooldown(ctx, task)
    clean(ctx, task)
    resume_after(ctx, task, "rearm", task.cooldown)
end

local function spawn_shape(ctx, task, id, prefab, x, y, width, length, collision)
    assert(math.abs(x) <= 990 and math.abs(y) <= 990, "spike trap spawn exceeds host coordinate range")
    ctx:spawn(prefab, id, x, y)
    ctx:set_size(id, width, length)
    ctx:set_rotation(id, task.direction.rotation)
    ctx:set_collision(id, collision)
end

local function center(task, length)
    return task.x + task.direction.x * length * 0.5,
        task.y + task.direction.y * length * 0.5
end

local function replace_effect(ctx, task, length)
    -- A single replacement at a phase boundary restores the exact base using
    -- spawn(x,y). This avoids cumulative drift without a set_position API.
    remove(ctx, task.effect)
    local x, y = center(task, length)
    spawn_shape(ctx, task, task.effect, task.prefab, x, y, task.thickness, length, true)
end

local function drive_next(ctx, task, x, y)
    task.frame = task.frame + 1
    local length = task.from + (task.to - task.from) * task.frame / task.frames
    local target_x, target_y = center(task, length)
    ctx:set_velocity(task.effect, (target_x - x) / task.dt, (target_y - y) / task.dt)
    resume_after(ctx, task, "tel_step", task.dt)
end

local function start_motion(ctx, task, from, to, duration, next_phase)
    task.from, task.to, task.next_phase = from, to, next_phase
    task.frames, task.frame = math.max(1, math.ceil(duration / step_seconds)), 0
    task.dt = duration / task.frames
    local x, y = center(task, from)
    drive_next(ctx, task, x, y)
end

local phases = {}

function phases.tel_begin(ctx, task)
    remove(ctx, task.warning)
    replace_effect(ctx, task, task.rest)
    if task.sound ~= "" then ctx:sound(task.sound) end
    start_motion(ctx, task, task.rest, task.length, task.extend, "hold")
end

function phases.tel_step(ctx, task)
    local x, y = ctx:get_position(task.effect)
    if x == nil then cooldown(ctx, task); return end
    if task.frame >= task.frames then
        if task.next_phase == "hold" then
            replace_effect(ctx, task, task.length)
            resume_after(ctx, task, "tel_retract", task.hold)
        else cooldown(ctx, task) end
        return
    end
    -- Read the actual center, so each visual update places the base back at
    -- the authored anchor. Between updates its error is at most one half-step
    -- of extension; the base should be inset slightly into the wall/floor.
    local projected = 2 * ((x - task.x) * task.direction.x + (y - task.y) * task.direction.y)
    ctx:set_size(task.effect, task.thickness, math.max(task.rest, math.min(task.length, projected)))
    drive_next(ctx, task, x, y)
end

function phases.tel_retract(ctx, task)
    if not exists(ctx, task.effect) then cooldown(ctx, task); return end
    start_motion(ctx, task, task.length, task.rest, task.retract, "done")
end

local function random_index(task, high)
    if task.seed == 0 then return math.random(high) end
    local stream = random_streams[task.source]
    if not stream or stream.seed ~= task.seed then
        stream = {seed = task.seed, value = task.seed}
        random_streams[task.source] = stream
    end
    stream.value = (stream.value * 48271) % 2147483647
    return math.floor(stream.value / 2147483647 * high) + 1
end

function phases.fly_fire(ctx, task)
    local player_x, player_y = ctx:get_position("player")
    for _, lane in ipairs(task.lanes) do
        remove(ctx, lane.warning)
        -- Recheck after warning. Walking close to a muzzle cannot cause an
        -- unavoidable spawn inside the player or immediately in their face.
        local ahead = player_x and ((player_x - lane.x) * task.direction.x
            + (player_y - lane.y) * task.direction.y) or -1
        if ahead >= math.max(task.min_spawn_distance, task.speed * 0.2) then
            spawn_shape(ctx, task, lane.shot, task.prefab, lane.x, lane.y,
                task.thickness, task.length, true)
            ctx:set_velocity(lane.shot, task.direction.x * task.speed, task.direction.y * task.speed)
        end
    end
    if task.sound ~= "" then ctx:sound(task.sound) end
    resume_after(ctx, task, "fly_finish", math.min(task.lifetime, task.flight_distance / task.speed))
end

function phases.fly_finish(ctx, task) cooldown(ctx, task) end
function phases.rearm(_, task) release(task) end

function traps.handles_trigger(name)
    return name == "telescopic_spike" or name == "flying_spikes"
end

function traps.handles_timer(name) return name == timer_event end

function traps.on_trigger(ctx, event)
    if event.phase ~= "enter" or not traps.handles_trigger(event.name)
        or busy[event.id] or active_count >= max_tasks then return end
    local p = event.properties or {}
    local anchor = text(p, "effectAnchor", event.id)
    if anchor == "$self" then anchor = event.id end
    local x, y = ctx:get_position(anchor)
    assert(x ~= nil, "missing spike trap anchor: " .. anchor)
    local orientation = text(p, "orientation", event.name == "flying_spikes" and "left" or "up")
    local direction = directions[orientation]
    assert(direction ~= nil, "spike trap orientation must be up/down/left/right")
    local horizontal = orientation == "left" or orientation == "right"
    local task = {
        source = event.id, direction = direction, x = x, y = y,
        prefab = text(p, "effectPrefab", "spike_trap_effect"),
        sound = text(p, "sound", "apple", true),
        thickness = number(p, "thickness", 1, 0.1, 8),
        length = number(p, "length", event.name == "flying_spikes" and 2.8 or 9, 0.25, 60),
        cooldown = number(p, "cooldown", 1, 0, 30),
    }
    if event.name == "telescopic_spike" then
        task.rest = number(p, "restLength", 0.25, 0.1, 5)
        assert(task.rest < task.length, "spike trap restLength must be less than length")
        task.extend = number(p, "extendSeconds", 0.18, 0.05, 3)
        task.hold = number(p, "holdSeconds", 0.15, 0, 5)
        task.retract = number(p, "retractSeconds", 0.35, 0.05, 3)
        task.delay = number(p, "delay", 0.25, 0, 10)
        -- Bound required center speed, including recovery from a late tick.
        assert((task.length - task.rest) / math.min(task.extend, task.retract) <= 600,
            "spike trap extension is too fast for its length")
    else
        assert(horizontal, "flying spikes support left/right orientation")
        task.lane_count = number(p, "laneCount", 5, 2, 12, true)
        task.lane_spacing = number(p, "laneSpacing", 4, 1.5, 12)
        task.count = number(p, "count", 2, 1, 4, true)
        assert(task.count < task.lane_count, "flying spikes must leave at least one empty lane")
        assert(task.thickness + 1 <= task.lane_spacing, "flying spike lanes need a safe gap")
        task.speed = number(p, "speed", 28, 1, 80)
        task.warning_seconds = number(p, "warningSeconds", 0.6, 0.25, 5)
        task.flight_distance = number(p, "flightDistance", 18, 2, 80)
        task.lifetime = number(p, "lifetime", 1.2, 0.05, 5)
        task.min_spawn_distance = number(p, "minSpawnDistance", 6, 2, 80)
        task.seed = number(p, "seed", 0, 0, 2147483646, true)
        assert(task.length <= 6, "flying spike length must not exceed 6")
    end
    sequence = sequence + 1
    task.token = tostring(sequence)
    task.effect = "spike:" .. task.token .. ":effect"
    task.warning = "spike:" .. task.token .. ":warning"
    tasks[task.token], busy[event.id] = task, task.token
    active_count = active_count + 1
    if event.name == "telescopic_spike" then
        local cx, cy = center(task, task.length)
        spawn_shape(ctx, task, task.warning, "spike_trap_warning", cx, cy,
            task.thickness, task.length, false)
        resume_after(ctx, task, "tel_begin", task.delay)
    else
        local pool = {}
        for lane = 0, task.lane_count - 1 do pool[#pool + 1] = lane end
        task.lanes = {}
        for _ = 1, task.count do
            local index = table.remove(pool, random_index(task, #pool))
            local lane = {x = x, y = y + index * task.lane_spacing,
                warning = "spike:" .. task.token .. ":warning:" .. index,
                shot = "spike:" .. task.token .. ":shot:" .. index}
            task.lanes[#task.lanes + 1] = lane
            spawn_shape(ctx, task, lane.warning, "spike_trap_warning",
                lane.x + direction.x * task.flight_distance * 0.5, lane.y,
                task.thickness * 0.65, task.flight_distance + task.length, false)
        end
        resume_after(ctx, task, "fly_fire", task.warning_seconds)
    end
end

function traps.on_timer(ctx, event)
    if event.name ~= timer_event then return end
    local task = tasks[event.id]
    if task then phases[task.phase](ctx, task) end
end

function traps.cancel_all(ctx)
    for _, task in pairs(tasks) do clean(ctx, task) end
    tasks, busy, active_count = {}, {}, 0
end

function traps.on_death(ctx) traps.cancel_all(ctx) end
return traps
