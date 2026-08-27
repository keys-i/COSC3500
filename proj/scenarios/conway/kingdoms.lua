local ai = {}

-- Quarter-share ancestry carried by every living kingdom cell
-- 1..3 are pure; 4..9 favour one parent; 10..12 are equal pair realms
ai.shares = {
    { 4, 0, 0 },
    { 0, 4, 0 },
    { 0, 0, 4 },
    { 3, 1, 0 },
    { 1, 3, 0 },
    { 3, 0, 1 },
    { 1, 0, 3 },
    { 0, 3, 1 },
    { 0, 1, 3 },
    { 2, 2, 0 },
    { 2, 0, 2 },
    { 0, 2, 2 },
}
ai.loyalty = { 1, 2, 3, 1, 2, 1, 3, 2, 3, 0, 0, 0 }

-- Three exact 48-bit chunks carry RGB ancestry through Lua's number type
ai.traits = { 0, 0, 0 }
local places = { 1, 4096, 16777216, 68719476736 }
for state, share in ipairs(ai.shares) do
    local chunk = 1 + math.floor((state - 1) / 4)
    local place = places[1 + (state - 1) % 4]
    local traits = share[1] + share[2] * 16 + share[3] * 256
    ai.traits[chunk] = ai.traits[chunk] + traits * place
end

local function pair_state(first, second)
    if first > second then
        first, second = second, first
    end
    if first == 1 then return second == 2 and 10 or 11 end
    return 12
end

-- Quantise local parent ancestry to quarters while retaining the stronger line
function ai.inherit(red, blue, yellow)
    local total = red + blue + yellow
    if total == 0 then return 0 end
    local leader, first, runner, second, tail
    if red >= blue and red >= yellow then
        leader, first = 1, red
        if blue >= yellow then
            runner, second, tail = blue, 2, yellow
        else
            runner, second, tail = yellow, 3, blue
        end
    elseif blue >= yellow then
        leader, first = 2, blue
        if red >= yellow then
            runner, second, tail = red, 1, yellow
        else
            runner, second, tail = yellow, 3, red
        end
    else
        leader, first = 3, yellow
        if red >= blue then
            runner, second, tail = red, 1, blue
        else
            runner, second, tail = blue, 2, red
        end
    end
    -- Three materially different ideologies cannot hold a realm together
    if tail * 8 >= total then return 0 end
    if runner * 8 < total then return leader end
    if (first - runner) * 4 <= total then return pair_state(leader, second) end
    if leader == 1 and second == 2 then return 4 end
    if leader == 2 and second == 1 then return 5 end
    if leader == 1 and second == 3 then return 6 end
    if leader == 3 and second == 1 then return 7 end
    if leader == 2 and second == 3 then return 8 end
    return 9
end

-- Red expands and raids; blue fortifies; yellow skirmishes and retreats
function ai.next_cell(current, generation, cell, live, red, blue, yellow)
    if live == 0 then return 0 end
    local heir = ai.inherit(red, blue, yellow)
    if heir == 0 then return 0 end
    local salt = generation * 131 + cell * 17
    -- Vim begins as the weakest house, then assimilates only borders where
    -- red ancestry already survives; it never creates a red line from nothing.
    if generation >= 720 and red > 0 then
        red = red + math.floor((live + 1) / 2)
        heir = ai.inherit(red, blue, yellow)
    end
    if current == 0 then
        local loyalty = ai.loyalty[heir] or 0
        if loyalty == 1 then
            local odds = generation < 1440 and 97
                or (generation < 2880 and 31 or 2)
            return live >= 2 and live <= 3 and salt % odds == 0 and heir or 0
        end
        if loyalty == 2 then return live == 3 and heir or 0 end
        if loyalty == 3 then
            return live == 2 and salt % 29 == 0 and heir or 0
        end
        return live >= 2 and live <= 3 and salt % 5 == 0 and heir or 0
    end
    local loyalty = ai.loyalty[current] or 0
    local share = ai.shares[current]
    -- Each house weights its own bloodline differently when borders marry.
    local weight = loyalty == 1 and (generation < 1440 and 1 or 4)
        or (loyalty == 2 and 3 or (loyalty == 3 and 1 or 2))
    if loyalty == 0 and generation >= 2880 and share[1] > 0 then weight = 3 end
    local marriage = ai.inherit(
        red + share[1] * weight,
        blue + share[2] * weight,
        yellow + share[3] * weight
    )
    if loyalty == 1 then
        if live < 1 or live > 3 then return 0 end
        -- Vim raids harder late, but heirs keep the conquered ancestry.
        return heir ~= current and marriage or current
    end
    if loyalty == 2 then
        if live < 2 or live > 4 then return 0 end
        if generation >= 720 and ai.loyalty[heir] == 1 then return heir end
        -- Emacs accepts dense defensive alliances and otherwise holds.
        return heir ~= current and live >= 3 and marriage or current
    end
    if loyalty == 3 then
        if live < 1 or live > 3 then return 0 end
        -- Nano marries freely, then retreats from costly border fights.
        if heir ~= current then return salt % 5 == 0 and 0 or marriage end
        return current
    end
    if live < 1 or live > 4 or salt % 53 == 0 then return 0 end
    -- Mixed confederacies keep both parents and re-form as borders move.
    return heir ~= current and marriage or current
end

return ai
