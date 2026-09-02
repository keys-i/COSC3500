"""Regression checks for Conway's sparse, inherited micro-kingdoms."""

import csv
import sys
from pathlib import Path

SHARES = {
    1: (4, 0, 0),
    2: (0, 4, 0),
    3: (0, 0, 4),
    4: (3, 1, 0),
    5: (1, 3, 0),
    6: (3, 0, 1),
    7: (1, 0, 3),
    8: (0, 3, 1),
    9: (0, 1, 3),
    10: (2, 2, 0),
    11: (2, 0, 2),
    12: (0, 2, 2),
}
assert all(sum(share) == 4 for share in SHARES.values())


def components(cells, state=None):
    """Return 8-neighbour kingdom sizes, optionally for one pure colour."""
    remaining = {
        point
        for point, value in cells.items()
        if state is None or value == state
    }
    sizes = []
    while remaining:
        stack, size = [remaining.pop()], 0
        while stack:
            x, y = stack.pop()
            size += 1
            for neighbour in (
                (x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)
            ):
                if neighbour != (x, y) and neighbour in remaining:
                    remaining.remove(neighbour)
                    stack.append(neighbour)
        sizes.append(size)
    return sizes


frames = {0: {}, 144: {}, 720: {}, 4320: {}}
width = height = 0
with Path(sys.argv[1]).open(newline="", encoding="utf-8") as source:
    for row in csv.DictReader(source):
        if row["record"] != "entity":
            continue
        frame = int(row["frame"])
        if frame not in frames:
            continue
        state = int(row["state_id"])
        assert state in SHARES
        width, height = int(row["world_width"]), int(row["world_height"])
        frames[frame][(int(float(row["x"])), int(float(row["y"])))] = state

initial, marriages, campaign, final = (
    frames[0],
    frames[144],
    frames[720],
    frames[4320],
)
area = width * height
assert len(initial) < area * 0.03
assert set(initial.values()) <= {1, 2, 3}
initial_counts = [
    sum(state == owner for state in initial.values()) for owner in (1, 2, 3)
]
assert initial_counts[0] < initial_counts[2] < initial_counts[1]
initial_kingdoms = components(initial)
assert len(initial_kingdoms) == 180 and max(initial_kingdoms) <= 4
assert (
    max(components(initial, 1))
    < max(components(initial, 3))
    < max(components(initial, 2))
)

mixed = {state for state in marriages.values() if state >= 4}
assert mixed == set(range(4, 13))
final_mixed = {state for state in final.values() if state >= 4}
assert sum(state >= 4 for state in final.values()) > len(final) * 0.8
assert all(final_mixed & pair for pair in ({4, 5, 10}, {6, 7, 11}, {8, 9, 12}))
final_kingdoms = components(final)
assert len(final) < area / 2 and len(final_kingdoms) >= 250
assert max(final_kingdoms) < len(final) * 0.2

ancestry = [
    sum(SHARES[state][line] for state in final.values()) for line in range(3)
]
campaign_ancestry = [
    sum(SHARES[state][line] for state in campaign.values()) for line in range(3)
]
total = sum(ancestry)
assert campaign_ancestry[0] < campaign_ancestry[2] < campaign_ancestry[1]
assert 0.60 < ancestry[0] / total < 0.65
assert ancestry[0] > ancestry[1] > ancestry[2]
