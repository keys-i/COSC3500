local ai = require("kingdoms")

-- Sparse micro-kingdom setup and ancestry-aware cellular callbacks
local counted_generation = -1
local state_counts = {}
local traits = ai.traits

local patterns = {
    { { 0, 0 }, { 1, 0 } }, -- Vim's sparse frontier pair
    { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }, -- Emacs fort
    { { -1, 0 }, { 0, 0 }, { 1, 0 } }, -- Nano skirmish line
}
local founders = { 1, 2, 3 }

function on_setup()
    assert(ai.inherit(8, 8, 0) == 10) -- red + blue = purple
    assert(ai.inherit(12, 4, 0) == 4) -- stronger red loyalty
    assert(ai.inherit(8, 8, 8) == 0) -- ideology collapse

    local total = engine.board_size()
    local root = math.floor(math.sqrt(total))
    local width = total == 28000 and 200
        or (root * root == total and root or math.floor(math.sqrt(total * 1.6)))
    local height = total / width
    local kingdom_goal = math.min(180, math.floor(total / 120))
    assert(total % width == 0)
    local reserved, placed = {}, { 0, 0, 0 }
    local kingdoms, attempts = 0, 0
    while kingdoms < kingdom_goal and attempts < kingdom_goal * 80 do
        attempts = attempts + 1
        local faction = founders[1 + kingdoms % #founders]
        local x = 2 + math.floor(engine.random() * (width - 4))
        local y = 2 + math.floor(engine.random() * (height - 4))
        local free = true
        for dy = -2, 2 do
            for dx = -2, 2 do
                if reserved[(y + dy) * width + x + dx + 1] then free = false end
            end
        end
        if free then
            for _, point in ipairs(patterns[faction]) do
                engine.board_set((y + point[2]) * width + x + point[1], faction)
                placed[faction] = placed[faction] + 1
            end
            for dy = -2, 2 do
                for dx = -2, 2 do
                    reserved[(y + dy) * width + x + dx + 1] = true
                end
            end
            kingdoms = kingdoms + 1
        end
    end
    assert(kingdoms == kingdom_goal)
    assert(placed[1] + placed[2] + placed[3] < total * 0.03)
end

function next_cell(current, generation, cell)
    -- Hold the scored board while the final on_tick reports its winner
    if generation == 4320 then return current end
    if generation ~= counted_generation then
        counted_generation = generation
        for state = 1, 12 do
            state_counts[state] = 0
        end
    end
    local red, blue, yellow, live =
        engine.neighbour_traits(traits[1], traits[2], traits[3])
    local next =
        ai.next_cell(current, generation, cell, live, red, blue, yellow)
    if next >= 1 and next <= 12 then
        state_counts[next] = state_counts[next] + 1
    end
    return next
end

function on_tick(generation)
    if generation ~= 4320 then return end
    local ancestry = { 0, 0, 0 }
    for state = 1, 12 do
        local share = ai.shares[state]
        for line = 1, 3 do
            ancestry[line] = ancestry[line] + state_counts[state] * share[line]
        end
    end
    local winner = ancestry[2] > ancestry[1] and 2 or 1
    if ancestry[3] > ancestry[winner] then winner = 3 end
    engine.result(winner)
end
