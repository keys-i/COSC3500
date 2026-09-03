#!/usr/bin/env python3
"""Collect M1 benchmark results and render report files.

The script builds a separate release tree, checks each benchmark row, then
writes CSV, SVG, and Markdown.
The page experiment is separate because Linux must report the actual backing
used by the benchmark process.
"""

from __future__ import annotations

import argparse
import csv
import html
import io
import math
import os
import platform
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from datetime import UTC, datetime
from itertools import pairwise
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
configured_out = Path(os.environ.get("M1_BENCH_OUT", "results/bench"))
OUT = (
    configured_out if configured_out.is_absolute() else ROOT / configured_out
).resolve()
PRIMARY_CASE = "cellular/conway/1m"
PAGE_CASE = "cellular/conway/10m"
LEVEL_CASES = (("Predator-prey 100K", "continuous/predator-prey/100k"),)
SCALING_CASES = (
    ("1K", 1_000, "cellular/conway/1k"),
    ("10K", 10_000, "cellular/conway/10k"),
    ("100K", 100_000, "cellular/conway/100k"),
    ("1M", 1_000_000, PRIMARY_CASE),
    ("10M", 10_000_000, "cellular/conway/10m"),
    ("100M", 100_000_000, "cellular/conway/100m"),
    ("1B", 1_000_000_000, "cellular/conway/1b"),
)
BENCH_FIELDS = (
    "case",
    "samples",
    "median_ns_per_unit",
    "ci_low_ns_per_unit",
    "ci_high_ns_per_unit",
    "cv_percent",
    "unit",
    "state_bytes",
    "pair_evaluations",
    "pair_list_rebuilds",
    "pair_list_bytes",
    "peak_rss_bytes",
    "throughput_munits_per_s",
    "checksum",
    "verified",
    "page_policy",
    "host_page_bytes",
    "advised_bytes",
    "anon_huge_bytes",
    "page_backing_verified",
)
SCALING_FIELDS = ("size", "cells", "index_width_bits", *BENCH_FIELDS)
LEVEL_FIELDS = ("opt_level", "workload", *BENCH_FIELDS)


def positive(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def run(
    command: list[str],
    *,
    capture: bool = False,
    env: dict[str, str] | None = None,
) -> str:
    """Run one repository command and return captured output when requested"""
    result = subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=capture,
        env=env,
    )
    return result.stdout


def configure(level: int | None = None) -> Path:
    """Configure and build the isolated executable used for reports"""
    suffix = "" if level is None else f"-l{level}"
    build = ROOT / f"build/evidence{suffix}"
    command = ["cmake", "--preset", "evidence", "-S", str(ROOT)]
    if level is not None:
        command.extend(["-B", str(build), f"-DM1_OPT_LEVEL={level}"])
    run(command)
    run(
        [
            "cmake",
            "--build",
            str(build),
            "--target",
            "m1",
            "hpc_bench",
            "benchmark",
        ]
    )
    return build


def benchmark(
    build: Path,
    case: str,
    samples: int,
    minimum_ms: int,
    *,
    env: dict[str, str] | None = None,
) -> dict[str, str]:
    """Run one benchmark case and check its CSV row"""
    # The benchmark executable emits one checked row for the requested case
    output = run(
        [
            str(build / "bench/hpc_bench"),
            "--binary",
            str(build / "bin/m1"),
            "--case",
            case,
            "--samples",
            str(samples),
            "--minimum-case-ms",
            str(minimum_ms),
            "--csv",
        ],
        capture=True,
        env=env,
    )
    # Require the exact CSV schema before building the report
    reader = csv.DictReader(io.StringIO(output))
    rows = list(reader)
    if tuple(reader.fieldnames or ()) != BENCH_FIELDS or len(rows) != 1:
        raise ValueError(f"{case}: benchmark returned invalid CSV")
    if rows[0]["verified"] != "true":
        raise ValueError(f"{case}: checksum was not verified")
    return rows[0]


def write_csv(
    target: Path, fields: tuple[str, ...], rows: list[dict[str, str]]
) -> None:
    """Write checked report rows with an explicit field order"""
    with target.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def scaling_row(
    build: Path,
    entry: tuple[str, int, str],
    samples: int,
    minimum_ms: int,
) -> dict[str, str]:
    """Measure one Conway scale and add its size metadata."""
    label, cells, case = entry
    print(f"benchmark {case}", flush=True)
    return {
        "size": label,
        "cells": str(cells),
        "index_width_bits": "32",
        **benchmark(build, case, samples, minimum_ms),
    }


def collect(samples: int, minimum_ms: int) -> list[dict[str, str]]:
    """Build, test, time the fixed scaling cases, and persist scaling.csv"""
    if "SLURM_JOB_ID" not in os.environ:
        raise ValueError("1K through 1B scaling must run inside Slurm")
    # Build and test first so every chart row comes from a verified executable
    OUT.mkdir(parents=True, exist_ok=True)
    build = configure(7)
    run(
        [
            "ctest",
            "--test-dir",
            str(build),
            "--output-on-failure",
            "-R",
            "^hpc\\.benchmark$",
        ]
    )
    scaling = [
        scaling_row(build, entry, samples, minimum_ms)
        for entry in SCALING_CASES
    ]
    write_csv(OUT / "scaling.csv", SCALING_FIELDS, scaling)
    return scaling


def collect_levels(
    samples: int,
    minimum_ms: int,
    start_level: int = 0,
    end_level: int = 7,
) -> tuple[list[dict[str, str]], Path]:
    """Build requested source levels and time the registered M1 workloads"""
    if not 0 <= start_level <= end_level <= 7:
        raise ValueError("level range must be within 0..7")
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    final_build = ROOT / f"build/evidence-l{end_level}"
    for level in range(start_level, end_level + 1):
        build = configure(level)
        for workload, case in LEVEL_CASES:
            print(f"benchmark L{level} {case}", flush=True)
            row = benchmark(build, case, samples, minimum_ms)
            rows.append({"opt_level": f"L{level}", "workload": workload, **row})
        final_build = build
    if start_level == 0 and end_level == 7:
        target = OUT / "levels.csv"
        for workload, _ in LEVEL_CASES:
            checksums = {
                row["checksum"] for row in rows if row["workload"] == workload
            }
            if len(checksums) != 1:
                raise ValueError(f"{workload}: L0 through L7 checksums differ")
        (OUT / "levels.svg").write_text(levels_svg(rows), encoding="utf-8")
    else:
        target = OUT / f"levels-{start_level}-{end_level}.csv"
    write_csv(target, LEVEL_FIELDS, rows)
    return rows, final_build


def text(
    x: float,
    y: float,
    value: str,
    size: int = 16,
    anchor: str = "middle",
    colour: str = "#24364b",
    weight: int = 400,
) -> str:
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" '
        f'font-family="Arial, sans-serif" font-size="{size}" '
        f'font-weight="{weight}" fill="{colour}">{html.escape(value)}</text>'
    )


def chart_frame(
    title: str,
    subtitle: str,
    body: list[str],
    width: int = 1200,
    height: int = 675,
) -> str:
    """Wrap SVG chart content in the shared canvas and heading treatment"""
    return "".join(
        [
            (
                f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
                f'height="{height}" viewBox="0 0 {width} {height}" '
                'role="img" aria-labelledby="chart-title chart-description">'
            ),
            f'<title id="chart-title">{html.escape(title)}</title>',
            f'<desc id="chart-description">{html.escape(subtitle)}</desc>',
            '<rect width="100%" height="100%" fill="#f3eadf"/>',
            '<rect width="100%" height="84" fill="#2c2118"/>',
            text(34, 35, title, 25, "start", "#ffffff", 700),
            text(34, 64, subtitle, 14, "start", "#e8d9c6"),
            *body,
            "</svg>",
        ]
    )


def diagram_frame(title: str, description: str, body: list[str]) -> str:
    """Wrap one square slide figure in the report's visual language."""
    return "".join(
        [
            (
                '<svg xmlns="http://www.w3.org/2000/svg" width="800" '
                'height="800" viewBox="0 0 800 800" role="img" '
                'aria-labelledby="figure-title figure-description">'
            ),
            f'<title id="figure-title">{html.escape(title)}</title>',
            (
                f'<desc id="figure-description">'
                f"{html.escape(description)}</desc>"
            ),
            (
                '<defs><marker id="arrow" markerWidth="9" '
                'markerHeight="9" refX="8" refY="4" orient="auto" '
                'markerUnits="userSpaceOnUse" viewBox="0 0 9 8">'
                '<path d="M0,0 L9,4 L0,8 z" '
                'fill="#c75146"/></marker></defs>'
            ),
            '<rect width="800" height="800" rx="28" fill="#f3eadf"/>',
            (
                '<rect x="18" y="18" width="764" height="764" rx="24" '
                'fill="#fffaf2" stroke="#2c2118" stroke-width="4"/>'
            ),
            (
                '<path d="M18 42 Q18 18 42 18 H758 Q782 18 782 42 '
                'V104 H18 Z" fill="#2c2118"/>'
            ),
            text(400, 69, title, 29, "middle", "#ffffff", 700),
            *body,
            text(400, 755, description, 15, "middle", "#66584c", 700),
            "</svg>",
        ]
    )


def arrow(x1: float, y1: float, x2: float, y2: float) -> str:
    """Draw the shared red directional arrow used by slide diagrams."""
    return (
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        'stroke="#c75146" stroke-width="3" stroke-linecap="round" '
        'marker-end="url(#arrow)"/>'
    )


def path_arrow(path: str, *, dashed: bool = False) -> str:
    """Draw a routed connector whose path carries the meaning."""
    dash = ' stroke-dasharray="10 9"' if dashed else ""
    return (
        f'<path d="{path}" fill="none" stroke="#c75146" '
        f'stroke-width="3" stroke-linecap="round" '
        f'stroke-linejoin="round"{dash} marker-end="url(#arrow)"/>'
    )


def static_slide_figures() -> dict[str, str]:
    """Return the mechanism figures that do not depend on benchmark CSV."""
    figures: dict[str, str] = {}

    body = [
        (
            '<rect x="82" y="176" width="636" height="462" rx="18" '
            'fill="#edf5ef" stroke="#2c2118" stroke-width="4"/>'
        ),
    ]
    for offset in range(1, 8):
        coordinate = 82 + offset * 79.5
        body.extend(
            [
                (
                    f'<line x1="{coordinate}" y1="176" x2="{coordinate}" '
                    'y2="638" stroke="#cfbfac" stroke-width="2"/>'
                ),
                (
                    f'<line x1="82" y1="{176 + offset * 57.75}" '
                    f'x2="718" y2="{176 + offset * 57.75}" '
                    'stroke="#cfbfac" stroke-width="2"/>'
                ),
            ]
        )
    for x, y in (
        (128, 228),
        (199, 302),
        (276, 213),
        (354, 357),
        (446, 228),
        (534, 321),
        (654, 240),
        (143, 470),
        (240, 551),
        (328, 493),
        (421, 576),
        (504, 475),
        (609, 554),
        (676, 420),
    ):
        body.append(f'<circle cx="{x}" cy="{y}" r="12" fill="#2a9d8f"/>')
    for x, y in (
        (174, 392),
        (309, 285),
        (411, 432),
        (570, 205),
        (595, 394),
        (696, 589),
    ):
        body.append(
            f'<path d="M{x} {y - 15} L{x - 14} {y + 12} '
            f'L{x + 14} {y + 12} Z" fill="#c75146"/>'
        )
    body.extend(
        [
            (
                '<rect x="95" y="125" width="250" height="38" rx="19" '
                'fill="#2a9d8f"/>'
            ),
            text(220, 151, "50,000 prey", 18, "middle", "#ffffff", 700),
            (
                '<rect x="455" y="125" width="250" height="38" rx="19" '
                'fill="#c75146"/>'
            ),
            text(580, 151, "50,000 hunters", 18, "middle", "#ffffff", 700),
            path_arrow("M70 260 C32 260 32 550 70 550"),
            path_arrow("M730 550 C768 550 768 260 730 260"),
            text(400, 681, "wrapped 1,000,000 × 1,000,000 world", 18),
        ]
    )
    figures["workload.svg"] = diagram_frame(
        "Fixed predator–prey workload", "10 steps · radius 80 · seed 7", body
    )

    body = [
        (
            '<rect x="58" y="145" width="190" height="92" rx="14" '
            'fill="#e6eef5" stroke="#486581" stroke-width="3"/>'
        ),
        text(153, 181, "Scenario", 19, "middle", "#24364b", 700),
        text(153, 211, "100K agents", 16),
        (
            '<rect x="305" y="145" width="190" height="92" rx="14" '
            'fill="#edf5ef" stroke="#2a9d8f" stroke-width="3"/>'
        ),
        text(400, 181, "Run seed", 19, "middle", "#24364b", 700),
        text(400, 211, "7", 18),
        (
            '<rect x="552" y="145" width="190" height="92" rx="14" '
            'fill="#f7ead8" stroke="#d39b2f" stroke-width="3"/>'
        ),
        text(647, 181, "Build", 19, "middle", "#24364b", 700),
        text(647, 211, "one level only", 16),
        path_arrow("M153 237 C153 280 260 285 310 318"),
        path_arrow("M400 237 V318"),
        path_arrow("M647 237 C647 280 540 285 490 318"),
        (
            '<rect x="112" y="330" width="576" height="76" rx="18" '
            'fill="#2c2118"/>'
        ),
        text(
            400,
            377,
            "benchmark tuple: scenario + seed + build",
            21,
            "middle",
            "#ffffff",
            700,
        ),
        path_arrow("M160 406 V438 Q160 450 148 450 H114 V455"),
    ]
    for level in range(8):
        x = 82 + level * 91
        colour = "#d39b2f" if level == 7 else "#486581"
        body.extend(
            [
                (
                    f'<rect x="{x}" y="464" width="64" height="64" rx="12" '
                    f'fill="{colour}"/>'
                ),
                text(x + 32, 504, f"L{level}", 19, "middle", "#ffffff", 700),
            ]
        )
        if level < 7:
            body.append(arrow(x + 65, 496, x + 83, 496))
    body.extend(
        [
            (
                '<path d="M82 540 V552 H783 V540" fill="none" '
                'stroke="#c75146" stroke-width="3"/>'
            ),
            path_arrow("M432 552 C432 575 400 575 400 600"),
            (
                '<rect x="173" y="611" width="454" height="76" rx="38" '
                'fill="#edf5ef" stroke="#2a9d8f" stroke-width="4"/>'
            ),
            text(
                400,
                647,
                "checksum + counters must match L0",
                20,
                "middle",
                "#236d64",
                700,
            ),
            text(400, 674, "reject if checksum or counters differ", 14),
        ]
    )
    figures["controls.svg"] = diagram_frame(
        "Controlled comparison",
        "fixed input · fixed seed · correctness gate",
        body,
    )

    mechanisms = (
        ("L0", "linked list"),
        ("L1", "contiguous"),
        ("L2", "sparse reset"),
        ("L3", "type split"),
        ("L4", "fast stencil"),
        ("L5", "pair kernel"),
        ("L6", "Verlet cache"),
        ("L7", "CSR + certs"),
    )
    body = []
    for index, (level, mechanism) in enumerate(mechanisms):
        column = index % 2
        row = index // 2
        x = 78 + column * 370
        y = 135 + row * 137
        colour = "#d39b2f" if index == 7 else "#486581"
        body.extend(
            [
                (
                    f'<rect x="{x}" y="{y}" width="275" height="88" rx="16" '
                    f'fill="{colour}"/>'
                ),
                text(x + 44, y + 53, level, 24, "middle", "#ffffff", 700),
                text(x + 84, y + 53, mechanism, 18, "start", "#ffffff", 700),
            ]
        )
        if column == 0:
            body.append(arrow(x + 285, y + 44, x + 355, y + 44))
        elif index < 7:
            next_y = y + 137
            body.append(
                path_arrow(
                    f"M585 {y + 88} V{y + 112} "
                    f"Q585 {y + 121} 576 {y + 121} H224 "
                    f"Q215 {y + 121} 215 {y + 130} V{next_y - 10}"
                )
            )
    figures["optimisation-map.svg"] = diagram_frame(
        "Cumulative optimisation ladder",
        "M1_OPT_LEVEL=N enables mechanisms L0 through LN",
        body,
    )

    body = [
        '<rect x="68" y="145" width="210" height="82" rx="14" fill="#486581"/>',
        text(173, 181, "heads[cell 12]", 18, "middle", "#ffffff", 700),
        text(173, 210, "64-bit index", 15, "middle", "#dce7ef"),
        arrow(278, 186, 300, 186),
    ]
    for index, entity in enumerate((9, 31, 44)):
        x = 352 + index * 136
        body.extend(
            [
                (
                    f'<circle cx="{x}" cy="186" r="42" fill="#f7ead8" '
                    'stroke="#c75146" stroke-width="4"/>'
                ),
                text(x, 193, str(entity), 22, "middle", "#2c2118", 700),
                text(x, 260, f"next[{entity}]", 14, "middle", "#66584c"),
            ]
        )
        if index < 2:
            body.append(arrow(x + 47, 186, x + 84, 186))
    body.extend(
        [
            arrow(666, 186, 727, 186),
            text(746, 194, "∅", 30, "middle", "#66584c", 700),
            text(
                400,
                338,
                "cell 12 traversal: heads[12] → next[9] → next[31]",
                22,
                "middle",
                "#392d24",
                700,
            ),
        ]
    )
    for index, value in enumerate(("9", "91", "31", "7", "44", "18")):
        x = 91 + index * 106
        body.extend(
            [
                (
                    f'<rect x="{x}" y="403" width="82" height="82" rx="9" '
                    'fill="#e9dfd1" stroke="#8b7969" stroke-width="2"/>'
                ),
                text(x + 41, 451, value, 20, "middle", "#392d24", 700),
            ]
        )
    body.extend(
        [
            path_arrow("M132 394 C175 350 300 350 344 394", dashed=True),
            path_arrow("M344 394 C387 350 512 350 556 394", dashed=True),
            text(
                400,
                550,
                "loads jump from entity 9 to 31 to 44",
                18,
                "middle",
                "#c75146",
                700,
            ),
            (
                '<rect x="201" y="619" width="398" height="62" rx="31" '
                'fill="#2c2118"/>'
            ),
            text(
                400,
                658,
                "visit order: 9 → 31 → 44 → ∅",
                19,
                "middle",
                "#ffffff",
                700,
            ),
        ]
    )
    figures["l0-linked.svg"] = diagram_frame(
        "L0 · linked cell lists",
        "heads[cell] and next[entity] are uint64_t",
        body,
    )

    body = [
        text(72, 156, "offsets", 18, "start", "#392d24", 700),
    ]
    for index, value in enumerate((0, 3, 3, 6, 8)):
        x = 178 + index * 105
        body.extend(
            [
                (
                    f'<rect x="{x}" y="125" width="88" height="64" rx="8" '
                    'fill="#e6eef5" stroke="#486581" stroke-width="3"/>'
                ),
                text(x + 44, 165, str(value), 20, "middle", "#24364b", 700),
            ]
        )
    body.extend(
        [
            path_arrow("M222 199 C222 235 140 235 140 276"),
            path_arrow("M327 199 V276"),
            text(72, 325, "members", 18, "start", "#392d24", 700),
        ]
    )
    for index, value in enumerate((9, 31, 44, 7, 18, 52, 5, 81)):
        x = 106 + index * 76
        fill = "#2a9d8f" if index < 3 else "#d9e8e4"
        body.extend(
            [
                (
                    f'<rect x="{x}" y="287" width="68" height="76" rx="7" '
                    f'fill="{fill}" stroke="#236d64" stroke-width="2"/>'
                ),
                text(x + 34, 333, str(value), 18, "middle", "#173f3a", 700),
            ]
        )
    body.extend(
        [
            (
                '<path d="M106 381 V397 H326 V381" fill="none" '
                'stroke="#c75146" stroke-width="3"/>'
            ),
            text(
                216,
                424,
                "cell 0: one contiguous range",
                17,
                "middle",
                "#c75146",
                700,
            ),
            (
                '<rect x="127" y="493" width="546" height="126" rx="18" '
                'fill="#edf5ef" stroke="#2a9d8f" stroke-width="3"/>'
            ),
            text(
                400, 535, "GridIndex = uint32_t", 24, "middle", "#236d64", 700
            ),
            text(400, 571, "members[offsets[c] … offsets[c + 1])", 17),
            text(400, 599, "sequential IDs; no next-pointer load", 17),
        ]
    )
    figures["l1-contiguous.svg"] = diagram_frame(
        "L1 · contiguous cell ranges", "offsets delimit packed member IDs", body
    )

    body = []
    occupied = {(0, 1), (1, 4), (3, 0), (4, 3), (5, 5)}
    cell = 72
    for row in range(6):
        for column in range(6):
            x = 53 + column * cell
            y = 139 + row * cell
            fill = "#2a9d8f" if (row, column) in occupied else "#f3eadf"
            body.append(
                f'<rect x="{x}" y="{y}" width="64" height="64" rx="6" '
                f'fill="{fill}" stroke="#8b7969" stroke-width="2"/>'
            )
    body.extend(
        [
            text(269, 607, "36 grid cells", 17, "middle", "#66584c", 700),
            arrow(477, 319, 560, 319),
            text(622, 156, "occupied", 18, "middle", "#392d24", 700),
        ]
    )
    for index, value in enumerate((1, 10, 18, 27, 35)):
        y = 181 + index * 75
        body.extend(
            [
                (
                    f'<rect x="570" y="{y}" width="104" height="55" rx="11" '
                    'fill="#2a9d8f"/>'
                ),
                text(
                    622, y + 36, f"cell {value}", 17, "middle", "#ffffff", 700
                ),
            ]
        )
    body.extend(
        [
            (
                '<rect x="514" y="590" width="216" height="78" rx="16" '
                'fill="#2c2118"/>'
            ),
            text(622, 624, "reset 5", 23, "middle", "#ffffff", 700),
            text(622, 651, "not all 36", 16, "middle", "#e8d9c6"),
        ]
    )
    figures["l2-sparse.svg"] = diagram_frame(
        "L2 · sparse grid reset", "touch only cells occupied last frame", body
    )

    body = [
        (
            '<rect x="75" y="138" width="650" height="130" rx="18" '
            'fill="#e9dfd1" stroke="#2c2118" stroke-width="4"/>'
        ),
        text(
            400,
            174,
            "one cell's contiguous range",
            18,
            "middle",
            "#392d24",
            700,
        ),
    ]
    values = (
        ("P2", "#2a9d8f"),
        ("P8", "#2a9d8f"),
        ("P9", "#2a9d8f"),
        ("H1", "#c75146"),
        ("H4", "#c75146"),
        ("H7", "#c75146"),
    )
    for index, (value, colour) in enumerate(values):
        x = 103 + index * 99
        body.extend(
            [
                (
                    f'<rect x="{x}" y="193" width="82" height="58" rx="8" '
                    f'fill="{colour}"/>'
                ),
                text(x + 41, 231, value, 18, "middle", "#ffffff", 700),
            ]
        )
    body.extend(
        [
            (
                '<line x1="400" y1="187" x2="400" y2="259" '
                'stroke="#2c2118" stroke-width="6"/>'
            ),
            text(400, 302, "type_splits[cell]", 17, "middle", "#392d24", 700),
            '<circle cx="160" cy="420" r="50" fill="#2a9d8f"/>',
            text(160, 428, "PREY", 17, "middle", "#ffffff", 700),
            arrow(210, 420, 439, 420),
            (
                '<rect x="448" y="351" width="270" height="138" rx="18" '
                'fill="#f8e2df" stroke="#c75146" stroke-width="4"/>'
            ),
            text(583, 398, "scan hunters only", 21, "middle", "#9f332d", 700),
            text(583, 435, "H1 · H4 · H7", 19, "middle", "#9f332d"),
            text(583, 466, "no per-member type branch", 15),
            (
                '<rect x="134" y="560" width="532" height="82" rx="18" '
                'fill="#2c2118"/>'
            ),
            text(
                400,
                598,
                "prey = [offsets[c], type_splits[c])",
                18,
                "middle",
                "#ffffff",
                700,
            ),
            text(
                400,
                625,
                "hunter = [type_splits[c], offsets[c + 1])",
                15,
                "middle",
                "#e8d9c6",
            ),
        ]
    )
    figures["l3-split.svg"] = diagram_frame(
        "L3 · type-split ranges",
        "target scan begins at type_splits[cell]",
        body,
    )

    body = []
    cell = 84
    for row in range(5):
        for column in range(5):
            x = 62 + column * cell
            y = 151 + row * cell
            selected = 1 <= row <= 3 and 1 <= column <= 3
            fill = "#f7ead8" if selected else "#edf1f4"
            if row == 2 and column == 2:
                fill = "#c75146"
            elif row == 4 and column == 4:
                fill = "#f7ead8"
            body.append(
                f'<rect x="{x}" y="{y}" width="76" height="76" rx="7" '
                f'fill="{fill}" stroke="#8b7969" stroke-width="2"/>'
            )
    body.extend(
        [
            path_arrow("M312 357 C405 357 430 207 516 207"),
            path_arrow("M474 525 C500 525 500 350 516 350"),
            text(
                268, 660, "fixed 3 × 3 neighbours", 18, "middle", "#392d24", 700
            ),
            (
                '<rect x="527" y="161" width="213" height="93" rx="15" '
                'fill="#e6eef5" stroke="#486581" stroke-width="3"/>'
            ),
            text(633, 198, "interior cell", 19, "middle", "#24364b", 700),
            text(633, 228, "direct arithmetic", 16),
            (
                '<rect x="527" y="294" width="213" height="112" rx="15" '
                'fill="#f7ead8" stroke="#d39b2f" stroke-width="3"/>'
            ),
            text(633, 333, "edge cell", 19, "middle", "#6e4c12", 700),
            text(633, 363, "preclassified", 16),
            text(633, 389, "periodic image", 16),
            path_arrow(
                "M474 545 C510 545 510 590 268 590 C26 590 26 545 52 545"
            ),
            text(
                268,
                620,
                "periodic_image[source, target]",
                16,
                "middle",
                "#c75146",
                700,
            ),
        ]
    )
    figures["l4-stencil.svg"] = diagram_frame(
        "L4 · preclassified stencil",
        "load_neighbours returns the fixed candidate cells",
        body,
    )

    body = [
        '<circle cx="215" cy="340" r="92" fill="#2a9d8f"/>',
        text(215, 331, "PREY A", 24, "middle", "#ffffff", 700),
        text(215, 365, "seeks B", 18, "middle", "#d9f1ec"),
        path_arrow("M307 318 C380 246 421 246 493 318"),
        path_arrow("M493 362 C421 434 380 434 307 362"),
        '<circle cx="585" cy="340" r="92" fill="#c75146"/>',
        text(585, 331, "HUNTER B", 22, "middle", "#ffffff", 700),
        text(585, 365, "seeks A", 18, "middle", "#f7d8d5"),
        (
            '<rect x="187" y="515" width="426" height="95" rx="18" '
            'fill="#2c2118"/>'
        ),
        text(
            400,
            553,
            "distance_squared = dx² + dy²",
            21,
            "middle",
            "#ffffff",
            700,
        ),
        text(
            400,
            586,
            "publish(A, B); publish(B, A)",
            17,
            "middle",
            "#e8d9c6",
        ),
        text(400, 672, "pair_evaluations += 1", 22, "middle", "#d39b2f", 700),
    ]
    figures["l5-pairs.svg"] = diagram_frame(
        "L5 · reciprocal pair kernel",
        "one cell-pair traversal publishes both directions",
        body,
    )

    body = [
        (
            '<circle cx="310" cy="380" r="195" fill="#edf5ef" '
            'stroke="#2a9d8f" stroke-width="5" stroke-dasharray="12 10"/>'
        ),
        (
            '<circle cx="310" cy="380" r="105" fill="#f7ead8" '
            'stroke="#d39b2f" stroke-width="5"/>'
        ),
        '<circle cx="310" cy="380" r="19" fill="#c75146"/>',
        text(310, 370, "r", 18, "middle", "#6e4c12", 700),
        text(310, 205, "r + skin", 18, "middle", "#236d64", 700),
    ]
    for x, y, colour in (
        (245, 328, "#486581"),
        (370, 325, "#486581"),
        (390, 440, "#486581"),
        (190, 453, "#486581"),
        (455, 270, "#8b5fa8"),
        (145, 275, "#8b5fa8"),
        (430, 516, "#8b5fa8"),
    ):
        body.append(f'<circle cx="{x}" cy="{y}" r="13" fill="{colour}"/>')
    body.extend(
        [
            (
                '<rect x="544" y="169" width="198" height="92" rx="16" '
                'fill="#2a9d8f"/>'
            ),
            text(643, 204, "BUILD", 20, "middle", "#ffffff", 700),
            text(643, 235, "candidate list", 16, "middle", "#d9f1ec"),
            arrow(643, 261, 643, 336),
            (
                '<rect x="544" y="345" width="198" height="92" rx="16" '
                'fill="#486581"/>'
            ),
            text(643, 380, "REUSE", 20, "middle", "#ffffff", 700),
            text(643, 411, "while motion < skin/2", 15, "middle", "#dce7ef"),
            arrow(643, 437, 643, 512),
            (
                '<rect x="544" y="521" width="198" height="92" rx="16" '
                'fill="#c75146"/>'
            ),
            text(643, 556, "REBUILD", 20, "middle", "#ffffff", 700),
            text(643, 587, "when bound expires", 15, "middle", "#f7d8d5"),
            path_arrow(
                "M742 567 H758 Q772 567 772 553 V229 Q772 215 758 215 H750"
            ),
            text(
                310,
                660,
                "rebuild_pairs = travelled ≥ skin / 2",
                18,
                "middle",
                "#236d64",
                700,
            ),
        ]
    )
    figures["l6-verlet.svg"] = diagram_frame(
        "L6 · Verlet candidate cache",
        "pairs contain candidates within cutoff + skin",
        body,
    )

    body = [
        text(69, 154, "offsets", 18, "start", "#392d24", 700),
    ]
    for index, value in enumerate((0, 3, 5, 8, 9)):
        x = 180 + index * 104
        body.extend(
            [
                (
                    f'<rect x="{x}" y="124" width="86" height="60" rx="8" '
                    'fill="#e6eef5" stroke="#486581" stroke-width="3"/>'
                ),
                text(x + 43, 162, str(value), 19, "middle", "#24364b", 700),
            ]
        )
    body.append(text(69, 284, "adjacency", 18, "start", "#392d24", 700))
    adjacency = (4, 7, 9, 0, 9, 0, 4, 7, 2)
    for index, value in enumerate(adjacency):
        x = 93 + index * 70
        fill = ("#2a9d8f", "#d39b2f", "#c75146", "#8b5fa8")[min(index // 3, 3)]
        body.extend(
            [
                (
                    f'<rect x="{x}" y="249" width="61" height="70" rx="7" '
                    f'fill="{fill}"/>'
                ),
                text(x + 30.5, 291, str(value), 18, "middle", "#ffffff", 700),
            ]
        )
    body.extend(
        [
            path_arrow("M223 194 C223 216 124 216 124 238"),
            path_arrow("M327 194 C327 216 299 216 299 238"),
            (
                '<path d="M93 328 V342 H294 V328" fill="none" '
                'stroke="#c75146" stroke-width="3"/>'
            ),
            text(
                400,
                360,
                "entity 0 range = adjacency[offsets[0] : offsets[1]]",
                19,
                "middle",
                "#392d24",
                700,
            ),
            (
                '<rect x="93" y="425" width="614" height="175" rx="20" '
                'fill="#edf5ef" stroke="#2a9d8f" stroke-width="4"/>'
            ),
            text(400, 468, "travel certificate", 23, "middle", "#236d64", 700),
            text(400, 510, "cached nearest still safe?", 20),
            (
                '<rect x="182" y="538" width="176" height="42" rx="21" '
                'fill="#2a9d8f"/>'
            ),
            text(270, 566, "YES · publish", 16, "middle", "#ffffff", 700),
            (
                '<rect x="442" y="538" width="176" height="42" rx="21" '
                'fill="#c75146"/>'
            ),
            text(530, 566, "NO · rescan slice", 16, "middle", "#ffffff", 700),
            text(
                400,
                654,
                "travelled < certified_travel[e] → reuse nearest[e]",
                18,
                "middle",
                "#d39b2f",
                700,
            ),
        ]
    )
    figures["l7-csr.svg"] = diagram_frame(
        "L7 · CSR adjacency",
        "offsets[e] and offsets[e + 1] delimit each candidate slice",
        body,
    )

    body = [
        (
            '<rect x="62" y="160" width="190" height="100" rx="16" '
            'fill="#486581"/>'
        ),
        text(157, 199, "entity updates", 18, "middle", "#ffffff", 700),
        text(157, 232, "fixed work", 16, "middle", "#dce7ef"),
        arrow(262, 210, 310, 210),
        (
            '<rect x="322" y="160" width="156" height="100" rx="16" '
            'fill="#e9dfd1" stroke="#8b7969" stroke-width="3"/>'
        ),
        text(400, 201, "÷ elapsed", 20, "middle", "#392d24", 700),
        text(400, 232, "nanoseconds", 16),
        arrow(488, 210, 536, 210),
        (
            '<rect x="548" y="160" width="190" height="100" rx="16" '
            'fill="#2a9d8f"/>'
        ),
        text(643, 199, "M updates/s", 19, "middle", "#ffffff", 700),
        text(643, 232, "× 1000", 16, "middle", "#d9f1ec"),
        text(
            400,
            321,
            "A timing row is accepted only when:",
            21,
            "middle",
            "#392d24",
            700,
        ),
    ]
    gates = (
        ("checksum", "same final state"),
        ("counters", "same work"),
        ("RSS", "child-process peak"),
        ("backing", "/proc/self/smaps"),
    )
    for index, (label, note) in enumerate(gates):
        column = index % 2
        row = index // 2
        x = 74 + column * 362
        y = 365 + row * 128
        colour = "#2a9d8f" if index < 2 else "#d39b2f"
        body.extend(
            [
                (
                    f'<rect x="{x}" y="{y}" width="290" height="91" rx="16" '
                    f'fill="{colour}"/>'
                ),
                text(
                    x + 145, y + 37, label.upper(), 18, "middle", "#ffffff", 700
                ),
                text(x + 145, y + 68, note, 15, "middle", "#ffffff"),
            ]
        )
    body.extend(
        [
            (
                '<rect x="180" y="628" width="440" height="54" rx="27" '
                'fill="#2c2118"/>'
            ),
            text(
                400,
                663,
                "failed validation → no benchmark row",
                18,
                "middle",
                "#ffffff",
                700,
            ),
        ]
    )
    figures["measurement.svg"] = diagram_frame(
        "Benchmark row schema",
        "throughput + checksum + counters + peak RSS",
        body,
    )
    return figures


def nice_axis(maximum: float, intervals: int = 6) -> tuple[float, list[float]]:
    """Return a rounded upper bound and evenly spaced axis ticks."""
    if maximum <= 0.0:
        raise ValueError("chart values must be positive")
    raw_step = maximum / intervals
    magnitude = 10.0 ** math.floor(math.log10(raw_step))
    residual = raw_step / magnitude
    multiplier = next(
        value for value in (1.0, 2.0, 2.5, 5.0, 10.0) if residual <= value
    )
    step = multiplier * magnitude
    upper = math.ceil(maximum / step) * step
    return upper, [step * index for index in range(round(upper / step) + 1)]


def tick_label(value: float) -> str:
    """Format an axis tick without unnecessary decimal zeroes."""
    return f"{value:.0f}" if value.is_integer() else f"{value:.1f}"


def y_axis(
    left: float,
    right: float,
    top: float,
    bottom: float,
    maximum: float,
    ticks: list[float],
    label: str,
) -> list[str]:
    """Draw one labelled linear y-axis with horizontal grid lines."""
    result = []
    for value in ticks:
        y = bottom - value / maximum * (bottom - top)
        result.extend(
            [
                (
                    f'<line x1="{left}" y1="{y:.1f}" x2="{right}" '
                    f'y2="{y:.1f}" stroke="#ded2c2" stroke-width="1"/>'
                ),
                text(left - 12, y + 5, tick_label(value), 12, "end", "#66584c"),
            ]
        )
    result.extend(
        [
            (
                f'<line x1="{left}" y1="{top}" x2="{left}" y2="{bottom}" '
                'stroke="#66584c" stroke-width="1.5"/>'
            ),
            (
                f'<line x1="{left}" y1="{bottom}" x2="{right}" '
                f'y2="{bottom}" stroke="#66584c" stroke-width="1.5"/>'
            ),
            (
                f'<text x="34" y="{(top + bottom) / 2:.1f}" '
                f'transform="rotate(-90 34 {(top + bottom) / 2:.1f})" '
                'text-anchor="middle" font-family="Arial, sans-serif" '
                f'font-size="13" font-weight="700" fill="#4a3c31">'
                f"{html.escape(label)}</text>"
            ),
        ]
    )
    return result


def scaling_svg(rows: list[dict[str, str]]) -> str:
    """Render scaling throughput as bars with axes, CIs, and RSS context."""
    grouped = {row["size"]: row for row in rows}
    if any(size not in grouped for size, _, _ in SCALING_CASES):
        raise ValueError("scaling.csv needs every 1K through 1B row")
    sizes = [item[0] for item in SCALING_CASES]
    ordered = [grouped[size] for size in sizes]
    throughputs = [float(row["throughput_munits_per_s"]) for row in ordered]
    ci_low = [1000.0 / float(row["ci_high_ns_per_unit"]) for row in ordered]
    ci_high = [1000.0 / float(row["ci_low_ns_per_unit"]) for row in ordered]
    maximum, ticks = nice_axis(max(ci_high) * 1.08)
    left, right, top, bottom = 92.0, 1150.0, 138.0, 500.0
    slot = (right - left) / len(sizes)
    x_values = [left + slot * (index + 0.5) for index in range(len(sizes))]
    colours = (
        "#486581",
        "#2a7f9e",
        "#2a9d8f",
        "#78a55a",
        "#d6a33b",
        "#e07a3f",
        "#c75146",
    )
    sample_counts = {row["samples"] for row in ordered}
    sample_note = (
        f"{next(iter(sample_counts))} measured sample"
        f"{'s' if next(iter(sample_counts)) != '1' else ''} per size"
        if len(sample_counts) == 1
        else "mixed sample counts"
    )
    body = [
        (
            '<rect x="58" y="108" width="1110" height="514" rx="16" '
            'fill="#fffaf2" stroke="#ddcfbd"/>'
        ),
        *y_axis(
            left,
            right,
            top,
            bottom,
            maximum,
            ticks,
            "Throughput (million cell-updates/s)",
        ),
    ]
    for x, size, value, low, high, colour in zip(
        x_values, sizes, throughputs, ci_low, ci_high, colours, strict=True
    ):
        y = bottom - value / maximum * (bottom - top)
        bar_height = bottom - y
        body.extend(
            [
                (
                    f'<rect x="{x - 47:.1f}" y="{y:.1f}" width="94" '
                    f'height="{bar_height:.1f}" rx="7" fill="{colour}"/>'
                ),
                text(x, y - 12, f"{value:.3f}", 13, "middle", colour, 700),
                text(x, 528, size, 14, "middle", "#392d24", 700),
            ]
        )
        if high - low > 1e-9:
            low_y = bottom - low / maximum * (bottom - top)
            high_y = bottom - high / maximum * (bottom - top)
            body.extend(
                [
                    (
                        f'<line x1="{x:.1f}" y1="{high_y:.1f}" '
                        f'x2="{x:.1f}" y2="{low_y:.1f}" '
                        'stroke="#2c2118" stroke-width="2"/>'
                    ),
                    (
                        f'<line x1="{x - 8:.1f}" y1="{high_y:.1f}" '
                        f'x2="{x + 8:.1f}" y2="{high_y:.1f}" '
                        'stroke="#2c2118" stroke-width="2"/>'
                    ),
                    (
                        f'<line x1="{x - 8:.1f}" y1="{low_y:.1f}" '
                        f'x2="{x + 8:.1f}" y2="{low_y:.1f}" '
                        'stroke="#2c2118" stroke-width="2"/>'
                    ),
                ]
            )
    body.append(
        text(621, 557, "Grid cells (log₁₀ scale)", 13, "middle", "#4a3c31", 700)
    )
    body.append(text(82, 590, "Peak RSS", 12, "start", "#66584c", 700))
    for x, row in zip(x_values, ordered, strict=True):
        rss_mib = float(row["peak_rss_bytes"]) / (1024.0 * 1024.0)
        rss = (
            f"{rss_mib / 1024.0:.1f} GiB"
            if rss_mib >= 1024.0
            else f"{rss_mib:.1f} MiB"
        )
        body.append(text(x, 590, rss, 11, "middle", "#66584c"))
    return chart_frame(
        "Conway scaling across six orders of magnitude",
        f"L7 · {sample_note} · fixed seed · checksum verified",
        body,
    )


def levels_svg(rows: list[dict[str, str]]) -> str:
    """Render cumulative speedup and adjacent-level changes as bar charts."""
    labels = [f"L{level}" for level in range(8)]
    grouped = {(row["workload"], row["opt_level"]): row for row in rows}
    if any(
        (workload, label) not in grouped
        for workload, _ in LEVEL_CASES
        for label in labels
    ):
        raise ValueError("levels.csv needs every M1 workload at L0 through L7")
    series = []
    for workload, _ in LEVEL_CASES:
        if (
            len({grouped[(workload, label)]["checksum"] for label in labels})
            != 1
        ):
            raise ValueError(f"{workload}: L0 through L7 checksums differ")
        values = [
            float(grouped[(workload, label)]["throughput_munits_per_s"])
            for label in labels
        ]
        if values[0] <= 0.0:
            raise ValueError(f"{workload}: L0 throughput must be positive")
        series.append((workload, [value / values[0] for value in values]))
    workload, values = series[0]
    raw = [
        float(grouped[(workload, label)]["throughput_munits_per_s"])
        for label in labels
    ]
    changes = [
        100.0 * (values[index] / values[index - 1] - 1.0)
        for index in range(1, 8)
    ]
    maximum, ticks = nice_axis(max(values) * 1.08)
    left, right, top, bottom = 104.0, 775.0, 165.0, 500.0
    slot = (right - left) / len(labels)
    x_values = [left + slot * (index + 0.5) for index in range(len(labels))]
    bar_colours = ["#6f7782"] + [
        "#2a9d8f" if value >= previous else "#c75146"
        for previous, value in pairwise(values)
    ]
    bar_colours[-1] = "#d39b2f"
    sample_counts = {grouped[(workload, label)]["samples"] for label in labels}
    sample_note = (
        f"{next(iter(sample_counts))} sample"
        f"{'s' if next(iter(sample_counts)) != '1' else ''} per level"
        if len(sample_counts) == 1
        else "mixed sample counts"
    )
    body = [
        (
            '<rect x="44" y="108" width="755" height="505" rx="16" '
            'fill="#fffaf2" stroke="#ddcfbd"/>'
        ),
        (
            '<rect x="818" y="108" width="342" height="505" rx="16" '
            'fill="#fffaf2" stroke="#ddcfbd"/>'
        ),
        text(421, 140, "Cumulative speedup", 16, "middle", "#392d24", 700),
        text(
            989, 140, "Change from previous level", 16, "middle", "#392d24", 700
        ),
        *y_axis(
            left, right, top, bottom, maximum, ticks, "Speedup versus L0 (×)"
        ),
    ]
    baseline_y = bottom - (bottom - top) / maximum
    body.append(
        f'<line x1="{left}" y1="{baseline_y:.1f}" x2="{right}" '
        f'y2="{baseline_y:.1f}" stroke="#8b7969" stroke-width="2" '
        'stroke-dasharray="7 7"/>'
    )
    for x, label, value, colour in zip(
        x_values, labels, values, bar_colours, strict=True
    ):
        y = bottom - value / maximum * (bottom - top)
        body.extend(
            [
                (
                    f'<rect x="{x - 28:.1f}" y="{y:.1f}" width="56" '
                    f'height="{bottom - y:.1f}" rx="6" fill="{colour}"/>'
                ),
                text(x, y - 10, f"{value:.3f}×", 11, "middle", colour, 700),
                text(x, 526, label, 13, "middle", "#392d24", 700),
            ]
        )
    body.append(
        text(
            440,
            570,
            f"{raw[0]:.3f} → {raw[-1]:.3f} M entity-updates/s",
            13,
            "middle",
            "#66584c",
            700,
        )
    )

    change_limit, _ = nice_axis(max(abs(value) for value in changes), 4)
    change_left, change_right = 884.0, 1138.0
    zero = (change_left + change_right) / 2.0
    half_width = (change_right - change_left) / 2.0
    body.append(
        f'<line x1="{zero:.1f}" y1="166" x2="{zero:.1f}" y2="510" '
        'stroke="#8b7969" stroke-width="1.5"/>'
    )
    for index, (label, change) in enumerate(
        zip(labels[1:], changes, strict=True)
    ):
        y = 183.0 + index * 45.0
        end = zero + change / change_limit * half_width
        colour = "#2a9d8f" if change >= 0.0 else "#c75146"
        body.extend(
            [
                text(852, y + 5, label, 12, "start", "#392d24", 700),
                (
                    f'<rect x="{min(zero, end):.1f}" y="{y - 11:.1f}" '
                    f'width="{max(abs(end - zero), 1.0):.1f}" height="22" '
                    f'rx="4" fill="{colour}"/>'
                ),
                text(
                    end + (7 if change >= 0.0 else -7),
                    y + 5,
                    f"{change:+.1f}%",
                    11,
                    "start" if change >= 0.0 else "end",
                    colour,
                    700,
                ),
            ]
        )
    body.extend(
        [
            (
                f'<line x1="{change_left}" y1="520" x2="{change_right}" '
                'y2="520" stroke="#66584c" stroke-width="1.5"/>'
            ),
            text(
                change_left,
                541,
                f"−{change_limit:.0f}%",
                11,
                "middle",
                "#66584c",
            ),
            text(zero, 541, "0%", 11, "middle", "#66584c"),
            text(
                change_right,
                541,
                f"+{change_limit:.0f}%",
                11,
                "middle",
                "#66584c",
            ),
            text(
                1011,
                570,
                "Step change in throughput",
                12,
                "middle",
                "#66584c",
                700,
            ),
            '<rect x="1053" y="17" width="112" height="51" rx="10" fill="#d39b2f"/>',
            text(1109, 49, f"{values[-1]:.3f}×", 20, "middle", "#2c2118", 700),
            text(
                600,
                647,
                f"All levels matched checksum {grouped[(workload, 'L0')]['checksum']}",
                13,
                "middle",
                "#4a3c31",
                700,
            ),
        ]
    )
    return chart_frame(
        "Serial optimisation ladder",
        f"{workload} · eight isolated release builds · {sample_note}",
        body,
    )


def pages_svg(rows: list[dict[str, str]]) -> str:
    """Render the verified page comparison as labelled horizontal bars."""
    grouped = {row["page_policy"]: row for row in rows}
    if "base" not in grouped or "huge" not in grouped:
        raise ValueError("pages.csv needs base and huge rows")
    if grouped["base"]["checksum"] != grouped["huge"]["checksum"]:
        raise ValueError("base and huge pages produced different checksums")
    if any(
        grouped[name]["page_backing_verified"] != "true"
        for name in ("base", "huge")
    ):
        raise ValueError("page graph requires verified backing")
    values = [
        float(grouped[name]["throughput_munits_per_s"])
        for name in ("base", "huge")
    ]
    maximum, ticks = nice_axis(max(values) * 1.08)
    left, right = 260.0, 1110.0
    axis_y = 475.0
    body = [
        (
            '<rect x="58" y="112" width="1110" height="500" rx="16" '
            'fill="#fffaf2" stroke="#ddcfbd"/>'
        ),
        text(
            613,
            148,
            "Throughput by verified page backing",
            16,
            "middle",
            "#392d24",
            700,
        ),
    ]
    for tick in ticks:
        x = left + tick / maximum * (right - left)
        body.extend(
            [
                (
                    f'<line x1="{x:.1f}" y1="178" x2="{x:.1f}" '
                    f'y2="{axis_y}" stroke="#ded2c2" stroke-width="1"/>'
                ),
                text(x, axis_y + 22, tick_label(tick), 12, "middle", "#66584c"),
            ]
        )
    body.append(
        f'<line x1="{left}" y1="{axis_y}" x2="{right}" y2="{axis_y}" '
        'stroke="#66584c" stroke-width="1.5"/>'
    )
    for y, name, value, colour in zip(
        (245.0, 355.0),
        ("base", "huge"),
        values,
        ("#486581", "#8b5fa8"),
        strict=True,
    ):
        end = left + value / maximum * (right - left)
        body.extend(
            [
                text(
                    left - 24,
                    y + 6,
                    "4 KiB base" if name == "base" else "2 MiB huge",
                    14,
                    "end",
                    "#392d24",
                    700,
                ),
                (
                    f'<rect x="{left}" y="{y - 22:.1f}" '
                    f'width="{end - left:.1f}" height="44" rx="8" '
                    f'fill="{colour}"/>'
                ),
                text(end + 12, y + 6, f"{value:.3f}", 14, "start", colour, 700),
            ]
        )
    change = 100.0 * (values[1] / values[0] - 1.0)
    body.extend(
        [
            text(
                685,
                532,
                "Throughput (million cell-updates/s)",
                13,
                "middle",
                "#4a3c31",
                700,
            ),
            text(
                613,
                575,
                f"Huge versus base: {change:+.2f}%",
                16,
                "middle",
                "#8b5fa8" if change >= 0.0 else "#c75146",
                700,
            ),
            text(
                600,
                647,
                "Backing verified from /proc/self/smaps; memory advice alone is not evidence.",
                12,
                "middle",
                "#66584c",
            ),
        ]
    )
    return chart_frame(
        "Page-size effect",
        "10M-cell Conway · identical binary and checksum",
        body,
    )


def levels_slide_svg(rows: list[dict[str, str]]) -> str:
    """Render the optimisation ladder for the deck's square image slot."""
    labels = [f"L{level}" for level in range(8)]
    workload = LEVEL_CASES[0][0]
    grouped = {(row["workload"], row["opt_level"]): row for row in rows}
    if any((workload, label) not in grouped for label in labels):
        raise ValueError("levels.csv needs L0 through L7")
    selected = [grouped[(workload, label)] for label in labels]
    if len({row["checksum"] for row in selected}) != 1:
        raise ValueError(f"{workload}: L0 through L7 checksums differ")
    raw = [float(row["throughput_munits_per_s"]) for row in selected]
    if raw[0] <= 0.0:
        raise ValueError(f"{workload}: L0 throughput must be positive")
    values = [value / raw[0] for value in raw]
    maximum, ticks = nice_axis(max(max(values), 1.0) * 1.08, 4)
    top, bottom = 155.0, 600.0
    body = []
    for tick in ticks:
        y = bottom - tick / maximum * (bottom - top)
        body.extend(
            [
                (
                    f'<line x1="66" y1="{y:.1f}" x2="748" y2="{y:.1f}" '
                    'stroke="#ded2c2" stroke-width="2"/>'
                ),
                text(57, y + 5, f"{tick_label(tick)}×", 13, "end", "#66584c"),
            ]
        )
    baseline_y = bottom - (bottom - top) / maximum
    body.append(
        f'<line x1="66" y1="{baseline_y:.1f}" x2="748" '
        f'y2="{baseline_y:.1f}" stroke="#8b7969" stroke-width="3" '
        'stroke-dasharray="8 7"/>'
    )
    for index, (label, value) in enumerate(zip(labels, values, strict=True)):
        x = 80 + index * 84
        y = bottom - value / maximum * (bottom - top)
        colour = (
            "#d39b2f"
            if label == "L7"
            else (
                "#2a9d8f"
                if index == 0 or value >= values[index - 1]
                else "#c75146"
            )
        )
        body.extend(
            [
                (
                    f'<rect x="{x}" y="{y:.1f}" width="54" '
                    f'height="{bottom - y:.1f}" rx="7" fill="{colour}"/>'
                ),
                text(
                    x + 27, y - 11, f"{value:.2f}×", 13, "middle", colour, 700
                ),
                text(x + 27, 629, label, 16, "middle", "#392d24", 700),
            ]
        )
    body.extend(
        [
            (
                '<rect x="163" y="662" width="474" height="48" rx="24" '
                'fill="#2c2118"/>'
            ),
            text(
                400,
                693,
                f"{raw[0]:.3f} → {raw[-1]:.3f} M updates/s",
                18,
                "middle",
                "#ffffff",
                700,
            ),
        ]
    )
    return diagram_frame(
        "Measured optimisation ladder",
        f"{workload} · fixed seed · checksum matched",
        body,
    )


def pages_slide_svg(rows: list[dict[str, str]]) -> str:
    """Render the verified page experiment for a square slide image slot."""
    grouped = {row["page_policy"]: row for row in rows}
    if "base" not in grouped or "huge" not in grouped:
        raise ValueError("pages.csv needs base and huge rows")
    if grouped["base"]["checksum"] != grouped["huge"]["checksum"]:
        raise ValueError("base and huge pages produced different checksums")
    if any(
        grouped[policy]["page_backing_verified"] != "true"
        for policy in ("base", "huge")
    ):
        raise ValueError("page figure requires verified backing")
    base = float(grouped["base"]["throughput_munits_per_s"])
    huge = float(grouped["huge"]["throughput_munits_per_s"])
    body = []
    for x, policy, page, value, colour in (
        (67, "BASE", "4 KiB", base, "#486581"),
        (433, "HUGE", "2 MiB", huge, "#8b5fa8"),
    ):
        body.extend(
            [
                (
                    f'<rect x="{x}" y="145" width="300" height="388" rx="22" '
                    f'fill="#f8f3eb" stroke="{colour}" stroke-width="5"/>'
                ),
                text(x + 150, 188, policy, 22, "middle", colour, 700),
                text(x + 150, 225, page + " backing", 17),
            ]
        )
        if policy == "BASE":
            for row in range(4):
                for column in range(5):
                    px = x + 51 + column * 42
                    py = 258 + row * 42
                    body.append(
                        f'<rect x="{px}" y="{py}" width="32" height="32" '
                        f'rx="4" fill="{colour}" opacity="0.82"/>'
                    )
        else:
            body.extend(
                [
                    (
                        f'<rect x="{x + 51}" y="258" width="92" height="158" '
                        f'rx="10" fill="{colour}" opacity="0.82"/>'
                    ),
                    (
                        f'<rect x="{x + 157}" y="258" width="92" height="158" '
                        f'rx="10" fill="{colour}" opacity="0.82"/>'
                    ),
                ]
            )
        body.extend(
            [
                text(x + 150, 469, f"{value:.3f}", 32, "middle", colour, 700),
                text(x + 150, 499, "M cell-updates/s", 15),
            ]
        )
    change = 100.0 * (huge / base - 1.0)
    body.extend(
        [
            (
                '<rect x="142" y="573" width="516" height="87" rx="20" '
                'fill="#2c2118"/>'
            ),
            text(400, 609, "BACKING VERIFIED", 18, "middle", "#ffffff", 700),
            text(
                400,
                641,
                f"huge versus base: {change:+.2f}%",
                19,
                "middle",
                "#d39b2f",
                700,
            ),
            text(
                400,
                700,
                "same binary · same checksum",
                17,
                "middle",
                "#392d24",
                700,
            ),
        ]
    )
    return diagram_frame(
        "Verified page backing", "10M-cell Conway · /proc/self/smaps", body
    )


def scaling_slide_svg(rows: list[dict[str, str]]) -> str:
    """Render Conway scaling for the deck's square image slot."""
    grouped = {row["size"]: row for row in rows}
    if any(size not in grouped for size, _, _ in SCALING_CASES):
        raise ValueError("scaling.csv needs every 1K through 1B row")
    labels = [size for size, _, _ in SCALING_CASES]
    selected = [grouped[label] for label in labels]
    values = [float(row["throughput_munits_per_s"]) for row in selected]
    maximum, ticks = nice_axis(max(values) * 1.08, 4)
    left, right, top, bottom = 78.0, 742.0, 160.0, 563.0
    step = (right - left) / (len(labels) - 1)
    points = [
        (
            left + index * step,
            bottom - value / maximum * (bottom - top),
        )
        for index, value in enumerate(values)
    ]
    body = []
    for tick in ticks:
        y = bottom - tick / maximum * (bottom - top)
        body.extend(
            [
                (
                    f'<line x1="{left}" y1="{y:.1f}" x2="{right}" '
                    f'y2="{y:.1f}" stroke="#ded2c2" stroke-width="2"/>'
                ),
                text(left - 10, y + 5, tick_label(tick), 13, "end", "#66584c"),
            ]
        )
    coordinates = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    body.append(
        f'<polyline points="{coordinates}" fill="none" stroke="#2c2118" '
        'stroke-width="6" stroke-linejoin="round"/>'
    )
    for index, ((x, y), label, value) in enumerate(
        zip(points, labels, values, strict=True)
    ):
        colour = "#c75146" if index >= 5 else "#2a9d8f"
        body.extend(
            [
                (
                    f'<circle cx="{x:.1f}" cy="{y:.1f}" r="12" '
                    f'fill="{colour}" stroke="#fffaf2" stroke-width="4"/>'
                ),
                text(x, y - 19, f"{value:.2f}", 13, "middle", colour, 700),
                text(x, 592, label, 14, "middle", "#392d24", 700),
            ]
        )
    rss_mib = float(selected[-1]["peak_rss_bytes"]) / (1024.0 * 1024.0)
    rss = (
        f"{rss_mib / 1024.0:.1f} GiB"
        if rss_mib >= 1024.0
        else f"{rss_mib:.1f} MiB"
    )
    body.extend(
        [
            (
                '<rect x="105" y="632" width="590" height="72" rx="18" '
                'fill="#2c2118"/>'
            ),
            text(
                400,
                662,
                f"10M: {values[4]:.3f} M cell-updates/s",
                18,
                "middle",
                "#ffffff",
                700,
            ),
            text(
                400,
                688,
                f"1B: {values[-1]:.3f} M/s · peak RSS {rss}",
                17,
                "middle",
                "#d39b2f",
                700,
            ),
        ]
    )
    return diagram_frame(
        "Conway scaling", "L7 · 1K to 1B cells · checksum verified", body
    )


def provenance_svg(row: dict[str, str]) -> str:
    """Render recorded benchmark context for the setup slide."""
    captured = row.get("captured_utc", "unavailable")
    if "T" in captured:
        captured = captured[:19].replace("T", " ") + " UTC"
    commit = row.get("git_commit", "unavailable")[:10]
    compiler = row.get("compiler", "unavailable")
    if len(compiler) > 45:
        compiler = compiler[:42] + "…"
    entries = (
        ("JOB", row.get("slurm_job_id", "unavailable")),
        ("NODE", row.get("slurm_node_list", "unavailable")),
        ("COMPILER", compiler),
        ("COMMIT", commit),
        (
            "SAMPLING",
            (
                f"{row.get('samples', '?')} × "
                f"{row.get('minimum_case_ms', '?')} ms minimum"
            ),
        ),
        ("CAPTURED", captured),
    )
    body = []
    for index, (label, value) in enumerate(entries):
        y = 132 + index * 85
        body.extend(
            [
                (
                    f'<rect x="64" y="{y}" width="672" height="67" rx="14" '
                    'fill="#f3eadf" stroke="#ddcfbd" stroke-width="2"/>'
                ),
                text(88, y + 28, label, 14, "start", "#c75146", 700),
                text(88, y + 53, value, 17, "start", "#392d24", 700),
            ]
        )
    state = "DIRTY TREE" if row.get("git_dirty") == "true" else "CLEAN TREE"
    colour = "#c75146" if state == "DIRTY TREE" else "#2a9d8f"
    body.extend(
        [
            (
                '<rect x="267" y="655" width="266" height="54" rx="27" '
                f'fill="{colour}"/>'
            ),
            text(400, 690, state, 18, "middle", "#ffffff", 700),
        ]
    )
    return diagram_frame(
        "Recorded run context", "generated from provenance.csv", body
    )


def page_backing_verified(rows: list[dict[str, str]] | None) -> bool:
    """Return whether every measured page policy has verified backing."""
    return bool(rows) and all(
        row["page_backing_verified"] == "true" for row in rows
    )


def page_experiment(
    samples: int, minimum_ms: int, build: Path | None = None
) -> list[dict[str, str]]:
    """Compare Linux base and huge page runs and record backing checks"""
    # Linux smaps shows whether base or huge pages actually backed the run
    if sys.platform != "linux":
        raise ValueError("page experiment requires Linux (/proc/self/smaps)")
    OUT.mkdir(parents=True, exist_ok=True)
    build = build or configure()
    rows = []
    for policy in ("base", "huge"):
        print(f"benchmark {PAGE_CASE} with {policy} pages", flush=True)
        environment = {**os.environ, "M1_PAGE_POLICY": policy}
        row = benchmark(
            build,
            PAGE_CASE,
            samples,
            minimum_ms,
            env=environment,
        )
        if row["page_policy"] != policy:
            raise ValueError(f"{policy}: benchmark reported wrong page policy")
        rows.append(row)
    write_csv(OUT / "pages.csv", BENCH_FIELDS, rows)
    unverified = [
        row["page_policy"]
        for row in rows
        if row["page_backing_verified"] != "true"
    ]
    if len({row["checksum"] for row in rows}) != 1:
        raise ValueError("base and huge pages produced different checksums")
    if unverified:
        print(
            f"report: page backing unverified for {', '.join(unverified)}; "
            "comparison marked UNVERIFIED",
            file=sys.stderr,
        )
        return rows
    (OUT / "pages.svg").write_text(pages_svg(rows), encoding="utf-8")
    return rows


def summary(
    scaling: list[dict[str, str]],
    levels: list[dict[str, str]] | None = None,
    pages: list[dict[str, str]] | None = None,
) -> str:
    """Format measured rows as compact slide-ready tables"""
    lines = [
        "# Benchmark report",
        "",
        "| Cells | M cell-updates/s | Median ns/cell | 95% CI | RSS MiB |",
        "| ---: | ---: | ---: | ---: | ---: |",
    ]
    grouped = {row["size"]: row for row in scaling}
    for size, _, _ in SCALING_CASES:
        row = grouped[size]
        low = float(row["ci_low_ns_per_unit"])
        high = float(row["ci_high_ns_per_unit"])
        rss = float(row["peak_rss_bytes"]) / (1024.0 * 1024.0)
        lines.append(
            f"| {size} | {float(row['throughput_munits_per_s']):.3f} | "
            f"{float(row['median_ns_per_unit']):.3f} | "
            f"{low:.3f}-{high:.3f} | {rss:.1f} |"
        )
    if levels:
        labels = [f"L{level}" for level in range(8)]
        grouped_levels = {
            (row["workload"], row["opt_level"]): row for row in levels
        }
        lines.extend(
            [
                "",
                "## Optimisation levels",
                "",
                "| Level | "
                + " | ".join(workload for workload, _ in LEVEL_CASES)
                + " |",
                "| ---: | " + " | ".join("---:" for _ in LEVEL_CASES) + " |",
            ]
        )
        for label in labels:
            speedups = []
            for workload, _ in LEVEL_CASES:
                value = float(
                    grouped_levels[(workload, label)]["throughput_munits_per_s"]
                )
                baseline = float(
                    grouped_levels[(workload, "L0")]["throughput_munits_per_s"]
                )
                speedups.append(f"{value / baseline:.3f}x")
            lines.append(f"| {label} | " + " | ".join(speedups) + " |")
    if pages:
        grouped_pages = {row["page_policy"]: row for row in pages}
        base = float(grouped_pages["base"]["throughput_munits_per_s"])
        huge = float(grouped_pages["huge"]["throughput_munits_per_s"])
        lines.extend(
            [
                "",
                "## Page policy",
                "",
                "| Policy | M cell-updates/s | RSS MiB | Backing verified |",
                "| --- | ---: | ---: | :---: |",
            ]
        )
        for policy in ("base", "huge"):
            row = grouped_pages[policy]
            rss = float(row["peak_rss_bytes"]) / (1024.0 * 1024.0)
            lines.append(
                f"| {policy} | {float(row['throughput_munits_per_s']):.3f} | "
                f"{rss:.1f} | {row['page_backing_verified']} |"
            )
        if page_backing_verified(pages):
            lines.extend(["", f"Huge/base throughput: **{huge / base:.3f}x**"])
        else:
            lines.extend(
                [
                    "",
                    (
                        "Page-backing comparison: **UNVERIFIED** "
                        "(`/proc/self/smaps` did not confirm both policies)."
                    ),
                ]
            )
    lines.append("")
    return "\n".join(lines)


def read_rows(target: Path) -> list[dict[str, str]]:
    with target.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def write_slide_figures(
    directory: Path,
    scaling: list[dict[str, str]],
    levels: list[dict[str, str]] | None,
    pages: list[dict[str, str]] | None,
) -> None:
    """Write one square figure for every technical image slot in the deck."""
    figures = static_slide_figures()
    figures["scaling-slide.svg"] = scaling_slide_svg(scaling)
    if levels:
        figures["levels-slide.svg"] = levels_slide_svg(levels)
    else:
        (directory / "levels-slide.svg").unlink(missing_ok=True)
    if pages and page_backing_verified(pages):
        figures["pages-slide.svg"] = pages_slide_svg(pages)
    else:
        (directory / "pages-slide.svg").unlink(missing_ok=True)
    provenance = directory / "provenance.csv"
    if provenance.exists():
        rows = read_rows(provenance)
        if len(rows) != 1:
            raise ValueError("provenance.csv needs exactly one row")
        figures["provenance.svg"] = provenance_svg(rows[0])
    else:
        (directory / "provenance.svg").unlink(missing_ok=True)
    for name, content in figures.items():
        (directory / name).write_text(content, encoding="utf-8")


def collect_case(index: int, samples: int, minimum_ms: int) -> None:
    """Measure one indexed Conway case for a multi-job Slurm run."""
    if index < 0 or index >= len(SCALING_CASES):
        raise ValueError("case index is out of range")
    OUT.mkdir(parents=True, exist_ok=True)
    row = scaling_row(
        ROOT / "build/evidence", SCALING_CASES[index], samples, minimum_ms
    )
    write_csv(OUT / f"case-{index}.csv", SCALING_FIELDS, [row])


def merge_cases(directory: Path = OUT) -> list[dict[str, str]]:
    """Merge one checked row from every Conway case job."""
    rows = []
    for index, (label, cells, case) in enumerate(SCALING_CASES):
        partial = read_rows(directory / f"case-{index}.csv")
        if len(partial) != 1:
            raise ValueError(f"case-{index}.csv needs exactly one row")
        row = partial[0]
        if (
            row["size"] != label
            or row["cells"] != str(cells)
            or row["case"] != case
        ):
            raise ValueError(f"case-{index}.csv does not match {case}")
        rows.append(row)
    write_csv(directory / "scaling.csv", SCALING_FIELDS, rows)
    return rows


def merge_levels(directory: Path = OUT) -> list[dict[str, str]]:
    """Merge the two checked Slurm level ranges."""
    rows = []
    for start, end in ((0, 3), (4, 7)):
        partial = read_rows(directory / f"levels-{start}-{end}.csv")
        expected = {
            (f"L{level}", workload)
            for level in range(start, end + 1)
            for workload, _ in LEVEL_CASES
        }
        actual = {(row["opt_level"], row["workload"]) for row in partial}
        if len(partial) != len(expected) or actual != expected:
            raise ValueError(f"levels-{start}-{end}.csv has invalid rows")
        rows.extend(partial)
    figure = levels_svg(rows)
    write_csv(directory / "levels.csv", LEVEL_FIELDS, rows)
    (directory / "levels.svg").write_text(figure, encoding="utf-8")
    return rows


def graph(
    scaling: list[dict[str, str]] | None = None,
    levels: list[dict[str, str]] | None = None,
    pages: list[dict[str, str]] | None = None,
) -> None:
    """Write report files from supplied or persisted scaling rows"""
    # Rebuild report files from CSV without repeating the timed benchmark runs
    scaling = scaling or read_rows(OUT / "scaling.csv")
    if levels is None and (OUT / "levels.csv").exists():
        levels = read_rows(OUT / "levels.csv")
    if pages is None and (OUT / "pages.csv").exists():
        pages = read_rows(OUT / "pages.csv")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "scaling.svg").write_text(scaling_svg(scaling), encoding="utf-8")
    if levels:
        (OUT / "levels.svg").write_text(levels_svg(levels), encoding="utf-8")
    if pages and page_backing_verified(pages):
        (OUT / "pages.svg").write_text(pages_svg(pages), encoding="utf-8")
    elif pages:
        (OUT / "pages.svg").unlink(missing_ok=True)
    (OUT / "summary.md").write_text(
        summary(scaling, levels, pages), encoding="utf-8"
    )
    write_slide_figures(OUT, scaling, levels, pages)


def command_line(command: list[str]) -> str:
    """Return one identifying line without making provenance a run blocker"""
    result = subprocess.run(
        command, cwd=ROOT, text=True, capture_output=True, check=False
    )
    output = result.stdout.strip() or result.stderr.strip()
    return output.splitlines()[0] if output else "unavailable"


def write_provenance(samples: int, minimum_ms: int) -> None:
    """Record enough context to identify and reproduce a cluster sweep"""
    OUT.mkdir(parents=True, exist_ok=True)
    fields = (
        "captured_utc",
        "slurm_job_id",
        "slurm_job_name",
        "slurm_node_list",
        "host",
        "platform",
        "git_commit",
        "git_dirty",
        "compiler",
        "cmake",
        "python",
        "samples",
        "minimum_case_ms",
    )
    git_status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    row = {
        "captured_utc": datetime.now(UTC).isoformat(),
        "slurm_job_id": os.environ.get("SLURM_JOB_ID", "local"),
        "slurm_job_name": os.environ.get("SLURM_JOB_NAME", "local"),
        "slurm_node_list": os.environ.get("SLURM_JOB_NODELIST", "local"),
        "host": platform.node(),
        "platform": platform.platform(),
        "git_commit": command_line(["git", "rev-parse", "HEAD"]),
        "git_dirty": str(bool(git_status.stdout.strip())).lower(),
        "compiler": command_line([os.environ.get("CXX", "c++"), "--version"]),
        "cmake": command_line(["cmake", "--version"]),
        "python": platform.python_version(),
        "samples": str(samples),
        "minimum_case_ms": str(minimum_ms),
    }
    write_csv(OUT / "provenance.csv", fields, [row])


def self_check() -> None:
    """Validate report rendering with synthetic checked rows and XML parsing"""
    fake = {field: "1" for field in BENCH_FIELDS}
    fake.update(
        {
            "case": PRIMARY_CASE,
            "unit": "cell_updates",
            "checksum": "abc",
            "verified": "true",
            "page_backing_verified": "false",
        }
    )
    scaling = [
        {
            "size": size,
            "cells": str(cells),
            "index_width_bits": "32",
            **fake,
            "case": case,
            "throughput_munits_per_s": str(index),
        }
        for index, (size, cells, case) in enumerate(SCALING_CASES, 1)
    ]
    levels = [
        {
            "opt_level": f"L{level}",
            "workload": workload,
            **fake,
            "case": case,
            "checksum": f"checksum-{workload}",
            "throughput_munits_per_s": str((level + 1) * workload_index),
        }
        for level in range(8)
        for workload_index, (workload, case) in enumerate(LEVEL_CASES, 1)
    ]
    pages = [
        {
            **fake,
            "page_policy": policy,
            "page_backing_verified": "true",
            "throughput_munits_per_s": str(index),
        }
        for index, policy in enumerate(("base", "huge"), 1)
    ]
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory)
        for index, row in enumerate(scaling):
            write_csv(output / f"case-{index}.csv", SCALING_FIELDS, [row])
        if merge_cases(output) != scaling:
            raise AssertionError("case merge changed scaling rows")
        split = 4 * len(LEVEL_CASES)
        write_csv(output / "levels-0-3.csv", LEVEL_FIELDS, levels[:split])
        write_csv(output / "levels-4-7.csv", LEVEL_FIELDS, levels[split:])
        if merge_levels(output) != levels:
            raise AssertionError("level merge changed level rows")
        for name, content in (
            ("scaling.svg", scaling_svg(scaling)),
            ("pages.svg", pages_svg(pages)),
        ):
            target = output / name
            target.write_text(content, encoding="utf-8")
            ET.parse(target)
        ET.parse(output / "levels.svg")
        provenance = {
            "captured_utc": "2026-09-03T04:25:53+00:00",
            "slurm_job_id": "581897",
            "slurm_node_list": "a100-0",
            "git_commit": "0123456789abcdef",
            "git_dirty": "false",
            "compiler": "g++ (GCC) 13.3.1",
            "samples": "1",
            "minimum_case_ms": "100",
        }
        write_csv(output / "provenance.csv", tuple(provenance), [provenance])
        write_slide_figures(output, scaling, levels, pages)
        names = {
            *static_slide_figures(),
            "levels-slide.svg",
            "pages-slide.svg",
            "scaling-slide.svg",
            "provenance.svg",
        }
        for name in names:
            ET.parse(output / name)
    report = summary(scaling, levels, pages)
    if "M cell-updates/s" not in report or "Huge/base throughput" not in report:
        raise AssertionError("summary is incomplete")
    unverified_pages = [
        {**row, "page_backing_verified": "false"} for row in pages
    ]
    unverified_report = summary(scaling, levels, unverified_pages)
    if (
        "Page-backing comparison: **UNVERIFIED**" not in unverified_report
        or "Huge/base throughput" in unverified_report
    ):
        raise AssertionError("unverified page backing was reported as measured")
    print("report self-check: PASS")


def main() -> int:
    """Dispatch collection, graphing, page checks, or self-check by mode"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        nargs="?",
        choices=(
            "all",
            "case",
            "cluster",
            "graph",
            "levels",
            "merge",
            "page",
            "self-check",
        ),
        default="all",
    )
    parser.add_argument("--case-index", type=int)
    parser.add_argument("--level-start", type=int, choices=range(8), default=0)
    parser.add_argument("--level-end", type=int, choices=range(8), default=7)
    parser.add_argument(
        "--samples",
        type=positive,
        default=positive(os.environ.get("SAMPLES", "5")),
    )
    parser.add_argument(
        "--minimum-case-ms",
        type=positive,
        default=positive(os.environ.get("MINIMUM_CASE_MS", "100")),
    )
    options = parser.parse_args()
    if options.mode == "self-check":
        self_check()
        return 0
    if options.mode in {"case", "merge"} and (
        sys.platform != "linux" or "SLURM_JOB_ID" not in os.environ
    ):
        raise ValueError(f"{options.mode} must run inside a Linux Slurm job")
    if options.mode == "case":
        if options.case_index is None:
            raise ValueError("case requires --case-index")
        collect_case(
            options.case_index, options.samples, options.minimum_case_ms
        )
        print(f"report: {OUT.relative_to(ROOT)}/case-{options.case_index}.csv")
        return 0
    if options.mode == "merge":
        write_provenance(options.samples, options.minimum_case_ms)
        if (OUT / "levels-0-3.csv").exists() or (
            OUT / "levels-4-7.csv"
        ).exists():
            merge_levels()
        graph(merge_cases())
        print(f"report: {OUT.relative_to(ROOT)}")
        return 0
    if options.mode == "levels":
        write_provenance(options.samples, options.minimum_case_ms)
        collect_levels(
            options.samples,
            options.minimum_case_ms,
            options.level_start,
            options.level_end,
        )
        print(f"report: {OUT.relative_to(ROOT)}")
        return 0
    if options.mode == "page":
        write_provenance(options.samples, options.minimum_case_ms)
        page_experiment(options.samples, options.minimum_case_ms)
        print(f"report: {OUT.relative_to(ROOT)}")
        return 0
    if options.mode == "cluster":
        if sys.platform != "linux" or "SLURM_JOB_ID" not in os.environ:
            raise ValueError("cluster sweep must run inside a Linux Slurm job")
        write_provenance(options.samples, options.minimum_case_ms)
        levels, build = collect_levels(options.samples, options.minimum_case_ms)
        scaling = collect(options.samples, options.minimum_case_ms)
        pages = page_experiment(options.samples, options.minimum_case_ms, build)
        graph(scaling, levels, pages)
        print(f"report: {OUT.relative_to(ROOT)}")
        return 0
    if options.mode == "graph":
        graph()
    else:
        write_provenance(options.samples, options.minimum_case_ms)
        graph(collect(options.samples, options.minimum_case_ms))
    print(f"report: {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"report: {error}", file=sys.stderr)
        raise SystemExit(2) from error
