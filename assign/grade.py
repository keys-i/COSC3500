import numpy as np

points = [
    tuple(map(float, input(f"x{i}, y{i}: ").split()))
    for i in range(1, 6)
]

print("\nEquations:")
for x, y in points:
    print(f"{x**2:g}a + {x:g}b + c = {y:g}")

A = np.array([[x**2, x, 1] for x, y in points])
Y = np.array([y for x, y in points])

a, b, c = np.linalg.lstsq(A, Y, rcond=None)[0]

print("\nSolution:")
print(f"a = {a:g}")
print(f"b = {b:g}")
print(f"c = {c:g}")
if a == 0:
    print("Linear fit; no parabola form.")
else:
    h = -b / (2 * a)
    k = c - b * b / (4 * a)
    assert np.allclose(a * (A[:, 1] - h) ** 2 + k, A @ [a, b, c])
    print(f"(x {-h:+.6f})² = {1 / a:.6f}(y {-k:+.6f})")
