return {
    on_enter = function(ctx, event)
        ctx:message("ORANGE STRIP IS A SENSOR   THE BRIDGE HAS A SHORT DELAY")
    end,
    on_trigger = function(ctx, event)
        if event.phase ~= "enter" then return end
        if event.name == "drop_apple" and ctx:once("apple") then
            ctx:set_velocity("apple_01", 0, 32)
            ctx:sound("apple")
            ctx:after(1.5, "hide_apple", "apple_01")
            ctx:message("APPLE RELEASED BY LUA")
        elseif event.name == "bridge_contact" and ctx:once("bridge") then
            ctx:after(0.65, "collapse_bridge", "bridge")
            ctx:message("BRIDGE TIMER STARTED   JUMP NOW")
        end
    end,
    on_timer = function(ctx, event)
        if event.name == "hide_apple" then
            ctx:set_velocity(event.id, 0, 0)
            ctx:set_enabled(event.id, false)
        elseif event.name == "collapse_bridge" then
            ctx:set_enabled(event.id, false)
            ctx:sound("apple")
            ctx:message("BRIDGE DISABLED   R RESTORES IT AND CLEARS TIMERS")
        end
    end,
    on_checkpoint = function(ctx, event)
        ctx:message("CHECKPOINT SAVED BEFORE THE BRIDGE")
    end,
    on_death = function(ctx, event)
        ctx:message("R TO RESET THE ROOM   TIMERS AND ONCE FLAGS WILL CLEAR")
    end,
    on_reset = function(ctx, event)
        ctx:message("APPLE AND BRIDGE RESTORED   CHECKPOINT RETAINED")
    end
}
