-- Contact-driven gameplay rules. Hidden walls remain physical while invisible;
-- broken blocks immediately stop blocking, then animate entirely through Lua.
local blocks = {}
local step_event = "lua_reactive_block_step"
local finish_event = "lua_reactive_block_finish"
local revealed, broken, tasks = {}, {}, {}
local sequence, active_count = 0, 0
local max_tasks = 16 -- at most 32 pending timers, leaving room for other rules

local function number(p, key, fallback, low, high)
    local value = p[key]
    if value == nil then value = fallback end
    assert(type(value) == "number" and value == value
        and value >= low and value <= high,
        "invalid reactive block " .. key .. " (expected " .. low .. ".." .. high .. ")")
    return value
end

local function sound_name(p, fallback)
    local value = p.sound
    if value == nil then value = fallback end
    assert(type(value) == "string" and #value <= 128, "invalid reactive block sound")
    return value
end

local function exists(ctx, id)
    return ctx:get_position(id) ~= nil
end

local function finish(ctx, token)
    local task = tasks[token]
    if not task then return end
    if exists(ctx, task.id) then
        ctx:set_collision(task.id, false)
        ctx:set_opacity(task.id, 0)
        ctx:destroy(task.id)
    end
    tasks[token] = nil
    active_count = active_count - 1
end

local function schedule_step(ctx, task)
    task.next_step = math.min(task.step, task.duration - task.elapsed)
    ctx:after(task.next_step, step_event, task.token)
end

function blocks.handles_trigger(name)
    return name == "reveal_hidden_wall" or name == "break_trap_block"
end

function blocks.handles_timer(name)
    return name == step_event or name == finish_event
end

function blocks.on_trigger(ctx, event)
    if event.phase ~= "enter" or not blocks.handles_trigger(event.name)
        or not exists(ctx, event.id) then return end
    local p = event.properties or {}
    if event.name == "reveal_hidden_wall" then
        if revealed[event.id] then return end
        local sound = sound_name(p, "Block Change")
        revealed[event.id] = true
        ctx:set_visible(event.id, true)
        if sound ~= "" then ctx:sound(sound) end
        return
    end
    if broken[event.id] then return end
    local duration = number(p, "duration", 0.5, 0.05, 5)
    local pop_min = number(p, "popMin", 5, 0, 40)
    local pop_max = number(p, "popMax", 7, 0, 40)
    local drift_min = number(p, "driftMin", 1, 0, 40)
    local drift_max = number(p, "driftMax", 3, 0, 40)
    local gravity = number(p, "gravity", 45, 0, 200)
    local step = number(p, "step", 1 / 60, 1 / 120, 0.1)
    local opacity = number(p, "opacity", 1, 0, 1)
    local sound = sound_name(p, "Break")
    assert(pop_min <= pop_max, "reactive block popMin must not exceed popMax")
    assert(drift_min <= drift_max, "reactive block driftMin must not exceed driftMax")

    broken[event.id] = true
    ctx:set_collision(event.id, false) -- physical support disappears immediately
    if sound ~= "" then ctx:sound(sound) end
    if active_count >= max_tasks then
        -- Overload cannot leave a permanent blocker or overflow native timers.
        ctx:destroy(event.id)
        return
    end
    sequence = sequence + 1
    local sign = math.random(0, 1) == 0 and -1 or 1
    local task = {
        id = event.id, token = tostring(sequence), duration = duration,
        elapsed = 0, step = step, opacity = opacity, gravity = gravity,
        dx = sign * (drift_min + math.random() * (drift_max - drift_min)),
        pop = pop_min + math.random() * (pop_max - pop_min),
    }
    tasks[task.token] = task
    active_count = active_count + 1
    ctx:set_opacity(task.id, task.opacity)
    ctx:set_velocity(task.id, task.dx, -task.pop)
    -- The independent deadline bounds lifetime even if visual step callbacks
    -- are rounded to the next physics tick. Only two timers exist per task.
    ctx:after(task.duration, finish_event, task.token)
    schedule_step(ctx, task)
end

function blocks.on_timer(ctx, event)
    if not blocks.handles_timer(event.name) then return end
    local task = tasks[event.id]
    if not task then return end -- cancelled or already finished timer
    if event.name == finish_event then finish(ctx, event.id); return end
    if not exists(ctx, task.id) then
        -- Keep the slot until its already queued deadline is consumed. Rapid
        -- external removals must not accumulate unbounded orphan timers.
        return
    end
    task.elapsed = math.min(task.duration, task.elapsed + task.next_step)
    if task.elapsed >= task.duration then finish(ctx, event.id); return end
    ctx:set_velocity(task.id, task.dx, -task.pop + task.gravity * task.elapsed)
    ctx:set_opacity(task.id, task.opacity * (1 - task.elapsed / task.duration))
    schedule_step(ctx, task)
end

function blocks.cancel_all(ctx)
    -- Keep already revealed walls as they are during the death presentation.
    -- Respawning or changing rooms recreates both the scene and this Lua VM.
    local tokens = {}
    for token in pairs(tasks) do tokens[#tokens + 1] = token end
    for _, token in ipairs(tokens) do finish(ctx, token) end
end

function blocks.on_death(ctx) blocks.cancel_all(ctx) end

return blocks
