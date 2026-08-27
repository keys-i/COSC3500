function next_cell(current, generation, cell)
    if cell ~= 4 then return 0 end
    -- state 1 / 5 / 9 occupy the three independent packed trait chunks.
    local red, blue, yellow, live = engine.neighbour_traits(801, 1620, 2439)
    if red == 24 and blue == 30 and yellow == 36 and live == 6 then return 0 end
    return 1
end
