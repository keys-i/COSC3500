-- Heston option-pricing PDE callbacks
-- The shared solver handles stepping while this module supplies model data
-- Field 0 and field 1 evolve call and put prices on the same spot-variance grid
-- Annual rates, spot-price units, and maturity in years
local K = 5250
local r = 0.03
local q = 0
local kappa = 2
local theta = 0.04
local sigma = 0.35
local rho = -0.70
local Smax = 42000

-- Field 0 is the call and field 1 is the put over the same spot-variance grid
-- Payoff at maturity supplies the backward-time solver's initial surface
function pde_initial(field, S, v)
    if field == 0 then return math.max(S - K, 0) end
    return math.max(K - S, 0)
end

-- Return Heston terms in the solver's fixed argument order
function pde_coefficients(field, S, v)
    -- Diffusion, drift, reaction, source in solver order
    return 0.5 * v * S * S,
        rho * sigma * v * S,
        0.5 * sigma * sigma * v,
        (r - q) * S,
        kappa * (theta - v),
        -r,
        0
end

-- Spot edges use option asymptotes while variance edges use zero normal flux
-- Boundary side 0 and 1 are spot edges while side 2 and 3 are variance edges
function pde_boundary(field, side, coordinate, tau)
    if side == 0 then
        if field == 0 then return 0, 0 end
        return 0, K * math.exp(-r * tau)
    end
    if side == 1 then
        if field == 0 then
            return 0, Smax * math.exp(-q * tau) - K * math.exp(-r * tau)
        end
        return 0, 0
    end
    if side == 2 then return 2, 0 end
    if field == 0 then return 0, coordinate * math.exp(-q * tau) end
    return 0, K * math.exp(-r * tau)
end

-- Reference values catch coefficient or boundary regressions at the probe point
function pde_reference(field)
    if field == 0 then return 195.51754059391 end
    return 206.39690015788
end
