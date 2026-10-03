-- All ring math and lifetime scheduling live in Lua.
-- The host only provides entity queries, spawning, velocity and timer commands.
local ring = {}
local sequence = 0
local expire_event = "lua_apple_ring_expire"

function ring.spawn(ctx, prefix, center_x, center_y, options)
    local p = options or {}
    local min_count, max_count = p.minCount or 12, p.maxCount or 24
    assert(min_count % 1 == 0 and max_count % 1 == 0
        and min_count >= 1 and min_count <= max_count and max_count <= 64,
        "apple ring count must be integers in 1..64")
    local radius, speed = p.radius or 2.5, p.speed or 15
    local lifetime = p.lifetime or 1
    assert(radius >= 0 and speed >= 0 and lifetime > 0,
        "invalid apple ring radius, speed or lifetime")

    local count = math.random(min_count, max_count) -- inclusive, 12..24 by default
    local phase = math.rad(p.phase or math.random(0, 359))
    local step = 2 * math.pi / count
    sequence = sequence + 1

    for i = 0, count - 1 do
        local angle = phase + i * step
        local dx, dy = math.cos(angle), math.sin(angle)
        local id = prefix .. ":" .. sequence .. ":" .. i
        -- event.properties.prefab identifies the emitting pressure plate.
        -- Use a separate gameplay field so the ring cannot clone its emitter.
        ctx:spawn(p.spawnPrefab or "ring_apple", id,
            center_x + dx * radius, center_y + dy * radius)
        ctx:set_velocity(id, dx * speed, dy * speed)
        ctx:after(lifetime, expire_event, id)
    end
    return count
end

function ring.on_trigger(ctx, event)
    if event.name ~= "apple_ring" or event.phase ~= "enter" then return end
    if not ctx:once("ring:" .. event.id) then return end
    local p = event.properties
    local x, y = ctx:get_position(p.spawnAnchor)
    assert(x ~= nil, "missing apple ring anchor: " .. tostring(p.spawnAnchor))
    ring.spawn(ctx, "burst:" .. event.id, x, y, p)
    ctx:sound("apple")
end

function ring.on_timer(ctx, event)
    if event.name ~= expire_event then return end
    -- It is safe if another gameplay rule already removed an apple.
    if ctx:get_position(event.id) ~= nil then ctx:destroy(event.id) end
end

return ring
