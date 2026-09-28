return {
    on_enter = function(ctx, event)
        ctx:message("A D MOVE   J DOUBLE JUMP   R RESTART AT CHECKPOINT")
    end,
    on_checkpoint = function(ctx, event)
        ctx:message("CHECKPOINT SAVED   ROOM TRAPS RESET ON R")
    end,
    on_reset = function(ctx, event)
        ctx:message("ROOM RESET   CHECKPOINT RETAINED")
    end
}
