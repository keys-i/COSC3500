function pde_initial(field, x, y) return 1 end

function pde_coefficients(field, x, y) return 0, 0, 0, 0, 0, 1, 0 end

function pde_boundary(field, side, coordinate, tau) return 2, 0 end

function pde_reference(field) return math.exp(0.1) end
