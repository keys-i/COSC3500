"""Check Conway transitions against the documented state rules

The test reads a snapshot and independently applies the first generation rule
table
Later counts confirm each faction persists and the expected advantage appears
"""

import csv
import sys
from collections import Counter
from pathlib import Path

snapshot = Path(sys.argv[1])
wanted = (0, 1, 60, 180, 360, 720)
counts = {frame: Counter() for frame in wanted}
states = {0: {}, 1: {}}

with snapshot.open(newline="", encoding="utf-8") as source:
    for row in csv.DictReader(source):
        frame = int(row["frame"])
        if frame not in counts or not row["state_id"]:
            continue
        state = int(row["state_id"])
        point = (int(float(row["x"])), int(float(row["y"])))
        # States 1--3 are live cells while higher values are fading trails
        if state in (1, 2, 3):
            counts[frame][state] += 1
        if frame in states:
            states[frame][point] = state

initial = sum(counts[0].values())
assert 4000 < initial < 6000
assert all(counts[0][state] > 1000 for state in (1, 2, 3))
assert all(sum(counts[frame].values()) > 0 for frame in wanted)
assert len({tuple(counts[frame].values()) for frame in wanted}) > 2
assert counts[720][1] > max(counts[720][2], counts[720][3])


def expected(current, red, blue, yellow):
    """Return one cell's next state from its state and neighbour counts"""
    # Mirror the Lua rule table for one generation without calling the renderer
    live = red + blue + yellow
    if current in (2, 3) and red >= 2 and 2 <= live <= 3:
        return 1
    if current == 2 and yellow >= 2 and 2 <= live <= 4:
        return 3
    if current == 1 and 1 <= live <= 3:
        return 1
    if current == 2 and live in (2, 3):
        return 2
    if current == 3 and 2 <= live <= 4:
        return 3
    if current == 0 and live == 2 and red == 2:
        return 1
    if current == 0 and live == 3:
        if yellow and red and blue:
            return 3
        if blue >= 2 and blue > red and blue > yellow:
            return 2
        return 1 if red >= 2 else 3
    if current in (1, 2, 3):
        return 4 + (current - 1) * 3
    if 4 <= current <= 12:
        return 0 if (current - 4) % 3 == 2 else current + 1
    return 0


for y in range(140):
    for x in range(200):
        neighbours = [
            states[0].get((x + dx, y + dy), 0)
            for dx in (-1, 0, 1)
            for dy in (-1, 0, 1)
            if dx or dy
        ]
        counts_by_owner = tuple(neighbours.count(owner) for owner in (1, 2, 3))
        assert states[1].get((x, y), 0) == expected(
            states[0].get((x, y), 0), *counts_by_owner
        )
