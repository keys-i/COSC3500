---@diagnostic disable: lowercase-global
-- Carrom match controller
-- Scene discs become local physics bodies during a shot
-- A shot records causal pockets before settlement applies board and match rules
-- Timeline flow is launch, substep physics, finish sinks, settle, then repeat
---@class Shot
---@field own integer[]
---@field opponent integer[]
---@field own_count integer
---@field opponent_count integer
---@field queen boolean
---@field striker boolean
---@field queen_right boolean
---@field is_break boolean
---@field target integer
---@field contacts integer[][]
---@field causal table<integer, boolean>
-- Disc index maps directly to the scene entity and colour pairing
names = {
    "striker",
    "queen",
    "white_1",
    "black_1",
    "white_2",
    "black_2",
    "white_3",
    "black_3",
    "white_4",
    "black_4",
    "white_5",
    "black_5",
    "white_6",
    "black_6",
    "white_7",
    "black_7",
    "white_8",
    "black_8",
    "white_9",
    "black_9",
}
-- Positions use board units while velocities advance in seconds
discs, player, next_shot, rolling = {}, 1, 18, false
score, pocketed, dues = { 0, 0 }, { {}, {} }, { 0, 0 }
queen_covered, queen_pending, queen_right = 0, 0, { false, false }
match_points, board, board_limit, breaker, tie_board = { 0, 0 }, 1, 8, 1, false
claimed, match_score, pockets, causal_pockets, finished, final_code =
    { 1, 2 }, nil, 0, 0, false, -1
---@type Shot?
shot = nil
break_pending = true
low, high, pocket_radius = 1.62, 14.38, 1.60
rail_low, rail_high, physics_dt = 2.08, 13.92, 0.00416666666667
physics_ticks = 12
last_contact, sink_events, sink_finalized = 0, 0, 0
local player_base = { 1, 3 }

-- Disc ownership and scene synchronisation
local function colour(index) return index % 2 == 1 and 1 or 2 end
local function own(index, side)
    return index >= 3 and claimed[side] ~= 0 and colour(index) == claimed[side]
end
local function targetable(index, side)
    return own(index, side)
        or (index == 2 and queen_right[side] and dues[side] == 0)
end
local function load(name)
    local striker, id = name == "striker", engine.id("type", name)
    local x, y = engine.entity(id, 0), engine.entity(id, 1)
    return {
        id = id,
        x = x,
        y = y,
        home_x = x,
        home_y = y,
        vx = 0,
        vy = 0,
        radius = striker and 0.38125 or 0.305,
        mass = striker and 0.65 or 1,
        active = true,
        sinking = false,
    }
end
local function move(d)
    if d.active then engine.move(d.id, d.x, d.y) end
end
local function centre_slot(index)
    return 8 + ((index % 3) - 1) * 0.42,
        8 + ((math.floor(index / 3) % 3) - 1) * 0.42
end
local function set_live(index, x, y)
    local d = discs[index]
    d.active, d.sinking, d.x, d.y, d.vx, d.vy = true, false, x, y, 0, 0
    engine.show(d.id)
    move(d)
end
local function restore(index)
    local x, y = centre_slot(index)
    set_live(index, x, y)
end
local function near_pocket(d, x, y)
    x, y = d.x - x, d.y - y
    return x * x + y * y < pocket_radius * pocket_radius
end
local function pocket_target(d)
    if near_pocket(d, low, low) then return low, low end
    if near_pocket(d, low, high) then return low, high end
    if near_pocket(d, high, low) then return high, low end
    if near_pocket(d, high, high) then return high, high end
end

-- Collision and rail response
-- Keep disc centres inside the playable rail rectangle after each substep
local function rail(d)
    local lo, hi = rail_low + d.radius, rail_high - d.radius
    if d.x < lo then
        d.x, d.vx = lo, math.abs(d.vx) * 0.78
    end
    if d.x > hi then
        d.x, d.vx = hi, -math.abs(d.vx) * 0.78
    end
    if d.y < lo then
        d.y, d.vy = lo, math.abs(d.vy) * 0.78
    end
    if d.y > hi then
        d.y, d.vy = hi, -math.abs(d.vy) * 0.78
    end
end
-- Resolve overlap before the elastic impulse so stacked discs separate reliably
local function collide(a, b)
    local dx, dy = b.x - a.x, b.y - a.y
    local contact, distance2 = a.radius + b.radius, dx * dx + dy * dy
    if distance2 >= contact * contact then return end
    local distance = math.sqrt(distance2)
    if distance < 0.00001 then
        dx, dy, distance = 0.001, 0, 0.001
    end
    local nx, ny, ia, ib = dx / distance, dy / distance, 1 / a.mass, 1 / b.mass
    local isum, overlap = ia + ib, contact - distance
    a.x, a.y = a.x - nx * overlap * ia / isum, a.y - ny * overlap * ia / isum
    b.x, b.y = b.x + nx * overlap * ib / isum, b.y + ny * overlap * ib / isum
    local closing = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny
    if closing < 0 then
        local impulse = -1.84 * closing / isum
        a.vx, a.vy = a.vx - impulse * ia * nx, a.vy - impulse * ia * ny
        b.vx, b.vy = b.vx + impulse * ib * nx, b.vy + impulse * ib * ny
        last_contact = last_contact + 1
        if rolling and shot then
            -- Credit only striker-connected contacts
            if shot.causal[a.index] or shot.causal[b.index] then
                shot.causal[a.index], shot.causal[b.index] = true, true
                shot.contacts[#shot.contacts + 1] = { a.index, b.index }
            end
        end
    end
end
baseline_min, baseline_max = 3.80, 12.05
local function baseline_x(x) return x >= baseline_min and x <= baseline_max end
local function baseline_y(side)
    local base =
        assert(player_base[side], "carrom supports players 1 and 2 only")
    assert(
        base == 1 or base == 3,
        "carrom players must use opposite bases 1 and 3"
    )
    return base == 1 and 12.40 or 3.53
end

-- Shot planning uses contact geometry and line-of-sight penalties
-- Score blockers near a proposed path without treating its target as a blocker
local function blocked(ax, ay, bx, by, ignored)
    local dx, dy = bx - ax, by - ay
    local length2, penalty = dx * dx + dy * dy, 0
    if length2 == 0 then return 999 end
    for i, d in ipairs(discs) do
        if i ~= ignored and i ~= 1 and d.active then
            local t = ((d.x - ax) * dx + (d.y - ay) * dy) / length2
            if t > 0.08 and t < 0.92 then
                local ox, oy = d.x - (ax + t * dx), d.y - (ay + t * dy)
                if ox * ox + oy * oy < (d.radius + 0.16) ^ 2 then
                    penalty = penalty + 9
                end
            end
        end
    end
    return penalty
end
-- Turn a legal geometric path into a scored striker placement and velocity
local function offer(
    index,
    sx,
    sy,
    gx,
    gy,
    px,
    py,
    bank,
    relaxed,
    bank_x,
    bank_y,
    side,
    best
)
    local target, contact =
        discs[index], discs[1].radius + discs[index].radius - 0.025
    local dx, dy = px - target.x, py - target.y
    local pocket_distance = math.sqrt(dx * dx + dy * dy)
    if pocket_distance < contact then return best end
    local nx, ny = dx / pocket_distance, dy / pocket_distance
    -- Reject blocked paths
    local incoming, outgoing =
        blocked(sx, sy, gx, gy, index),
        blocked(target.x, target.y, px, py, index)
    if
        math.abs(gx - (target.x - nx * contact)) > 0.04
        or math.abs(gy - (target.y - ny * contact)) > 0.04
        or (
            (not bank and (incoming ~= 0 or outgoing ~= 0) and not relaxed)
            or (bank and outgoing ~= 0)
        )
    then
        return best
    end
    local ax, ay = (bank and bank_x or gx) - sx, (bank and bank_y or gy) - sy
    local aim = math.sqrt(ax * ax + ay * ay)
    if aim < 0.55 then return best end
    local value = pocket_distance
        + aim * 0.23
        + (bank and 1.65 or 0)
        + incoming
        + outgoing
        + (engine.random() - 0.5) * 0.08
    if best == nil or value < best.value then
        local live_own = 0
        for live_index, disc in ipairs(discs) do
            if disc.active and own(live_index, side) then
                live_own = live_own + 1
            end
        end
        local angle = live_own == 1 and 0 or (engine.random() - 0.5) * 0.006
        local force_jitter = 1 + (engine.random() - 0.5) * 0.03
        local cosine, sine, ux, uy =
            math.cos(angle), math.sin(angle), ax / aim, ay / aim
        return {
            index = index,
            x = sx,
            y = sy,
            vx = (ux * cosine - uy * sine) * 60 * force_jitter,
            vy = (ux * sine + uy * cosine) * 60 * force_jitter,
            value = value,
        }
    end
    return best
end
-- Test direct lines first, then one-cushion banks when no direct shot survives
local function consider(index, px, py, side, best, bank_only, relaxed)
    local target, striker, baseline = discs[index], discs[1], baseline_y(side)
    local dx, dy = px - target.x, py - target.y
    local distance = math.sqrt(dx * dx + dy * dy)
    if distance < 0.01 then return best end
    local nx, ny = dx / distance, dy / distance
    if math.abs(ny) < 0.015 then return best end
    local contact = striker.radius + target.radius - 0.025
    local gx, gy = target.x - nx * contact, target.y - ny * contact
    if not bank_only then
        local travel = (gy - baseline) / ny
        local sx = gx - nx * travel
        if travel > 0.55 and baseline_x(sx) then
            best = offer(
                index,
                sx,
                baseline,
                gx,
                gy,
                px,
                py,
                false,
                relaxed,
                0,
                0,
                side,
                best
            )
        end
    end
    -- One-cushion banks
    if bank_only and math.abs(nx) > 0.08 then
        for _, rail_x in ipairs({
            rail_low + striker.radius,
            rail_high - striker.radius,
        }) do
            local to_rail = (gx - rail_x) / nx
            local by = gy - ny * to_rail
            -- Compensate for 0.78 rail restitution
            local inbound_x, inbound_y = -nx / 0.78, ny
            local inbound_length =
                math.sqrt(inbound_x * inbound_x + inbound_y * inbound_y)
            inbound_x, inbound_y =
                inbound_x / inbound_length, inbound_y / inbound_length
            local to_baseline = (by - baseline) / inbound_y
            local sx = rail_x - inbound_x * to_baseline
            if
                to_rail > 0.20
                and to_baseline > 0.20
                and baseline_x(sx)
                and by > rail_low
                and by < rail_high
            then
                if
                    blocked(sx, baseline, rail_x, by, index) == 0
                    and blocked(rail_x, by, gx, gy, index) == 0
                then
                    best = offer(
                        index,
                        sx,
                        baseline,
                        gx,
                        gy,
                        px,
                        py,
                        true,
                        false,
                        rail_x,
                        by,
                        side,
                        best
                    )
                end
            end
        end
    end
    return best
end
-- Prefer clear direct pots, then relaxed lines, banks, and a safe recovery shot
local function choose(side)
    if break_pending then
        local baseline, dy = baseline_y(side), side == 1 and -1 or 1
        return {
            index = 0,
            x = 8 + (engine.random() - 0.5) * 0.12,
            y = baseline,
            vx = (engine.random() - 0.5) * 1.1,
            vy = dy * 60,
        }
    end
    local best = nil
    for index, d in ipairs(discs) do
        if d.active and targetable(index, side) then
            best = consider(index, low, low, side, best, false, false)
            best = consider(index, low, high, side, best, false, false)
            best = consider(index, high, low, side, best, false, false)
            best = consider(index, high, high, side, best, false, false)
        end
    end
    if best then return best end
    for index, d in ipairs(discs) do
        if d.active and targetable(index, side) then
            best = consider(index, low, low, side, best, false, true)
            best = consider(index, low, high, side, best, false, true)
            best = consider(index, high, low, side, best, false, true)
            best = consider(index, high, high, side, best, false, true)
        end
    end
    if best then return best end
    for index, d in ipairs(discs) do
        if d.active and targetable(index, side) then
            best = consider(index, low, low, side, best, true, false)
            best = consider(index, low, high, side, best, true, false)
            best = consider(index, high, low, side, best, true, false)
            best = consider(index, high, high, side, best, true, false)
        end
    end
    if best then return best end
    local baseline, recovery = baseline_y(side), nil
    for index, d in ipairs(discs) do
        if d.active and targetable(index, side) then
            local sx = math.max(baseline_min, math.min(baseline_max, d.x))
            local dx, dy = d.x - sx, d.y - baseline
            local length = math.sqrt(dx * dx + dy * dy)
            if length <= discs[1].radius + d.radius + 0.08 then
                sx = math.max(
                    baseline_min,
                    math.min(baseline_max, d.x + (d.x < 8 and 1.2 or -1.2))
                )
                dx, dy = d.x - sx, d.y - baseline
                length = math.sqrt(dx * dx + dy * dy)
            end
            if length > 0.55 then
                local value = blocked(sx, baseline, d.x, d.y, index) * 10
                    + length
                    + (engine.random() - 0.5) * 0.08
                if recovery == nil or value < recovery.value then
                    recovery = {
                        index = index,
                        x = sx,
                        y = baseline,
                        vx = dx / length * 60,
                        vy = dy / length * 60,
                        value = value,
                    }
                end
            end
        end
    end
    if recovery then return recovery end
    return {
        index = 0,
        x = 8,
        y = baseline_y(side),
        vx = 0,
        vy = side == 1 and -18.4 or 18.4,
    }
end

-- Board accounting and match progression
-- Dues return the player's most recently pocketed coin before adding a debt
local function return_coin(side)
    local index = table.remove(pocketed[side])
    if index then
        score[side] = score[side] - 1
        restore(index)
        return true
    end
    return false
end
local function apply_due(side, count)
    for _ = 1, count do
        if not return_coin(side) then dues[side] = dues[side] + 1 end
    end
end
local function recover_due(side)
    while dues[side] > 0 and return_coin(side) do
        dues[side] = dues[side] - 1
    end
end
local function all_gone(side)
    if claimed[side] == 0 then return false end
    for i, d in ipairs(discs) do
        if d.active and own(i, side) then return false end
    end
    return true
end
-- A fresh board preserves match totals but resets ownership and queen state
local function rerack(step)
    for index, d in ipairs(discs) do
        if index == 1 then
            d.active = false
            engine.hide(d.id)
        else
            set_live(index, d.home_x, d.home_y)
        end
    end
    score, pocketed, dues = { 0, 0 }, { {}, {} }, { 0, 0 }
    queen_covered, queen_pending, queen_right = 0, 0, { false, false }
    break_pending = true
    breaker = 3 - breaker
    player, next_shot = breaker, step + 28
    claimed = { 0, 0 }
    claimed[breaker], claimed[3 - breaker] = 1, 2
end
local function finish(code)
    assert(code >= 0 and code <= 2, "carrom result")
    assert(
        sink_events == sink_finalized,
        "carrom pocket animation must finish before scoring"
    )
    finished, final_code = true, code
    engine.result(code)
    return true
end
local function remaining(side)
    local count = 0
    for index, d in ipairs(discs) do
        if d.active and own(index, side) then count = count + 1 end
    end
    return count
end
-- Board points are based on the opponent coins left when the board closes
local function close_board(step, winner)
    local points = remaining(3 - winner)
        + (match_points[winner] <= 21 and 3 or 0)
    match_points[winner] = match_points[winner]
        + math.max(1, math.min(12, points))
    if match_points[winner] >= 25 then return finish(winner) end
    if board >= board_limit then
        if match_points[1] ~= match_points[2] then
            return finish(match_points[1] > match_points[2] and 1 or 2)
        end
        if tie_board then return finish(0) end
        tie_board, board_limit = true, board_limit + 1 -- one deciding board only
    end
    board = board + 1
    rerack(step)
    return true
end
local function result_text()
    local actor = player == 1 and "IVY" or "NOAH"
    local state = finished and "COMPLETE"
        or (actor .. (rolling and " STRIKES" or " TO PLAY"))
    local queen = queen_pending ~= 0 and "QUEEN NEEDS COVER"
        or (
            queen_covered ~= 0
                and ("QUEEN COVERED " .. (queen_covered == 1 and "IVY" or "NOAH"))
            or "QUEEN OPEN"
        )
    local colours = claimed[1] == 1 and "IVY WHITE / NOAH BLACK"
        or "IVY BLACK / NOAH WHITE"
    engine.text(
        match_score,
        "BOARD "
            .. board
            .. "/"
            .. board_limit
            .. " · IVY "
            .. match_points[1]
            .. " · NOAH "
            .. match_points[2]
            .. " · "
            .. colours
            .. " · "
            .. queen
            .. " · "
            .. state
    )
end
local function queen_outcome(stroke, due)
    if not stroke.queen or due > 0 then return 0 end
    if stroke.queen_right then return stroke.own_count > 0 and 2 or 1 end
    if stroke.own_count > 1 then return 2 end
    return stroke.own_count == 1 and 1 or 0
end

-- Shot settlement commits pockets only after motion has ended
-- Score only after every sink animation has become a confirmed pocket
local function settle(step)
    assert(shot, "settling requires a shot")
    rolling = false
    local board_closed = false
    local striker = discs[1]
    striker.active, striker.vx, striker.vy = false, 0, 0
    engine.hide(striker.id)
    local keeps_turn = false
    local covering = queen_pending == player
    if shot.striker then
        -- AICF Laws 72--75 and 98--101
        apply_due(player, shot.own_count + 1)
        if shot.queen or covering then
            restore(2)
            queen_pending = 0
        end
        keeps_turn = shot.own_count > 0 or (shot.queen and shot.queen_right)
    elseif covering then
        queen_pending = 0
        if shot.own_count > 0 then
            queen_covered, keeps_turn = player, true
        else
            restore(2)
        end
    elseif shot.queen then
        local outcome = queen_outcome(shot, dues[player])
        if outcome == 2 then
            queen_covered, keeps_turn = player, true
        elseif outcome == 1 then
            queen_pending, keeps_turn = player, true
        else
            restore(2)
        end
    elseif shot.own_count > 0 then
        keeps_turn = true
    end
    if shot.own_count > 0 and not shot.striker then recover_due(player) end
    if all_gone(player) then
        -- AICF Law 107
        board_closed =
            close_board(step, queen_covered == 0 and 3 - player or player)
    elseif all_gone(3 - player) then
        board_closed = close_board(step, 3 - player)
    elseif not board_closed and not keeps_turn then
        player = 3 - player
    end
    if not finished and not board_closed then next_shot = step + 4 end
    result_text()
end
-- Snapshot the rules-relevant outcome before physics starts changing the board
local function launch()
    local p, striker = choose(player), discs[1]
    p.x, p.y =
        math.max(baseline_min, math.min(baseline_max, p.x)), baseline_y(player)
    assert(
        baseline_x(p.x) and p.y == baseline_y(player),
        "carrom striker must launch from its CLASSIC baseline"
    )
    striker.active, striker.x, striker.y, striker.vx, striker.vy =
        true, p.x, p.y, p.vx, p.vy
    engine.show(striker.id)
    move(striker)
    shot = {
        own = {},
        opponent = {},
        own_count = 0,
        opponent_count = 0,
        queen = false,
        striker = false,
        queen_right = queen_right[player],
        is_break = break_pending,
        target = p.index,
        contacts = {},
        causal = { [1] = true },
    }
    break_pending = false
    rolling = true
    result_text()
end
-- Only pockets connected to the striker's contact graph count toward the shot
local function pocket(index)
    assert(shot, "pocketing requires a shot")
    local d = discs[index]
    assert(d.sinking, "carrom piece must visibly reach a hole before pocketing")
    d.active, d.sinking, d.vx, d.vy = false, false, 0, 0
    engine.hide(d.id)
    if index == 1 then
        shot.striker = true
        return
    end
    if not shot.causal[index] then
        restore(index)
        return
    end
    pockets = pockets + 1
    causal_pockets = causal_pockets + 1
    if index == 2 then
        shot.queen = true
        return
    end
    if own(index, player) then
        shot.own[#shot.own + 1], shot.own_count = index, shot.own_count + 1
        score[player], queen_right[player] = score[player] + 1, true
        pocketed[player][#pocketed[player] + 1] = index
    else
        local opponent = 3 - player
        shot.opponent[#shot.opponent + 1], shot.opponent_count =
            index, shot.opponent_count + 1
        score[opponent] = score[opponent] + 1
        pocketed[opponent][#pocketed[opponent] + 1] = index
    end
end
local function stage_pocket(index, x, y)
    local d = discs[index]
    d.sinking, d.x, d.y, d.vx, d.vy = true, x, y, 0, 0
    sink_events = sink_events + 1
    move(d)
end
-- Complete staged sinks before checking whether the shot has stopped
local function finish_pockets()
    for index, d in ipairs(discs) do
        if d.sinking then
            pocket(index)
            sink_finalized = sink_finalized + 1
        end
    end
end
local function at_rest()
    for _, d in ipairs(discs) do
        if d.sinking or (d.active and d.vx * d.vx + d.vy * d.vy > 0.0025) then
            return false
        end
    end
    return true
end

-- Physics advances positions, staged pockets and contacts as separate phases
-- Integrate, detect pockets and rails, solve contacts, then apply rolling drag
local function step_physics()
    local fastest, substeps = 0, 2
    for _, d in ipairs(discs) do
        if d.active and not d.sinking then
            fastest = math.max(fastest, math.sqrt(d.vx * d.vx + d.vy * d.vy))
        end
    end
    substeps =
        math.max(2, math.min(12, math.ceil(fastest * physics_dt * 4 / 0.11)))
    local dt = physics_dt * 4 / substeps
    for _ = 1, substeps do
        for _, d in ipairs(discs) do
            if d.active and not d.sinking then
                d.x, d.y = d.x + d.vx * dt, d.y + d.vy * dt
            end
        end
        for i, d in ipairs(discs) do
            if d.active and not d.sinking then
                local x, y = pocket_target(d)
                if x then
                    stage_pocket(i, x, y)
                else
                    rail(d)
                end
            end
        end
        for pass = 1, 2 do
            for i = 1, #discs - 1 do
                if discs[i].active and not discs[i].sinking then
                    for j = i + 1, #discs do
                        if discs[j].active and not discs[j].sinking then
                            collide(discs[i], discs[j])
                        end
                    end
                end
            end
        end
        local drag = 0.968 ^ (1 / substeps)
        for _, d in ipairs(discs) do
            if d.active and not d.sinking then
                d.vx, d.vy = d.vx * drag, d.vy * drag
                if d.vx * d.vx + d.vy * d.vy < 0.0016 then
                    d.vx, d.vy = 0, 0
                end
            end
        end
    end
    for _, d in ipairs(discs) do
        move(d)
    end
end

-- Host callbacks initialise scene state then advance the match timeline
-- Load scene discs once and exercise the rule and collision invariants
function on_setup()
    assert(math.random == nil and math.randomseed == nil and print == nil)
    for index, name in ipairs(names) do
        local d = load(name)
        d.index = index
        discs[#discs + 1] = d
    end
    match_score = engine.id("type", "match_score")
    assert(
        colour(3) == 1
            and colour(4) == 2
            and own(3, 1)
            and own(4, 2)
            and not own(2, 1)
    )
    assert(
        baseline_x(3.80)
            and baseline_x(12.05)
            and not baseline_x(3.79)
            and not baseline_x(12.06)
    )
    assert(
        player_base[1] == 1
            and player_base[2] == 3
            and baseline_y(1) == 12.40
            and baseline_y(2) == 3.53
    )
    assert(
        math.abs(discs[1].radius / discs[3].radius - 1.25) < 0.0001,
        "carrom striker scale"
    )
    local a, b =
        { index = 1, x = 4, y = 4, vx = 12, vy = 0, radius = 0.3, mass = 1 },
        { index = 3, x = 4.55, y = 4, vx = 0, vy = 0, radius = 0.3, mass = 1 }
    rolling, shot = true, { causal = { [1] = true }, contacts = {} }
    collide(a, b)
    assert(b.vx > 0 and a.x < 4 and b.x > 4.55, "carrom collision response")
    assert(
        shot.causal[3] and #shot.contacts == 1,
        "carrom causal contact ledger"
    )
    rolling, shot = false, nil
    assert(
        queen_outcome({ queen = true, queen_right = true, own_count = 0 }, 0)
                == 1
            and queen_outcome(
                { queen = true, queen_right = true, own_count = 1 },
                0
            ) == 2
            and queen_outcome(
                { queen = true, queen_right = false, own_count = 0 },
                0
            ) == 0
            and queen_outcome(
                    { queen = true, queen_right = true, own_count = 1 },
                    1
                )
                == 0,
        "carrom AICF queen eligibility and cover"
    )
    assert(
        queen_covered == 0
            and queen_pending == 0
            and break_pending
            and dues[1] == 0,
        "carrom opening state"
    )
    result_text()
end
-- A timeline step may contain several short physics intervals for stability
function on_timeline(step)
    local launched = false
    if rolling then finish_pockets() end
    if not rolling and not finished and step >= next_shot then
        launch()
        launched = true
    end
    if rolling and not launched then
        -- Fixed substeps prevent tunnelling
        for _ = 1, physics_ticks do
            step_physics()
            if at_rest() then
                settle(step)
                break
            end
        end
    end
    if step == 10800 then
        assert(pockets > 0, "carrom: no physical pockets")
        assert(last_contact > 0, "carrom: striker never contacted a coin")
        assert(causal_pockets > 0, "carrom: no causal pocket")
        assert(
            score[1] + score[2] > 0 or match_points[1] + match_points[2] > 0,
            "carrom: no legal coin score"
        )
        local live_positions = ""
        for index, disc in ipairs(discs) do
            if index >= 3 and disc.active then
                live_positions = live_positions
                    .. " "
                    .. index
                    .. "@"
                    .. disc.x
                    .. ","
                    .. disc.y
            end
        end
        assert(
            finished,
            "carrom: match did not finish: board="
                .. board
                .. " score="
                .. match_points[1]
                .. ":"
                .. match_points[2]
                .. " live="
                .. remaining(1)
                .. ":"
                .. remaining(2)
                .. " queen="
                .. queen_covered
                .. live_positions
        )
    end
end
