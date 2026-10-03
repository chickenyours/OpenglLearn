return {
    on_enter = function(ctx, event)
        ctx:message("CROSS THE FIRST STRIP TO SPAWN A HARMLESS ORB")
    end,
    on_trigger = function(ctx, event)
        if event.phase ~= "enter" then return end
        if event.name == "spawn_demo" and ctx:once("orb") then
            ctx:spawn("orb", "demo_orb", -15, 10)
            ctx:set_velocity("demo_orb", 9, 0)
            ctx:after(2, "remove_orb", "demo_orb")
            ctx:message("ORB SPAWNED THROUGH THE ECS COMMAND QUEUE")
        elseif event.name == "tour_finish" then
            ctx:message("TOUR COMPLETE   RIGHT DOOR LOOPS BACK   LEFT DOOR RETURNS")
        end
    end,
    on_timer = function(ctx, event)
        ctx:destroy(event.id)
        ctx:message("ORB DESTROYED SAFELY AFTER THE PHYSICS STEP")
    end,
    on_checkpoint = function(ctx, event)
        ctx:message("CHECKPOINT SAVED IN THIS ROOM")
    end,
    on_reset = function(ctx, event)
        ctx:message("ROOM RESTORED   ORB DEMO CAN RUN AGAIN")
    end
}
