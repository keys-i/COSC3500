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
