-- Non-blocking gameplay tasks. Each task owns one timer and resumes one phase
-- at a time; no thread, coroutine, busy wait, or room-specific code is needed.
local traps = {}
local timer_event = "lua_delayed_trap_task"
local tasks, busy = {}, {}
local sequence, active_count = 0, 0
local max_tasks = 24 -- leaves command/timer capacity for other room scripts

local defaults = {
    delayed_explosion = {
        prefab = "explosion_effect", width = 8, height = 8,
        delay = 0.8, damage_start = 0.08, damage_duration = 0.2,
        visual_duration = 1.0, cooldown = 1, sound = "trap_explosion",
    },
    delayed_laser = {
        prefab = "laser_effect", width = 3.2, height = 8,
        delay = 0.8, damage_start = 0.1, damage_duration = 0.8,
        visual_duration = 1.1, cooldown = 1, sound = "trap_laser",
    },
}

local function number(p, key, fallback, low, high)
    local value = p[key]
    if value == nil then value = fallback end
    assert(type(value) == "number" and value == value
        and value >= low and value <= high,
        "invalid delayed trap " .. key .. " (expected " .. low .. ".." .. high .. ")")
    return value
end

local function text(p, key, fallback, allow_empty)
    local value = p[key]
    if value == nil then value = fallback end
    assert(type(value) == "string" and #value <= 128
        and (allow_empty or #value > 0), "invalid delayed trap " .. key)
    return value
end

local function resume_after(ctx, task, phase, seconds)
    task.phase = phase
    ctx:after(seconds, timer_event, task.token)
end

local function exists(ctx, id)
    return ctx:get_position(id) ~= nil
end

local function remove_warning(ctx, task)
    if exists(ctx, task.warning) then ctx:destroy(task.warning) end
end

local function remove_effect(ctx, task)
    if exists(ctx, task.effect) then
        ctx:set_collision(task.effect, false)
        ctx:destroy(task.effect)
    end
end

local function release(task)
    tasks[task.token] = nil
    if busy[task.source] == task.token then busy[task.source] = nil end
    active_count = active_count - 1
end

local function finish_visual(ctx, task)
    remove_effect(ctx, task)
    resume_after(ctx, task, "rearm", task.cooldown)
end

-- State transitions are ordinary Lua functions. Adding another delayed
-- ability can reuse this queue and the same generic host entity operations.
local phases = {}

function phases.begin_effect(ctx, task)
    remove_warning(ctx, task)
    ctx:spawn(task.prefab, task.effect, task.x, task.y)
    ctx:set_collision(task.effect, false)
    ctx:set_visible(task.effect, true)
    ctx:set_size(task.effect, task.width, task.height)
    ctx:set_rotation(task.effect, task.rotation)
    ctx:restart_animation(task.effect)
    if task.sound ~= "" then ctx:sound(task.sound) end
    resume_after(ctx, task, "enable_damage", task.damage_start)
end

function phases.enable_damage(ctx, task)
    -- Other rules may destroy an effect early; stale work must be harmless.
    if not exists(ctx, task.effect) then
        resume_after(ctx, task, "rearm", task.cooldown)
        return
    end
    ctx:set_collision(task.effect, true)
    resume_after(ctx, task, "disable_damage", task.damage_duration)
end

function phases.disable_damage(ctx, task)
    if exists(ctx, task.effect) then ctx:set_collision(task.effect, false) end
    local tail = math.max(0, task.visual_duration - task.damage_start - task.damage_duration)
    resume_after(ctx, task, "finish_visual", tail)
end

phases.finish_visual = finish_visual
function phases.rearm(_, task) release(task) end

function traps.handles_trigger(name)
    return defaults[name] ~= nil
end

function traps.handles_timer(name)
    return name == timer_event
end

function traps.on_trigger(ctx, event)
    local fallback = defaults[event.name]
    if not fallback or event.phase ~= "enter" or busy[event.id] then return end
    -- Capacity exhaustion skips this activation instead of overflowing the
    -- host queue. Leaving and re-entering the trigger can try again later.
    if active_count >= max_tasks then return end
    local p = event.properties or {}
    local anchor = text(p, "effectAnchor", nil)
    local x, y = ctx:get_position(anchor)
    assert(x ~= nil, "missing delayed trap anchor: " .. anchor)
    local task = {
        source = event.id, x = x, y = y,
        prefab = text(p, "effectPrefab", fallback.prefab),
        sound = text(p, "sound", fallback.sound, true),
        width = number(p, "effectWidth", fallback.width, 0.1, 100),
        height = number(p, "effectHeight", fallback.height, 0.1, 100),
        rotation = number(p, "effectRotation", 0, -3600, 3600),
        delay = number(p, "delay", fallback.delay, 0, 60),
        damage_start = number(p, "damageStart", fallback.damage_start, 0, 60),
        damage_duration = number(p, "damageDuration", fallback.damage_duration, 0.01, 60),
        visual_duration = number(p, "visualDuration", fallback.visual_duration, 0.01, 60),
        cooldown = number(p, "cooldown", fallback.cooldown, 0, 60),
    }
    assert(task.visual_duration + 0.000001 >= task.damage_start + task.damage_duration,
        "delayed trap visualDuration must cover damageStart + damageDuration")
    sequence = sequence + 1
    task.token = tostring(sequence)
    task.effect = "delayed:" .. task.token .. ":effect"
    task.warning = "delayed:" .. task.token .. ":warning"
    tasks[task.token], busy[event.id] = task, task.token
    active_count = active_count + 1

    -- Snapshot the anchor at activation so the warning and subsequent damage
    -- share exactly the same position, even if another script moves it later.
    ctx:spawn("effect_warning", task.warning, task.x, task.y)
    ctx:set_collision(task.warning, false)
    ctx:set_visible(task.warning, true)
    ctx:set_size(task.warning, task.width, task.height)
    ctx:set_rotation(task.warning, task.rotation)
    resume_after(ctx, task, "begin_effect", task.delay)
end

function traps.on_timer(ctx, event)
    if event.name ~= timer_event then return end
    local task = tasks[event.id]
    if task then phases[task.phase](ctx, task) end
end

function traps.cancel_all(ctx)
    for _, task in pairs(tasks) do
        remove_warning(ctx, task)
        remove_effect(ctx, task)
    end
    -- Already queued timer messages become no-ops. Reset/change_room disposes
    -- the whole room VM, including its native timer queue, automatically.
    tasks, busy, active_count = {}, {}, 0
end

function traps.on_death(ctx) traps.cancel_all(ctx) end

return traps
