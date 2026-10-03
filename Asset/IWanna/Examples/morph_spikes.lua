-- Ordinary-looking lethal spikes. All transformations and sequencing are Lua.
-- The invisible trigger is independent of its precisely masked hazard child.
local morph = {}
local step_event, watch_event = "lua_morph_spike_step", "lua_morph_spike_watch"
local tick, max_tasks, max_controllers = 1 / 120, 8, 64
local controllers, tasks = {}, {}
local sequence, task_sequence, active_count, controller_count = 0, 0, 0, 0
local watching, stopped = false, false

-- {width multiplier, height multiplier, angle from authored rotation,
--  transition seconds, hold seconds, optional ordinary-form flag}.
-- A sound is emitted once at the start of each row, never on animation ticks.
local ordinary = {1, 1, 0, 0.28, 0, true}
local patterns = {
    vertical = {{1, 5, 0, .10, .16}, ordinary},
    horizontal = {{6, 1, 0, .10, .18}, ordinary},
    giant = {{3.6, 3.6, 0, .10, .20}, {1, 1, 0, .30, 0, true}},
    squash_pop = {{2.8, .35, 0, .08, .03}, {.8, 6, 0, .08, .18}, ordinary},
    cross = {{6, .65, 0, .10, .10}, {.7, 6, 0, .10, .18}, ordinary},
    stutter = {{1, 3, 0, .07, .06}, {1, 1, 0, .06, .05, true},
        {1, 6, 0, .08, .18}, ordinary},
    turn_spear = {{1, 1, -90, .07, 0}, {1, 7, -90, .12, .16},
        {1, 1, 0, .32, 0, true}},
    windmill = {{.75, 4, 0, .10, 0}, {.75, 4, 180, .42, 0},
        {.75, 4, 360, .42, 0}, {1, 1, 360, .26, 0, true}},
    diagonal_giant = {{2.5, 4, -45, .14, .20}, {1, 1, 0, .32, 0, true}},
    staircase_combo = {{3, .55, 0, .08, .07}, {.8, 4, 0, .10, .06},
        {1, 5, 90, .18, .12}, {2, 3, -90, .30, .18}, {1, 1, 0, .30, 0, true}},
}

local function number(p, name, fallback, low, high)
    local v = p[name]
    if v == nil then v = fallback end
    assert(type(v) == "number" and v == v and v >= low and v <= high,
        "invalid morph spike " .. name)
    return v
end

local function text(p, name, fallback, empty)
    local v = p[name]
    if v == nil then v = fallback end
    assert(type(v) == "string" and #v <= 128 and (empty or #v > 0),
        "invalid morph spike " .. name)
    return v
end

local function exists(ctx, id) return ctx:get_position(id) ~= nil end
local function remove(ctx, id)
    if exists(ctx, id) then ctx:set_collision(id, false); ctx:destroy(id) end
end

local function offset(width, height, angle, px, py)
    local radians = angle * math.pi / 180
    local x, y = width * px, height * py
    return x * math.cos(radians) - y * math.sin(radians),
        x * math.sin(radians) + y * math.cos(radians)
end

local function center(state, shape)
    local x, y = offset(shape.w, shape.h, shape.r, state.px, state.py)
    return state.root_x - x, state.root_y - y
end

local function update_root(ctx, state)
    local x, y = ctx:get_position(state.id)
    if x == nil then return false end
    local dx, dy = offset(state.w, state.h, state.rotation, state.px, state.py)
    state.root_x, state.root_y = x + dx, y + dy
    assert(math.abs(state.root_x) + state.radius <= 990
        and math.abs(state.root_y) + state.radius <= 990,
        "morph spike sweep exceeds host coordinate range")
    state.parent_x, state.parent_y = x, y
    return true
end

local function replace(ctx, state, shape)
    remove(ctx, state.effect)
    local x, y = center(state, shape)
    ctx:spawn(state.prefab, state.effect, x, y)
    ctx:set_size(state.effect, shape.w, shape.h)
    ctx:set_rotation(state.effect, shape.r)
    ctx:set_collision(state.effect, true)
    return x, y
end

local function release(task)
    if not tasks[task.token] then return end
    tasks[task.token] = nil
    if task.state.task == task.token then task.state.task = nil end
    active_count = active_count - 1
end

local function forget(ctx, state)
    if state.task then release(tasks[state.task]) end
    remove(ctx, state.effect)
    controllers[state.id] = nil
    controller_count = controller_count - 1
end

local function schedule(ctx, task, phase, seconds)
    task.phase = phase
    ctx:after(seconds, step_event, task.token)
end

local function cooldown(ctx, task)
    replace(ctx, task.state, task.state.idle)
    schedule(ctx, task, "cooldown", task.state.cooldown)
end

local function advance(ctx, task, x, y)
    task.frame = task.frame + 1
    local f, a, b = task.frame / task.stage.frames, task.from, task.stage
    local shape = {w = a.w + (b.w - a.w) * f, h = a.h + (b.h - a.h) * f,
        r = a.r + (b.r - a.r) * f}
    local target_x, target_y = center(task.state, shape)
    ctx:set_size(task.state.effect, shape.w, shape.h)
    ctx:set_rotation(task.state.effect, shape.r)
    ctx:set_velocity(task.state.effect, (target_x - x) / tick, (target_y - y) / tick)
    schedule(ctx, task, "step", tick)
end

local function begin_stage(ctx, task)
    task.index = task.index + 1
    local stage = task.state.stages[task.index]
    if not stage then cooldown(ctx, task); return end
    task.from = task.stage or task.state.idle
    task.stage, task.frame = stage, 0
    -- Rebuild exactly at the pivot at every phase boundary. This prevents
    -- numerical drift without adding a special-purpose C++ positioning API.
    local x, y = replace(ctx, task.state, task.from)
    if task.state.sound ~= "" then ctx:sound(task.state.sound) end
    advance(ctx, task, x, y) -- begin moving in this callback, with no warning
end

local function watch(ctx)
    watching = false
    if stopped then return end
    local missing, repairs = {}, 0
    for _, state in pairs(controllers) do
        local x, y = ctx:get_position(state.id)
        if x == nil then
            -- Tick() combines commands from all due timers. Bound cleanup as
            -- well as repairs so eight simultaneous phase changes still fit.
            if #missing < 8 then missing[#missing + 1] = state end
        elseif repairs < 8 and not state.task and (not exists(ctx, state.effect)
            or x ~= state.parent_x or y ~= state.parent_y) then
            update_root(ctx, state)
            replace(ctx, state, state.idle)
            repairs = repairs + 1
        end
    end
    for _, state in ipairs(missing) do forget(ctx, state) end
    if controller_count > 0 then
        watching = true
        ctx:after(.25, watch_event, "watch")
    end
end

function morph.handles_trigger(name) return name == "morph_spike" end
function morph.handles_timer(name) return name == step_event or name == watch_event end

function morph.on_entity_spawn(ctx, event)
    if stopped or not morph.handles_trigger(event.name) then return end
    -- The host emits this once for every newly created instance, including a
    -- destroy/spawn that reuses an existing stable ID. Keep the old state until
    -- the replacement has passed validation, then invalidate its timer token.
    local previous_state = controllers[event.id]
    local p = event.properties or {}
    local variant = text(p, "variant", "vertical")
    local pattern = patterns[variant]
    assert(pattern, "unknown morph spike variant: " .. variant)
    local x, y = ctx:get_position(event.id)
    if x == nil then return end
    local state = {id = event.id, variant = variant,
        w = number(p, "sizeX", 2.5, .1, 20), h = number(p, "sizeY", 2.5, .1, 20),
        rotation = (number(p, "rotation", 0, -3600, 3600) + 180) % 360 - 180,
        px = number(p, "pivotX", 0, -1, 1), py = number(p, "pivotY", .5, -1, 1),
        cooldown = number(p, "cooldown", 1.4, .05, 30),
        sound = text(p, "sound", "Block Change", true),
        prefab = text(p, "effectPrefab", "morph_spike_effect"), stages = {}, radius = 0}
    local sx, sy = number(p, "scaleX", 1, .25, 4), number(p, "scaleY", 1, .25, 4)
    local speed = number(p, "speedScale", 1, .25, 3)
    state.idle = {w = state.w, h = state.h, r = state.rotation}
    local previous = state.idle
    for _, value in ipairs(pattern) do
        local stage = {w = state.w * value[1] * (value[6] and 1 or sx),
            h = state.h * value[2] * (value[6] and 1 or sy), r = state.rotation + value[3],
            frames = math.max(2, math.ceil(value[4] / speed / tick)), hold = value[5] / speed}
        assert(stage.w <= 160 and stage.h <= 160, "morph spike transformed size exceeds 160")
        local radius = math.sqrt((stage.w * state.px)^2 + (stage.h * state.py)^2)
        local old_radius = math.sqrt((previous.w * state.px)^2 + (previous.h * state.py)^2)
        local distance = math.abs(stage.w - previous.w) * math.abs(state.px)
            + math.abs(stage.h - previous.h) * math.abs(state.py)
            + math.max(radius, old_radius) * math.abs(stage.r - previous.r) * math.pi / 180
        assert(distance / (stage.frames * tick) <= 650,
            "morph spike transform is too fast: reduce size/scale or speedScale")
        state.radius = math.max(state.radius, radius, old_radius)
        state.stages[#state.stages + 1], previous = stage, stage
    end
    assert(controller_count - (previous_state and 1 or 0) < max_controllers,
        "morph spike room limit is 64 controllers")
    update_root(ctx, state)
    -- IDs are reserved by this module; avoid any authored/dynamic collision.
    repeat sequence = sequence + 1; state.effect = "morph:" .. sequence .. ":effect"
    until not exists(ctx, state.effect)
    -- All numeric/range validation above completes before world mutations.
    if previous_state then forget(ctx, previous_state) end
    controllers[state.id], controller_count = state, controller_count + 1
    ctx:set_visible(state.id, false)
    replace(ctx, state, state.idle)
    if not watching then watching = true; ctx:after(.25, watch_event, "watch") end
end

function morph.on_trigger(ctx, event)
    if stopped or event.phase ~= "enter" or not morph.handles_trigger(event.name) then return end
    local state = controllers[event.id]
    if not state or state.task or active_count >= max_tasks then return end
    if not update_root(ctx, state) then return end
    task_sequence = task_sequence + 1
    local task = {token = tostring(task_sequence), state = state, index = 0}
    tasks[task.token], state.task = task, task.token
    active_count = active_count + 1
    begin_stage(ctx, task)
end

function morph.on_timer(ctx, event)
    if event.name == watch_event then watch(ctx); return end
    if event.name ~= step_event then return end
    local task = tasks[event.id]
    if not task then return end
    if not exists(ctx, task.state.id) then forget(ctx, task.state); return end
    if task.phase == "cooldown" then release(task); return end
    if task.phase == "hold" then begin_stage(ctx, task); return end
    local x, y = ctx:get_position(task.state.effect)
    if x == nil then cooldown(ctx, task); return end
    if task.frame >= task.stage.frames then
        if task.stage.hold > 0 then
            replace(ctx, task.state, task.stage)
            schedule(ctx, task, "hold", task.stage.hold)
        -- begin_stage already rebuilds the preceding exact shape. Do not
        -- enqueue a redundant destroy/spawn pair at a zero-hold boundary.
        else begin_stage(ctx, task) end
    else advance(ctx, task, x, y) end
end

function morph.cancel_all(ctx)
    stopped = true
    for _, state in pairs(controllers) do remove(ctx, state.effect) end
    controllers, tasks = {}, {}
    active_count, controller_count, watching = 0, 0, false
end

function morph.on_death(ctx) morph.cancel_all(ctx) end
return morph
