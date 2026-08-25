function on_setup()
    assert(math.random == nil and math.randomseed == nil and print == nil)
end

function on_turn(step)
    local width = 7
    local column = (step - 1) % width
    local row = 5
    while row >= 0 and engine.board(row * width + column) ~= 0 do
        row = row - 1
    end
    if row >= 0 then
        local value = step % 2 == 1 and 2 or 3
        engine.board_set(row * width + column, value)
    end
end
