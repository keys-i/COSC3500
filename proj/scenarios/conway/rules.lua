local ai = require("ai")

-- Conway contest host callbacks
-- Setup seeds the board while next_cell delegates one transition per cell
-- on_tick resolves the result from counts reset at each generation boundary
local counted_generation = -1
local counts = { 0, 0, 0 }

-- Seed a balanced three-colour field and lock the transition examples in place
function on_setup()
    assert(ai.next_cell(1, 1, 1, 0, 0) == 1)
    assert(ai.next_cell(2, 1, 0, 1, 0) == 7)
    assert(ai.next_cell(3, 3, 2, 0, 1) == 1)
    assert(ai.next_cell(2, 3, 0, 1, 2) == 3)
    assert(ai.next_cell(1, 3, 1, 0, 2) == 1)
    local total = engine.board_size()
    local placed = 0
    -- Engine RNG is seeded by the run
    for cell = 0, total - 1 do
        if engine.random() < 0.18 then
            engine.board_set(cell, 1 + math.floor(engine.random() * 3))
            placed = placed + 1
        end
    end
    assert(placed * 100 > total * 15 and placed * 100 < total * 21)
end

-- Count live cells once per generation while the host visits each cell
function next_cell(current, generation)
    if generation ~= counted_generation then
        counted_generation = generation
        counts[1], counts[2], counts[3] = 0, 0, 0
    end
    if current >= 1 and current <= 3 then
        counts[current] = counts[current] + 1
    end

    -- The engine reports colour counts from the immutable current generation
    local red = engine.neighbour_count(1)
    local blue = engine.neighbour_count(2)
    local yellow = engine.neighbour_count(3)
    local live = red + blue + yellow
    return ai.next_cell(current, live, red, blue, yellow)
end

-- Resolve the fixed-length contest from the completed generation counts
function on_tick(generation)
    if generation == 720 then
        local winner = counts[2] > counts[1] and 2 or 1
        if counts[3] > counts[winner] then winner = 3 end
        engine.result(winner)
    end
end
