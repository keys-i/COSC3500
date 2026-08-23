local ai = {}

-- Three-colour cellular automaton rules
-- The host supplies the current state and live-neighbour counts by colour
-- Transition order is colour conversion, survival, birth, then trail decay
-- State 0 is empty, 1..3 are live colours, and 4..12 are fading trail steps
function ai.next_cell(current, live, red, blue, yellow)
    -- Cross-colour conversions take precedence over ordinary survival
    if
        (current == 2 or current == 3)
        and red >= 2
        and live >= 2
        and live <= 3
    then
        return 1
    end
    if current == 2 and yellow >= 2 and live >= 2 and live <= 4 then
        return 3
    end

    -- Apply survival before birth to preserve a live cell's allowed colour
    -- Red: S123 and B2
    if current == 1 and live >= 1 and live <= 3 then return 1 end
    -- Blue: B3/S23
    if current == 2 and (live == 2 or live == 3) then return 2 end
    -- Yellow: S234
    if current == 3 and live >= 2 and live <= 4 then return 3 end

    -- Birth colour follows the local mix at three live neighbours
    if current == 0 then
        if live == 2 and red == 2 then return 1 end
        if live == 3 then
            if yellow > 0 and red > 0 and blue > 0 then return 3 end
            if blue >= 2 and blue > red and blue > yellow then return 2 end
            if red >= 2 then return 1 end
            return 3
        end
    end

    -- Dead live cells enter their colour's three-step trail before vanishing
    if current >= 1 and current <= 3 then return 4 + (current - 1) * 3 end
    -- Trail values are stored in consecutive groups of three per live colour
    if current >= 4 and current <= 12 then
        return (current - 4) % 3 == 2 and 0 or current + 1
    end
    return 0
end

return ai
