local pi = 3.141592653589793

function pde_initial(field, x, y) return math.sin(pi * x) * math.sin(pi * y) end

function pde_coefficients(field, x, y) return 1, 0, 1, 0, 0, 0, 0 end

function pde_boundary(field, side, coordinate, tau) return 0, 0 end

function pde_reference(field) return math.exp(-2 * pi * pi * 0.01) end
