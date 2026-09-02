-- Chess game and search controller
-- Board squares run a8 through h1 with White positive and Black negative
-- Move generation builds pseudo-moves, then apply and undo filter legal moves
-- Search iterates depth under a node budget before committing one scene move
local players = require("players")
local b, side, castle, ep, half, ply, done = {}, 1, 15, 0, 0, 0, false
local history = {}
local nodes = 0
local player_names = { "Joe", "Gina" }
-- Undo buffers mirror every mutation so search can change and restore one board
local uf, ut, upiece, ucap, ucs, ucastle, uep, uhalf, urf, urt =
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}
local nr, nc = { -2, -2, -1, -1, 1, 1, 2, 2 }, { -1, 1, -2, 2, -2, 2, -1, 1 }
local kr, kc = { -1, -1, -1, 0, 0, 1, 1, 1 }, { -1, 0, 1, -1, 1, -1, 0, 1 }
local br, bc = { -1, -1, 1, 1 }, { -1, 1, -1, 1 }
local rr, rc = { -1, 1, 0, 0 }, { 0, 0, -1, 1 }
local val = { 100, 320, 330, 500, 900, 0 }
local inf, mate = 1000000000, 900000000

-- Board coordinates and castling bits
local function sq(r, c) return r * 8 + c + 1 end
local function row(s) return math.floor((s - 1) / 8) end
local function col(s) return (s - 1) % 8 end
local function inside(r, c) return r >= 0 and r < 8 and c >= 0 and c < 8 end
local function own(p, w) return p * w > 0 end
local function right(x) return math.floor(castle / x) % 2 == 1 end
local function clear(x)
    if right(x) then castle = castle - x end
end
-- Pack from square, to square and promotion into one integer for search tables
local function pack(f, t, p) return (f * 65 + t) * 8 + (p or 0) end
local function unpackmove(move)
    local p = move % 8
    local rest = math.floor(move / 8)
    return math.floor(rest / 65), rest % 65, p
end
-- Count attacks directly instead of generating the opponent's legal moves
local function attacker_count(target, who)
    if not target then error("attacked missing target") end
    if target < 1 then error("attacked target below board") end
    if target > 64 then error("attacked target above board") end
    if not who then error("attacked missing side") end
    local r, c = row(target), col(target)
    local pr = r + who
    local count = 0
    for dc = -1, 1, 2 do
        if inside(pr, c + dc) and b[sq(pr, c + dc)] == who then
            count = count + 1
        end
    end
    for i = 1, 8 do
        local ar, ac = r + nr[i], c + nc[i]
        if inside(ar, ac) and b[sq(ar, ac)] == who * 2 then
            count = count + 1
        end
        ar, ac = r + kr[i], c + kc[i]
        if inside(ar, ac) and b[sq(ar, ac)] == who * 6 then
            count = count + 1
        end
    end
    for group = 1, 2 do
        for i = 1, 4 do
            local dr, dc =
                group == 1 and br[i] or rr[i], group == 1 and bc[i] or rc[i]
            local ar, ac = r + dr, c + dc
            while inside(ar, ac) do
                local p = b[sq(ar, ac)]
                if p ~= 0 then
                    if p == who * (group == 1 and 3 or 4) or p == who * 5 then
                        count = count + 1
                    end
                    break
                end
                ar, ac = ar + dr, ac + dc
            end
        end
    end
    return count
end
local function attacked(target, who) return attacker_count(target, who) > 0 end

-- Pseudo-move generation
-- Generate moves that obey piece movement but may still leave the king in check
local function pseudo(who)
    local list = { 0 }
    local count = 0
    for f = 1, 64 do
        local piece = b[f]
        if own(piece, who) then
            local k, r, c = math.abs(piece), row(f), col(f)
            if k == 1 then
                local dr, start, last =
                    -who, who == 1 and 6 or 1, who == 1 and 0 or 7
                local ar = r + dr
                if inside(ar, c) and b[sq(ar, c)] == 0 then
                    if ar == last then
                        for p = 5, 2, -1 do
                            count = count + 1
                            list[count] = pack(f, sq(ar, c), p)
                        end
                    else
                        count = count + 1
                        list[count] = pack(f, sq(ar, c))
                    end
                    if r == start and b[sq(r + 2 * dr, c)] == 0 then
                        count = count + 1
                        list[count] = pack(f, sq(r + 2 * dr, c))
                    end
                end
                for dc = -1, 1, 2 do
                    local ac = c + dc
                    if inside(ar, ac) then
                        local to, q = sq(ar, ac), b[sq(ar, ac)]
                        if q ~= 0 and math.abs(q) ~= 6 and not own(q, who) then
                            if ar == last then
                                for p = 5, 2, -1 do
                                    count = count + 1
                                    list[count] = pack(f, to, p)
                                end
                            else
                                count = count + 1
                                list[count] = pack(f, to)
                            end
                        elseif to == ep then
                            count = count + 1
                            list[count] = pack(f, to)
                        end
                    end
                end
            elseif k == 2 or k == 6 then
                local drs, dcs = k == 2 and nr or kr, k == 2 and nc or kc
                for i = 1, 8 do
                    local ar, ac = r + drs[i], c + dcs[i]
                    if
                        inside(ar, ac)
                        and math.abs(b[sq(ar, ac)]) ~= 6
                        and not own(b[sq(ar, ac)], who)
                    then
                        count = count + 1
                        list[count] = pack(f, sq(ar, ac))
                    end
                end
                if k == 6 then
                    local home, enemy = who == 1 and 61 or 5, -who
                    local king_side = who == 1 and 1 or 4
                    local queen_side = who == 1 and 2 or 8
                    if f == home and not attacked(home, enemy) then
                        if
                            right(king_side)
                            and b[home + 1] == 0
                            and b[home + 2] == 0
                            and b[home + 3] == who * 4
                            and not attacked(home + 1, enemy)
                            and not attacked(home + 2, enemy)
                        then
                            count = count + 1
                            list[count] = pack(f, home + 2)
                        end
                        if
                            right(queen_side)
                            and b[home - 1] == 0
                            and b[home - 2] == 0
                            and b[home - 3] == 0
                            and b[home - 4] == who * 4
                            and not attacked(home - 1, enemy)
                            and not attacked(home - 2, enemy)
                        then
                            count = count + 1
                            list[count] = pack(f, home - 2)
                        end
                    end
                end
            else
                local first, last =
                    k == 3 and 1 or (k == 4 and 5 or 1),
                    k == 3 and 4 or (k == 4 and 8 or 8)
                for i = first, last do
                    local x = i <= 4 and i or i - 4
                    local dr, dc =
                        i <= 4 and br[x] or rr[x], i <= 4 and bc[x] or rc[x]
                    local ar, ac = r + dr, c + dc
                    while inside(ar, ac) do
                        local to, q = sq(ar, ac), b[sq(ar, ac)]
                        if own(q, who) then break end
                        if math.abs(q) == 6 then break end
                        count = count + 1
                        list[count] = pack(f, to)
                        if q ~= 0 then break end
                        ar, ac = ar + dr, ac + dc
                    end
                end
            end
        end
    end
    return list, count
end

-- Depth-indexed slices let recursive search reuse one move table
-- They avoid allocating a fresh move list at every search node
local work = {}
local span = 256
local candidate_base = 1
local legal_base = 1 + span * 32
-- Allocation-free pseudo-move generation for search nodes
local function pseudo_into(who, base)
    local count = 0
    for f = 1, 64 do
        local piece = b[f]
        if own(piece, who) then
            local k, r, c = math.abs(piece), row(f), col(f)
            if k == 1 then
                local dr, start, last =
                    -who, who == 1 and 6 or 1, who == 1 and 0 or 7
                local ar = r + dr
                if inside(ar, c) and b[sq(ar, c)] == 0 then
                    if ar == last then
                        for p = 5, 2, -1 do
                            count = count + 1
                            work[base + count - 1] = pack(f, sq(ar, c), p)
                        end
                    else
                        count = count + 1
                        work[base + count - 1] = pack(f, sq(ar, c))
                    end
                    if r == start and b[sq(r + 2 * dr, c)] == 0 then
                        count = count + 1
                        work[base + count - 1] = pack(f, sq(r + 2 * dr, c))
                    end
                end
                for dc = -1, 1, 2 do
                    local ac = c + dc
                    if inside(ar, ac) then
                        local to, q = sq(ar, ac), b[sq(ar, ac)]
                        if q ~= 0 and math.abs(q) ~= 6 and not own(q, who) then
                            if ar == last then
                                for p = 5, 2, -1 do
                                    count = count + 1
                                    work[base + count - 1] = pack(f, to, p)
                                end
                            else
                                count = count + 1
                                work[base + count - 1] = pack(f, to)
                            end
                        elseif to == ep then
                            count = count + 1
                            work[base + count - 1] = pack(f, to)
                        end
                    end
                end
            elseif k == 2 or k == 6 then
                local drs, dcs = k == 2 and nr or kr, k == 2 and nc or kc
                for i = 1, 8 do
                    local ar, ac = r + drs[i], c + dcs[i]
                    if
                        inside(ar, ac)
                        and math.abs(b[sq(ar, ac)]) ~= 6
                        and not own(b[sq(ar, ac)], who)
                    then
                        count = count + 1
                        work[base + count - 1] = pack(f, sq(ar, ac))
                    end
                end
                if k == 6 then
                    local home, enemy = who == 1 and 61 or 5, -who
                    local king_side = who == 1 and 1 or 4
                    local queen_side = who == 1 and 2 or 8
                    if f == home and not attacked(home, enemy) then
                        if
                            right(king_side)
                            and b[home + 1] == 0
                            and b[home + 2] == 0
                            and b[home + 3] == who * 4
                            and not attacked(home + 1, enemy)
                            and not attacked(home + 2, enemy)
                        then
                            count = count + 1
                            work[base + count - 1] = pack(f, home + 2)
                        end
                        if
                            right(queen_side)
                            and b[home - 1] == 0
                            and b[home - 2] == 0
                            and b[home - 3] == 0
                            and b[home - 4] == who * 4
                            and not attacked(home - 1, enemy)
                            and not attacked(home - 2, enemy)
                        then
                            count = count + 1
                            work[base + count - 1] = pack(f, home - 2)
                        end
                    end
                end
            else
                local first, last =
                    k == 3 and 1 or (k == 4 and 5 or 1),
                    k == 3 and 4 or (k == 4 and 8 or 8)
                for i = first, last do
                    local x = i <= 4 and i or i - 4
                    local dr, dc =
                        i <= 4 and br[x] or rr[x], i <= 4 and bc[x] or rc[x]
                    local ar, ac = r + dr, c + dc
                    while inside(ar, ac) do
                        local to, q = sq(ar, ac), b[sq(ar, ac)]
                        if own(q, who) or math.abs(q) == 6 then break end
                        count = count + 1
                        work[base + count - 1] = pack(f, to)
                        if q ~= 0 then break end
                        ar, ac = ar + dr, ac + dc
                    end
                end
            end
        end
    end
    return count
end

-- Apply every special rule here so undo needs only the saved mutation record
-- Position commit and rollback
local function apply(move, slot)
    slot = slot or 0
    local f, t, p = unpackmove(move)
    local piece, cap, cs = b[f], b[t], t
    if piece == nil or cap == nil then
        error(
            "move decoded outside board "
                .. move
                .. " from "
                .. f
                .. " to "
                .. t
        )
    end
    if piece == 0 then error("move starts on empty square") end
    if not own(piece, side) then error("move side mismatch") end
    if math.abs(cap) == 6 then
        error("move generator attempted king capture")
    end
    local oldcastle, oldep, oldhalf, rf, rt = castle, ep, half, 0, 0
    local who, k = side, math.abs(piece)
    if k == 1 and t == ep and cap == 0 then
        cs = t + who * 8
        cap = b[cs]
    end
    b[f] = 0
    b[t] = who * (p ~= 0 and p or k)
    if cs ~= t then b[cs] = 0 end
    if k == 6 and math.abs(t - f) == 2 then
        rf = t > f and f + 3 or f - 4
        rt = t > f and f + 1 or f - 1
        b[rt] = b[rf]
        b[rf] = 0
    end
    if k == 6 then
        clear(who == 1 and 1 or 4)
        clear(who == 1 and 2 or 8)
    end
    if f == 57 or cs == 57 then clear(2) end
    if f == 64 or cs == 64 then clear(1) end
    if f == 1 or cs == 1 then clear(8) end
    if f == 8 or cs == 8 then clear(4) end
    ep = k == 1 and math.abs(t - f) == 16 and f - who * 8 or 0
    half = (k == 1 or cap ~= 0) and 0 or half + 1
    side = -side
    uf[slot] = f
    ut[slot] = t
    upiece[slot] = piece
    ucap[slot] = cap
    ucs[slot] = cs
    ucastle[slot] = oldcastle
    uep[slot] = oldep
    uhalf[slot] = oldhalf
    urf[slot] = rf
    urt[slot] = rt
end
-- Restore the position stored by apply before returning to the parent node
local function undo(slot)
    local f, t, cs = uf[slot], ut[slot], ucs[slot]
    side = -side
    castle = ucastle[slot]
    ep = uep[slot]
    half = uhalf[slot]
    b[f] = upiece[slot]
    b[t] = cs == t and ucap[slot] or 0
    if cs ~= t then b[cs] = ucap[slot] end
    if urf[slot] ~= 0 then
        b[urf[slot]] = b[urt[slot]]
        b[urt[slot]] = 0
    end
end
local function king(who)
    for s = 1, 64 do
        if b[s] == who * 6 then return s end
    end
    return 0
end
local function incheck(who, context)
    if who ~= 1 and who ~= -1 then error("invalid side") end
    local k = king(who)
    if k == 0 then error(context) end
    return attacked(k, -who)
end
-- Filter pseudo-moves by making each move and checking the moving side's king
-- Legal-move filtering
local function legal(who)
    local ownking = king(who)
    if ownking == 0 then error("legal entered without own king") end
    local enemyking = king(-who)
    if enemyking == 0 then error("legal entered without enemy king") end
    local before = side
    local out = { 0 }
    local out_count = 0
    local candidates, candidate_count = pseudo(who)
    if king(who) == 0 then
        error(
            "pseudo removed own king at node "
                .. nodes
                .. " square "
                .. ownking
                .. " value "
                .. b[ownking]
        )
    end
    if king(-who) == 0 then error("pseudo removed enemy king") end
    for index = 1, candidate_count do
        local move = candidates[index]
        local _, target = unpackmove(move)
        if math.abs(b[target]) ~= 6 then
            apply(move, 15)
            local k = king(who)
            local ok = k ~= 0 and not attacked(k, -who)
            undo(15)
            if king(who) == 0 then error("legal undo lost own king") end
            if king(-who) == 0 then error("legal undo lost enemy king") end
            if ok then
                out_count = out_count + 1
                out[out_count] = move
            end
        end
    end
    if side ~= before then error("legal changed side") end
    if king(who) == 0 then error("legal returned without own king") end
    if king(-who) == 0 then error("legal returned without enemy king") end
    return out, out_count
end

-- Search variant of legal that writes into the depth-indexed move buffer
local function legal_into(who, depth)
    local cbase = candidate_base + depth * span
    local lbase = legal_base + depth * span
    local candidate_count = pseudo_into(who, cbase)
    local out_count = 0
    for index = 0, candidate_count - 1 do
        local move = work[cbase + index]
        local _, target = unpackmove(move)
        if math.abs(b[target]) ~= 6 then
            apply(move, 32 + depth)
            local ok = not attacked(king(who), -who)
            undo(32 + depth)
            if ok then
                out_count = out_count + 1
                work[lbase + out_count - 1] = move
            end
        end
    end
    return lbase, out_count
end

-- Hash en passant only when a legal capture exists
local function ep_for_key()
    if ep == 0 then return 0 end
    local er, ec = row(ep), col(ep)
    local from_row = er + side
    for dc = -1, 1, 2 do
        local ac = ec + dc
        if inside(from_row, ac) then
            local f = sq(from_row, ac)
            if b[f] == side then
                local move = pack(f, ep)
                local mover = side
                apply(move, 31)
                local ok = not attacked(king(mover), side)
                undo(31)
                if ok then return ep end
            end
        end
    end
    return 0
end
-- Two rolling hashes avoid allocating a board string at each search node
local function key()
    local a, z = 17, 97
    for s = 1, 64 do
        local v = b[s] + 6
        a = (a * 37 + v * (s + 3)) % 2147483629
        z = (z * 67 + v * (s + 19)) % 2147483587
    end
    return a .. ":" .. z .. ":" .. side .. ":" .. castle .. ":" .. ep_for_key()
end
-- Recognise the material-only positions that cannot produce checkmate
local function insufficient()
    local minors, bishops, knights, colour = 0, 0, 0, -1
    for s = 1, 64 do
        local k = math.abs(b[s])
        if k == 1 or k == 4 or k == 5 then return false end
        if k == 2 then
            minors = minors + 1
            knights = knights + 1
        elseif k == 3 then
            minors = minors + 1
            bishops = bishops + 1
            local here = (row(s) + col(s)) % 2
            if colour == -1 then
                colour = here
            elseif colour ~= here then
                return false
            end
        end
    end
    return minors <= 1 or (knights == 0 and bishops > 0 and colour ~= -1)
end
-- Evaluation and search workspace
local pst = {
    0,
    2,
    4,
    6,
    8,
    6,
    4,
    2,
    2,
    5,
    8,
    12,
    14,
    12,
    8,
    5,
    4,
    8,
    14,
    20,
    24,
    20,
    14,
    8,
    6,
    12,
    20,
    30,
    34,
    30,
    20,
    12,
    8,
    14,
    24,
    36,
    42,
    36,
    24,
    14,
    6,
    12,
    20,
    30,
    34,
    30,
    20,
    12,
    4,
    8,
    14,
    20,
    24,
    20,
    14,
    8,
    2,
    5,
    8,
    12,
    14,
    12,
    8,
    5,
}
local search_limit = 0
local tt_depth, tt_score, tt_move, tt_flag = {}, {}, {}, {}
local root_score = {}
local aborted = false
local st_stage, st_left, st_alpha, st_beta, st_alpha0, st_beta0, st_best, st_index, st_count, st_base, st_hash =
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}
local pf_stage, pf_index, pf_count, pf_base = {}, {}, {}, {}
local q_stage, q_left, q_alpha, q_beta, q_index, q_count, q_base =
    {}, {}, {}, {}, {}, {}, {}
---@type { tactical_depth: integer }?
local search_profile, search_profile_key = nil, ""
local function profile_for(who)
    return players[player_names[who == 1 and 1 or 2]]
end
local function search_key() return key() .. ":" .. search_profile_key end
-- Add player-specific pressure and king-safety terms to material evaluation
local function tactical_evaluate(who, profile)
    local score = 0
    for s = 1, 64 do
        local p, k = b[s], math.abs(b[s])
        if p ~= 0 and k ~= 6 then
            local owner = p > 0 and 1 or -1
            local sign = owner == who and 1 or -1
            local attackers = attacker_count(s, -owner)
            local defenders = attacker_count(s, owner)
            if attackers > 0 then
                local loss = math.max(
                    1,
                    math.floor(val[k] / (defenders == 0 and 100 or 200))
                )
                if defenders == 0 or attackers > defenders then
                    score = score
                        + (
                                owner == who and -profile.piece_safety
                                or profile.piece_pressure
                            )
                            * loss
                end
            end
            if k == 2 then
                local targets = 0
                local r, c = row(s), col(s)
                for i = 1, 8 do
                    local ar, ac = r + nr[i], c + nc[i]
                    if
                        inside(ar, ac)
                        and own(b[sq(ar, ac)], -owner)
                        and math.abs(b[sq(ar, ac)]) >= 3
                    then
                        targets = targets + 1
                    end
                end
                if targets >= 2 then
                    score = score + sign * profile.knight_forks * (targets - 1)
                end
            end
        end
    end
    local ownking, enemyking = king(who), king(-who)
    if attacked(ownking, -who) then score = score - profile.king_safety end
    if attacked(enemyking, who) then score = score + profile.king_attack end
    for i = 1, 8 do
        local er, ec = row(enemyking) + kr[i], col(enemyking) + kc[i]
        if inside(er, ec) and attacked(sq(er, ec), who) then
            score = score + profile.king_attack
        end
        local orow, ocol = row(ownking) + kr[i], col(ownking) + kc[i]
        if inside(orow, ocol) and attacked(sq(orow, ocol), -who) then
            score = score - profile.king_safety
        end
    end
    return score
end
-- Scores are always from who perspective so negamax can simply negate children
local function evaluate(who)
    local profile = search_profile or profile_for(who)
    local total, mobility = 0, 0
    for s = 1, 64 do
        local p, k = b[s], math.abs(b[s])
        if p ~= 0 then
            local rank = p > 0 and 7 - row(s) or row(s)
            local square = rank * 8 + col(s) + 1
            local centre = pst[square]
            local advance = k == 1 and rank * 5 or 0
            local king = k == 6 and (28 - centre) or 0
            total = total
                + (p > 0 and 1 or -1)
                    * (math.floor(val[k] * profile.material / 10) + centre * profile.centre + advance * profile.pawn_advance - king * profile.king_safety)
            if k >= 2 and k <= 5 then
                mobility = mobility
                    + (p > 0 and 1 or -1)
                        * math.min(4, math.floor(centre / 8))
                        * profile.activity
            end
        end
    end
    return total * who + mobility * who * 3 + tactical_evaluate(who, profile)
end
-- Put a transposition-table move first to improve alpha-beta cutoffs
local function order(base, count, preferred)
    if preferred == 0 then return end
    for i = 0, count - 1 do
        if work[base + i] == preferred then
            if i ~= 0 then
                local move = work[base]
                work[base] = preferred
                work[base + i] = move
            end
            return
        end
    end
end
local function tactical(move)
    local f, t, p = unpackmove(move)
    return p ~= 0 or b[t] ~= 0 or (math.abs(b[f]) == 1 and t == ep)
end
-- Extend captures and promotions to avoid evaluating hanging pieces
-- Bounded negamax and quiescence search
local function quiesce(depth_limit, alpha, beta)
    local depth, value, returning = 0, 0, false
    q_stage[0] = 0
    q_left[0] = depth_limit
    q_alpha[0] = alpha
    q_beta[0] = beta
    while true do
        if returning then
            if depth == 0 then return value end
            depth = depth - 1
            undo(48 + depth)
            value = -value
            if value >= q_beta[depth] then
                value = q_beta[depth]
                returning = true
            elseif value > q_alpha[depth] then
                q_alpha[depth] = value
                returning = false
            else
                returning = false
            end
        elseif q_stage[depth] == 0 then
            nodes = nodes + 1
            if nodes >= search_limit then
                aborted = true
                value = evaluate(side)
                returning = true
            else
                local stand = evaluate(side)
                if stand >= q_beta[depth] then
                    value = q_beta[depth]
                    returning = true
                else
                    if stand > q_alpha[depth] then q_alpha[depth] = stand end
                    if q_left[depth] <= 0 then
                        value = q_alpha[depth]
                        returning = true
                    else
                        local base, count = legal_into(side, 16 + depth)
                        q_base[depth] = base
                        q_count[depth] = count
                        q_index[depth] = 0
                        q_stage[depth] = 1
                        order(base, count, tt_move[search_key()] or 0)
                    end
                end
            end
        elseif q_index[depth] >= q_count[depth] then
            value = q_alpha[depth]
            returning = true
        else
            q_index[depth] = q_index[depth] + 1
            local move = work[q_base[depth] + q_index[depth] - 1]
            if tactical(move) then
                apply(move, 48 + depth)
                depth = depth + 1
                q_stage[depth] = 0
                q_left[depth] = q_left[depth - 1] - 1
                q_alpha[depth] = -q_beta[depth - 1]
                q_beta[depth] = -q_alpha[depth - 1]
            end
        end
    end
end
-- Iterative negamax keeps the last completed depth
-- An incomplete depth never replaces the prior result after the node budget
local function search_root(move, limit, root_alpha)
    assert(search_profile, "search profile must exist")
    apply(move, 1)
    local depth = 1
    st_stage[depth] = 0
    st_left[depth] = limit - 1
    st_alpha[depth] = -inf
    st_beta[depth] = -root_alpha
    local value = 0
    local returning = false
    while not aborted do
        if returning then
            if depth == 1 then
                undo(1)
                return -value
            end
            undo(depth)
            depth = depth - 1
            value = -value
            if value > st_best[depth] then
                st_best[depth] = value
                tt_move[st_hash[depth]] =
                    work[st_base[depth] + st_index[depth] - 1]
            end
            if value > st_alpha[depth] then st_alpha[depth] = value end
            if st_alpha[depth] >= st_beta[depth] then
                tt_depth[st_hash[depth]] = st_left[depth]
                tt_score[st_hash[depth]] = st_best[depth]
                tt_flag[st_hash[depth]] = 2
                value = st_best[depth]
                returning = true
            else
                returning = false
            end
        else
            nodes = nodes + 1
            if nodes >= search_limit then
                aborted = true
                break
            end
            if st_left[depth] <= 0 then
                value = quiesce(
                    search_profile.tactical_depth,
                    st_alpha[depth],
                    st_beta[depth]
                )
                returning = true
            elseif st_stage[depth] == 0 then
                local hash = search_key()
                st_hash[depth] = hash
                local known = tt_depth[hash] or -1
                local cached = tt_score[hash] or 0
                local flag = tt_flag[hash] or 0
                if
                    known >= st_left[depth]
                    and (
                        flag == 1
                        or (flag == 2 and cached >= st_beta[depth])
                        or (flag == 3 and cached <= st_alpha[depth])
                    )
                then
                    value = cached
                    returning = true
                else
                    local base, count = legal_into(side, depth)
                    st_base[depth] = base
                    st_count[depth] = count
                    st_index[depth] = 0
                    st_best[depth] = -inf
                    st_alpha0[depth] = st_alpha[depth]
                    st_beta0[depth] = st_beta[depth]
                    st_stage[depth] = 1
                    if count == 0 then
                        value = incheck(side, "search lost king")
                                and -mate + depth
                            or 0
                        returning = true
                    else
                        order(base, count, tt_move[hash] or 0)
                    end
                end
            elseif st_index[depth] >= st_count[depth] then
                tt_depth[st_hash[depth]] = st_left[depth]
                tt_score[st_hash[depth]] = st_best[depth]
                tt_flag[st_hash[depth]] = st_best[depth] <= st_alpha0[depth]
                        and 3
                    or (st_best[depth] >= st_beta0[depth] and 2 or 1)
                value = st_best[depth]
                returning = true
            else
                st_index[depth] = st_index[depth] + 1
                local child = work[st_base[depth] + st_index[depth] - 1]
                apply(child, depth + 1)
                depth = depth + 1
                st_stage[depth] = 0
                st_left[depth] = st_left[depth - 1] - 1
                st_alpha[depth] = -st_beta[depth - 1]
                st_beta[depth] = -st_alpha[depth - 1]
            end
        end
    end
    while depth >= 1 do
        undo(depth)
        depth = depth - 1
    end
    return 0
end
-- Player selection and move ordering
local opening = { pack(53, 37), pack(13, 29), pack(63, 46), pack(2, 19) }
local profile_fields = {
    "search_nodes",
    "search_depth",
    "tactical_depth",
    "opening_plies",
    "variety",
    "material",
    "centre",
    "pawn_advance",
    "activity",
    "piece_safety",
    "piece_pressure",
    "knight_forks",
    "king_safety",
    "king_attack",
}
-- Reject incomplete player tables before their weights reach the evaluator
local function validate_profiles()
    local count = 0
    for name, profile in pairs(players) do
        count = count + 1
        if
            type(name) ~= "string"
            or type(profile) ~= "table"
            or type(profile.style) ~= "string"
        then
            error("invalid player profile")
        end
        for i = 1, #profile_fields do
            local field = profile_fields[i]
            local value = profile[field]
            if
                type(value) ~= "number"
                or value < 0
                or value ~= math.floor(value)
            then
                error("invalid " .. name .. "." .. field)
            end
        end
        if
            profile.search_nodes < 1
            or profile.search_depth < 1
            or profile.search_depth > 12
            or profile.tactical_depth > 8
            or profile.opening_plies > #opening
        then
            error("invalid search controls for " .. name)
        end
    end
    if
        count < 2
        or player_names[1] == player_names[2]
        or not players[player_names[1]]
        or not players[player_names[2]]
    then
        error("invalid chess match players")
    end
end
-- Spend fewer nodes in materially decided positions where tactics matter less
local function adaptive_limit(profile)
    local balance = 0
    for s = 1, 64 do
        local piece = b[s]
        if piece ~= 0 then
            balance = balance + (piece > 0 and 1 or -1) * val[math.abs(piece)]
        end
    end
    if math.abs(balance) >= 500 then
        return math.floor(profile.search_nodes * 7 / 10)
    end
    return profile.search_nodes
end
-- Keep the best complete depth and apply seeded variety to near-equal roots
local function choose()
    local base, count = legal_into(side, 0)
    if count == 0 then return 0 end
    local profile = profile_for(side)
    local book = ply < profile.opening_plies and opening[ply + 1] or nil
    if book then
        for i = 0, count - 1 do
            if work[base + i] == book then return book end
        end
    end
    local selected = work[base]
    nodes = 0
    search_profile = profile
    search_profile_key = player_names[side == 1 and 1 or 2]
    search_limit = adaptive_limit(profile)
    for i = 0, count - 1 do
        root_score[work[base + i]] = nil
    end
    for limit = 1, profile.search_depth do
        aborted = false
        local alpha = -inf
        local beta = inf
        local best = -inf
        local complete = true
        order(base, count, selected)
        for i = 0, count - 1 do
            local move = work[base + i]
            local score = search_root(move, limit, alpha)
            if aborted then
                complete = false
                break
            end
            root_score[move] = score
            if score > best then
                best = score
                selected = move
            end
            if score > alpha then alpha = score end
        end
        if not complete then break end
    end
    -- Reproducible root tie-break
    local best = -inf
    for i = 0, count - 1 do
        local score = root_score[work[base + i]]
        if score and score > best then best = score end
    end
    if best == -inf then
        search_profile = nil
        search_profile_key = ""
        return selected
    end
    local choices = {}
    local n = 0
    for i = 0, count - 1 do
        local move = work[base + i]
        if root_score[move] and root_score[move] >= best - profile.variety then
            n = n + 1
            choices[n] = move
        end
    end
    local selected_choice =
        choices[engine.random and math.floor(engine.random() * n) + 1 or 1]
    search_profile = nil
    search_profile_key = ""
    return selected_choice
end
-- Translate signed internal pieces to the scene's piece-state identifiers
local function draw()
    for s = 1, 64 do
        local p = b[s]
        local shown = p == 0 and 0 or (p > 0 and 8 - p or 14 + p)
        engine.board_set(s - 1, shown)
    end
end
-- Rule checks and host lifecycle
local function winner(who) return who == 1 and 1 or 2 end
local function initial_position()
    for s = 1, 64 do
        b[s] = 0
    end
    local back = { 4, 2, 3, 5, 6, 3, 2, 4 }
    for f = 0, 7 do
        b[1 + f], b[9 + f] = -back[f + 1], -1
        b[49 + f], b[57 + f] = 1, back[f + 1]
    end
    side, castle, ep, half, ply, done = 1, 15, 0, 0, 0, false
end
local function has_move(base, count, from, to, promotion)
    for i = 0, count - 1 do
        local f, t, p = unpackmove(work[base + i])
        if f == from and t == to and (promotion == nil or p == promotion) then
            return true
        end
    end
    return false
end
-- Count legal leaf nodes with the same apply and undo path used by the searcher
local function perft(limit)
    local depth, total = 0, 0
    pf_stage[1] = 0
    while true do
        if depth == limit then
            total = total + 1
            undo(64 + depth)
            depth = depth - 1
        else
            local frame = depth + 1
            if pf_stage[frame] ~= 1 then
                local base, count = legal_into(side, depth)
                pf_stage[frame] = 1
                pf_base[frame] = base
                pf_count[frame] = count
                pf_index[frame] = 0
            end
            if pf_index[frame] < pf_count[frame] then
                pf_index[frame] = pf_index[frame] + 1
                apply(
                    work[pf_base[frame] + pf_index[frame] - 1],
                    64 + depth + 1
                )
                depth = depth + 1
                pf_stage[depth + 1] = 0
            elseif depth == 0 then
                return total
            else
                undo(64 + depth)
                depth = depth - 1
            end
        end
    end
end
-- Check move generation and the evaluator before a visible game begins
local function rule_selfcheck()
    validate_profiles()
    local expected = { 20, 400, 8902, 197281 }
    for d = 1, 4 do
        local got = perft(d)
        if got ~= expected[d] then
            error("perft depth " .. d .. " got " .. got)
        end
    end
    for s = 1, 64 do
        b[s] = 0
    end
    side, castle, ep, half = 1, 3, 0, 0
    b[61] = 6
    b[64] = 4
    b[5] = -6
    local base, count = legal_into(1, 0)
    if not has_move(base, count, 61, 63, nil) then
        error("kingside castle missing")
    end
    for s = 1, 64 do
        b[s] = 0
    end
    side, castle, ep = 1, 0, 20
    b[61] = 6
    b[5] = -6
    b[29] = 1
    b[28] = -1
    base, count = legal_into(1, 0)
    if not has_move(base, count, 29, 20, nil) then
        error("en passant missing")
    end
    for s = 1, 64 do
        b[s] = 0
    end
    side, castle, ep = 1, 0, 0
    b[61] = 6
    b[5] = -6
    b[9] = 1
    base, count = legal_into(1, 0)
    for p = 2, 5 do
        if not has_move(base, count, 9, 1, p) then
            error("promotion missing " .. p)
        end
    end
    b[9] = 0
    if not insufficient() then error("insufficient material missing") end
    b[10] = 3
    b[11] = 2
    if insufficient() then error("bishop knight wrongly insufficient") end
    for s = 1, 64 do
        b[s] = 0
    end
    side, castle, ep = 1, 0, 20
    b[61] = 6
    b[5] = -6
    b[29] = 1
    b[28] = -1
    if ep_for_key() ~= 20 then
        error("legal en passant missing from repetition key")
    end
    b[5] = -4
    b[1] = -6
    if ep_for_key() ~= 0 then
        error("pinned en passant leaked into repetition key")
    end
    if
        profile_for(1) ~= players.Joe
        or profile_for(-1) ~= players.Gina
        or players.Linh.search_nodes <= players.Zain.search_nodes
    then
        error("player profiles missing")
    end
    for s = 1, 64 do
        b[s] = 0
    end
    side, castle, ep = 1, 0, 0
    b[61] = 6
    b[5] = -6
    b[36] = 2
    b[21] = -5
    b[26] = -4
    if tactical_evaluate(1, players.Zain) <= 0 then
        error("fork evaluator missing")
    end
    search_profile = players.Joe
    search_profile_key = "Joe"
    local joe_search_key = search_key()
    search_profile = players.Gina
    search_profile_key = "Gina"
    if search_key() == joe_search_key then
        error("player search cache not isolated")
    end
    search_profile = nil
    search_profile_key = ""
    initial_position()
end
local function finish()
    local _, move_count = legal_into(side, 24)
    if move_count == 0 then
        done = true
        engine.result(incheck(side, "finish lost king") and winner(-side) or 0)
        return true
    end
    if insufficient() then
        done = true
        engine.result(0)
        return true
    end
    -- Threefold/50-move claims; fivefold/75-move automatic draws
    local repetitions = history[key()] or 0
    if repetitions >= 5 or half >= 150 or repetitions >= 3 or half >= 100 then
        done = true
        engine.result(0)
        return true
    end
    return false
end
-- Rebind tables here because the Lua host retains only globals exposed to it
function on_setup()
    -- CLX requires tables to stay rooted
    b = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    }
    history = {}
    uf = {}
    ut = {}
    upiece = {}
    ucap = {}
    ucs = {}
    ucastle = {}
    uep = {}
    uhalf = {}
    urf = {}
    urt = {}
    work = {}
    tt_depth = {}
    tt_score = {}
    tt_move = {}
    tt_flag = {}
    root_score = {}
    st_stage = {}
    st_left = {}
    st_alpha = {}
    st_beta = {}
    st_alpha0 = {}
    st_beta0 = {}
    st_best = {}
    st_index = {}
    st_count = {}
    st_base = {}
    st_hash = {}
    pf_stage = {}
    pf_index = {}
    pf_count = {}
    pf_base = {}
    q_stage = {}
    q_left = {}
    q_alpha = {}
    q_beta = {}
    q_index = {}
    q_count = {}
    q_base = {}
    ---@diagnostic disable-next-line: lowercase-global
    chess_roots = {
        b,
        history,
        uf,
        ut,
        upiece,
        ucap,
        ucs,
        ucastle,
        uep,
        uhalf,
        urf,
        urt,
        work,
        tt_depth,
        tt_score,
        tt_move,
        tt_flag,
        root_score,
        st_stage,
        st_left,
        st_alpha,
        st_beta,
        st_alpha0,
        st_beta0,
        st_best,
        st_index,
        st_count,
        st_base,
        st_hash,
        pf_stage,
        pf_index,
        pf_count,
        pf_base,
        q_stage,
        q_left,
        q_alpha,
        q_beta,
        q_index,
        q_count,
        q_base,
    }
    initial_position()
    rule_selfcheck()
    local initial, initial_count = pseudo(1)
    if initial_count ~= 20 then error("initial pseudo moves") end
    local probe = initial[1]
    apply(probe, 1)
    local checked = attacked(king(1), -1)
    undo(1)
    if checked then error("initial check") end
    local _, legal_count = legal(1)
    if legal_count ~= 20 then error("initial legal moves") end
    history[key()] = 1
    draw()
end
-- Each timeline turn applies a searched move and records the resulting position
function on_turn(step)
    if done then return end
    if finish() then return end
    local move = choose()
    apply(move, 0)
    ply = ply + 1
    local k = key()
    history[k] = (history[k] or 0) + 1
    draw()
    if not finish() and (ply >= 600 or step >= 600) then
        error("chess exceeded 600 plies without a result")
    end
end
