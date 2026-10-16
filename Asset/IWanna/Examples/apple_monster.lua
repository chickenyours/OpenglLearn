-- Enemy brains, health, aimed shots and the attached pixel health bar are Lua.
local monster = {}
local update_event = "lua_apple_monster_update"
local dt, max_actors, max_bullets = 1 / 30, 4, 32
local actors, tasks, pending, used = {}, {}, {}, {}
local sequence, active, reserved, bullet_count, stopped = 0, 0, 0, 0, false

local function number(p, key, fallback, low, high, integer)
    local value = p[key]
    if value == nil then value = fallback end
    assert(type(value) == "number" and value == value and value >= low and value <= high
        and (not integer or value % 1 == 0), "invalid apple monster " .. key)
    return value
end

local function text(p, key, fallback, empty)
    local value = p[key]
    if value == nil then value = fallback end
    assert(type(value) == "string" and #value <= 128 and (empty or #value > 0),
        "invalid apple monster " .. key)
    return value
end

local function config(p)
    local c = {
        hp = number(p, "health", 5, 1, 50, true),
        size = number(p, "monsterSize", 6, 2, 12),
        speed = number(p, "wanderSpeed", 6, 0, 20),
        radius = number(p, "wanderRadius", 14, 6, 30),
        height = number(p, "wanderHeight", 3, 0, 12),
        hover = number(p, "hoverOffset", 3, 0, 20),
        retarget = number(p, "retargetSeconds", 1.1, .25, 5),
        shot_interval = number(p, "shotInterval", 1.4, .4, 10),
        first_shot = number(p, "firstShotDelay", 1, .25, 10),
        shot_speed = number(p, "shotSpeed", 20, 1, 50),
        shot_size = number(p, "shotSize", 1.4, .5, 3),
        lifetime = number(p, "shotLifetime", 4, .25, 6),
        spawn_min = number(p, "spawnRadiusMin", 12, 6, 30),
        spawn_max = number(p, "spawnRadiusMax", 18, 6, 40),
        min_x = number(p, "arenaMinX", -970, -970, 970),
        max_x = number(p, "arenaMaxX", 970, -970, 970),
        min_y = number(p, "arenaMinY", -970, -970, 970),
        max_y = number(p, "arenaMaxY", 970, -970, 970),
        bar_width = number(p, "barWidth", 8, 3, 16),
        bar_height = number(p, "barHeight", .65, .3, 1.5),
        bar_offset = number(p, "barOffset", 4.5, 2, 12),
        seed = number(p, "seed", 0, 0, 2147483646, true),
        actor_prefab = text(p, "monsterPrefab", "apple_monster_actor"),
        shot_prefab = text(p, "shotPrefab", "apple_monster_shot"),
        spawn_sound = text(p, "spawnSound", "apple", true),
        shot_sound = text(p, "shotSound", "apple", true),
        hit_sound = text(p, "hitSound", "Block Change", true),
        death_sound = text(p, "deathSound", "Break", true),
    }
    assert(c.min_x < c.max_x and c.min_y < c.max_y, "apple monster arena must have positive extent")
    assert(c.spawn_min <= c.spawn_max, "apple monster spawn radius range is reversed")
    return c
end

local function random(c)
    if c.seed == 0 then return math.random() end
    c.random = ((c.random or c.seed) * 48271) % 2147483647
    return c.random / 2147483647
end

local function clamp(v, low, high) return math.max(low, math.min(high, v)) end
local function exists(ctx, id) return ctx:get_position(id) ~= nil end
local function remove(ctx, id)
    if exists(ctx, id) then ctx:set_collision(id, false); ctx:destroy(id) end
end
local function sound(ctx, name) if name ~= "" then ctx:sound(name) end end

local function drop(ctx, state, keep_actor)
    if actors[state.id] ~= state then return end
    if not keep_actor then remove(ctx, state.id) end
    for _, id in ipairs(state.bars) do remove(ctx, id) end
    for id in pairs(state.bullets) do remove(ctx, id); bullet_count = bullet_count - 1 end
    actors[state.id], tasks[state.token] = nil, nil
    active = active - 1
end

local function bar_position(state, x, y, fill)
    if fill then x = x - state.c.bar_width * .5 + state.c.bar_width * state.hp / state.c.hp * .5 end
    return x, y - state.c.bar_offset
end

local function make_bar(ctx, state, x, y)
    local c = state.c
    for index, prefab in ipairs({"apple_health_frame", "apple_health_empty", "apple_health_fill"}) do
        local bx, by = bar_position(state, x, y, index == 3)
        ctx:spawn(prefab, state.bars[index], bx, by)
        ctx:set_size(state.bars[index], index == 1 and c.bar_width+.35 or c.bar_width,
            index == 1 and c.bar_height+.35 or c.bar_height)
    end
end

local function update_fill(ctx, state)
    local x, y = ctx:get_position(state.id)
    if x == nil then return end
    remove(ctx, state.bars[3])
    local bx, by = bar_position(state, x, y, true)
    ctx:spawn("apple_health_fill", state.bars[3], bx, by)
    ctx:set_size(state.bars[3], state.c.bar_width * state.hp / state.c.hp, state.c.bar_height)
    ctx:set_velocity(state.bars[3], state.vx, state.vy)
end

local function choose_goal(state, px, py)
    local c, angle = state.c, random(state.c) * math.pi * 2
    state.goal_x = clamp(px + math.cos(angle) * c.radius, c.min_x, c.max_x)
    state.goal_y = clamp(py - c.hover + math.sin(angle) * c.height, c.min_y, c.max_y)
    state.next_goal = state.time + c.retarget
end

local function fire(ctx, state, x, y, px, py)
    if bullet_count >= max_bullets then return end
    local dx, dy = px-x, py-y
    local length = math.sqrt(dx*dx+dy*dy)
    local muzzle = state.c.size * .5 + state.c.shot_size * .5 + .4
    -- Never create a damaging apple inside a player who moved into the muzzle.
    if length <= muzzle + 1.5 then return end
    dx, dy = dx / length, dy / length
    state.shot_serial = state.shot_serial + 1
    local id = "apple_monster:" .. state.token .. ":shot:" .. state.shot_serial
    ctx:spawn(state.c.shot_prefab, id, x+dx*muzzle, y+dy*muzzle)
    ctx:set_size(id, state.c.shot_size, state.c.shot_size)
    ctx:set_velocity(id, dx*state.c.shot_speed, dy*state.c.shot_speed)
    state.bullets[id], bullet_count = state.time + state.c.lifetime, bullet_count + 1
    sound(ctx, state.c.shot_sound)
end

function monster.handles_trigger(name) return name == "summon_apple_monster" end
function monster.handles_actor(name) return name == "apple_monster_actor" end
function monster.handles_timer(name) return name == update_event end

function monster.on_trigger(ctx, event)
    if stopped or event.phase ~= "enter" or not monster.handles_trigger(event.name)
        or used[event.id] or active + reserved >= max_actors then return end
    local c = config(event.properties or {})
    local px, py = ctx:get_position("player")
    if px == nil then return end
    local x, y
    for _ = 1, 12 do
        local angle, radius = random(c)*math.pi*2, c.spawn_min+random(c)*(c.spawn_max-c.spawn_min)
        x = clamp(px+math.cos(angle)*radius, c.min_x, c.max_x)
        y = clamp(py-c.hover+math.sin(angle)*c.height, c.min_y, c.max_y)
        if (x-px)^2+(y-py)^2 >= (c.size*.5+3)^2 then break else x = nil end
    end
    if x == nil then return end -- a cramped authored arena cannot force a spawn on the player
    sequence = sequence + 1
    local id = "apple_monster:" .. sequence .. ":actor"
    assert(not exists(ctx, id), "reserved apple monster entity ID is occupied")
    pending[id], used[event.id] = {c=c, token=tostring(sequence)}, true
    reserved = reserved + 1
    ctx:spawn(c.actor_prefab, id, x, y)
    ctx:set_size(id, c.size, c.size)
    sound(ctx, c.spawn_sound)
end

function monster.on_entity_spawn(ctx, event)
    if stopped or not monster.handles_actor(event.name) then return end
    local allocation = pending[event.id]
    local had_reservation = allocation ~= nil
    local c = allocation and allocation.c or config(event.properties or {})
    local old = actors[event.id]
    assert(active - (old and 1 or 0) < max_actors, "apple monster room limit is four active actors")
    if old then drop(ctx, old, true) end
    if not allocation then sequence = sequence + 1; allocation = {token=tostring(sequence)} end
    pending[event.id] = nil
    if had_reservation then reserved = reserved - 1 end
    local state = {id=event.id, c=c, token=allocation.token, hp=c.hp,
        time=0, next_goal=0, next_shot=c.first_shot, shot_serial=0,
        bullets={}, hits={}, vx=0, vy=0, flash=0, bars={}}
    for i=1,3 do
        state.bars[i] = "apple_monster:" .. state.token .. ":bar:" .. i
        assert(not exists(ctx, state.bars[i]), "reserved apple monster health bar ID is occupied")
    end
    local x, y = ctx:get_position(state.id)
    actors[state.id], tasks[state.token], active = state, state, active+1
    ctx:set_size(state.id, c.size, c.size)
    make_bar(ctx, state, x, y)
    ctx:after(dt, update_event, state.token)
end

function monster.on_hit(ctx, event)
    if stopped or not monster.handles_actor(event.name) or event.phase ~= "hit" then return end
    local state = actors[event.id]
    if not state then return end
    local projectile = event.properties and event.properties.projectileId
    if type(projectile) ~= "string" or state.hits[projectile] then return end
    state.hits[projectile] = true
    state.hp = state.hp - 1 -- every distinct player bullet deals exactly one point
    sound(ctx, state.c.hit_sound)
    if state.hp == 0 then sound(ctx, state.c.death_sound); drop(ctx, state, false); return end
    update_fill(ctx, state)
    state.flash = .12
    ctx:set_opacity(state.id, .5)
end

function monster.on_timer(ctx, event)
    if stopped or event.name ~= update_event then return end
    local state = tasks[event.id]
    if not state then return end
    local x, y = ctx:get_position(state.id)
    local px, py = ctx:get_position("player")
    if x == nil or px == nil then drop(ctx, state, false); return end
    state.time = state.time + dt
    for id, expiry in pairs(state.bullets) do
        local bx, by = ctx:get_position(id)
        if bx == nil or state.time >= expiry or bx < state.c.min_x-8 or bx > state.c.max_x+8
            or by < state.c.min_y-8 or by > state.c.max_y+8 then
            remove(ctx, id); state.bullets[id] = nil; bullet_count = bullet_count - 1
        end
    end
    if state.time >= state.next_goal then choose_goal(state, px, py) end
    local dx, dy = state.goal_x-x, state.goal_y-y
    local distance = math.sqrt(dx*dx+dy*dy)
    local speed = math.min(state.c.speed, distance * 2)
    state.vx, state.vy = 0, 0
    if distance > .05 then state.vx, state.vy = dx/distance*speed, dy/distance*speed end
    state.vx = (clamp(x+state.vx*dt, state.c.min_x, state.c.max_x)-x)/dt
    state.vy = (clamp(y+state.vy*dt, state.c.min_y, state.c.max_y)-y)/dt
    ctx:set_velocity(state.id, state.vx, state.vy)
    for index, id in ipairs(state.bars) do
        local bx, by = ctx:get_position(id)
        if bx then
            local tx, ty = bar_position(state, x+state.vx*dt, y+state.vy*dt, index == 3)
            ctx:set_velocity(id, (tx-bx)/dt, (ty-by)/dt)
        end
    end
    if state.flash > 0 then
        state.flash = math.max(0, state.flash-dt)
        if state.flash == 0 then ctx:set_opacity(state.id, 1) end
    end
    if state.time >= state.next_shot then
        fire(ctx, state, x, y, px, py)
        state.next_shot = state.time + state.c.shot_interval
    end
    ctx:after(dt, update_event, state.token)
end

function monster.cancel_all(ctx)
    stopped = true
    local list = {}
    for _, state in pairs(actors) do list[#list+1] = state end
    for _, state in ipairs(list) do drop(ctx, state, false) end
    actors, tasks, pending = {}, {}, {}
    bullet_count, active, reserved = 0, 0, 0
end
function monster.on_death(ctx) monster.cancel_all(ctx) end
return monster
