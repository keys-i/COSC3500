local model = {}

-- Chronus cellular transition rules
-- The host supplies one cell's neighbour counts and receives its next state
-- Fixed stages model elapsed months while hashes repeat local outcomes
-- State codes are ocean, S, E1, E2, I1, I2, I3, R, V and D
model.ocean, model.susceptible = 0, 1
model.exposed_one, model.exposed_two = 2, 3
model.infectious_one, model.infectious_two, model.infectious_three = 4, 5, 6
model.recovered, model.vaccinated, model.deceased = 7, 8, 9

local function roll(generation, cell)
    -- Integer hash avoids global RNG state
    return (cell * 1103515245 + generation * 12345 + 1013904223) % 997
end

-- Per-cell monthly transition
-- Progression stages advance monthly while infection uses local counts
function model.next_cell(current, generation, cell, infectious, exposed)
    if current == model.ocean or current == model.deceased then
        return current
    end
    if current == model.susceptible then
        if
            infectious > 0
            and roll(generation, cell)
                < math.min(860, infectious * 190 + exposed * 45)
        then
            return model.exposed_one
        end
        if generation >= 48 and roll(generation + 17, cell) < 18 then
            return model.vaccinated
        end
        return current
    end
    if current == model.exposed_one then return model.exposed_two end
    if current == model.exposed_two then return model.infectious_one end
    if current == model.infectious_one then return model.infectious_two end
    if current == model.infectious_two then return model.infectious_three end
    if current == model.infectious_three then
        if roll(generation, cell) < 27 then return model.deceased end
        return model.recovered
    end
    return current
end

return model
