function on_setup() engine.board_set(0, engine.id("type", "one") + 1) end

function on_turn(step)
    if step == 1 then engine.board_set(1, engine.id("type", "two") + 1) end
end
