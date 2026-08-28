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
import os
import platform
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from datetime import UTC, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
configured_out = Path(os.environ.get("M1_BENCH_OUT", "results/bench"))
OUT = (
    configured_out if configured_out.is_absolute() else ROOT / configured_out
).resolve()
PRIMARY_CASE = "cellular/conway/1m"
PAGE_CASE = "cellular/conway/10m"
LEVEL_CASES = (
    ("Conway 1M", PRIMARY_CASE),
    ("PDE heat", "pde/heat"),
    ("Chess", "turn/chess"),
    ("Carrom", "timeline/carrom"),
)
SCALING_CASES = (
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


def collect(samples: int, minimum_ms: int) -> list[dict[str, str]]:
    """Build, test, time the fixed scaling cases, and persist scaling.csv"""
    if "SLURM_JOB_ID" not in os.environ:
        raise ValueError("100K through 1B scaling must run inside Slurm")
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
    scaling = []
    for label, cells, case in SCALING_CASES:
        print(f"benchmark {case}", flush=True)
        scaling.append(
            {
                "size": label,
                "cells": str(cells),
                "index_width_bits": "32",
                **benchmark(build, case, samples, minimum_ms),
            }
        )
    scaling_fields = ("size", "cells", "index_width_bits", *BENCH_FIELDS)
    write_csv(OUT / "scaling.csv", scaling_fields, scaling)
    return scaling


def collect_levels(
    samples: int, minimum_ms: int
) -> tuple[list[dict[str, str]], Path]:
    """Build every source level and time the registered M1 workload suite"""
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    final_build = ROOT / "build/evidence-l7"
    for level in range(8):
        build = configure(level)
        for workload, case in LEVEL_CASES:
            print(f"benchmark L{level} {case}", flush=True)
            row = benchmark(build, case, samples, minimum_ms)
            rows.append({"opt_level": f"L{level}", "workload": workload, **row})
        final_build = build
    for workload, _ in LEVEL_CASES:
        checksums = {
            row["checksum"] for row in rows if row["workload"] == workload
        }
        if len(checksums) != 1:
            raise ValueError(f"{workload}: L0 through L7 checksums differ")
    write_csv(
        OUT / "levels.csv", ("opt_level", "workload", *BENCH_FIELDS), rows
    )
    (OUT / "levels.svg").write_text(levels_svg(rows), encoding="utf-8")
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
    height: int = 600,
) -> str:
    """Wrap SVG chart content in the shared canvas and heading treatment"""
    return "".join(
        [
            (
                f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
                f'height="{height}" viewBox="0 0 {width} {height}">'
            ),
            '<rect width="100%" height="100%" fill="#f4f7fb"/>',
            '<rect width="100%" height="78" fill="#111c31"/>',
            text(34, 35, title, 25, "start", "#ffffff", 700),
            text(34, 62, subtitle, 14, "start", "#c7d6eb"),
            *body,
            "</svg>",
        ]
    )


def line(points: list[tuple[int, float]], colour: str) -> str:
    coordinates = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    circles = "".join(
        f'<circle cx="{x:.1f}" cy="{y:.1f}" r="6" fill="{colour}"/>'
        for x, y in points
    )
    return (
        f'<polyline points="{coordinates}" fill="none" stroke="{colour}" '
        f'stroke-width="4"/>{circles}'
    )


def scaling_svg(rows: list[dict[str, str]]) -> str:
    """Turn fixed scaling rows into a self-contained SVG throughput chart"""
    # Charts use measured throughput while fixed cases share one scale
    grouped = {row["size"]: row for row in rows}
    if any(size not in grouped for size, _, _ in SCALING_CASES):
        raise ValueError("scaling.csv needs every 100K through 1B row")
    sizes = [item[0] for item in SCALING_CASES]
    throughputs = [
        float(grouped[size]["throughput_munits_per_s"]) for size in sizes
    ]
    throughput_max = max(throughputs) * 1.15
    top, bottom = 155, 475
    x_values = [120 + index * 240 for index in range(len(SCALING_CASES))]
    body = [
        text(
            600,
            112,
            "Throughput by problem size",
            18,
            "middle",
            "#24364b",
            700,
        ),
        (
            f'<line x1="110" y1="{bottom}" x2="1090" '
            f'y2="{bottom}" stroke="#52657a"/>'
        ),
    ]
    points = []
    for x, size, value in zip(x_values, sizes, throughputs, strict=True):
        y = bottom - value / throughput_max * (bottom - top)
        points.append((x, y))
        body.append(
            text(x, y - 12, f"{value:.2f}", 12, "middle", "#155eef", 700)
        )
    body.append(line(points, "#155eef"))
    for x, size in zip(x_values, sizes, strict=True):
        body.append(text(x, 510, size, 14, "middle", "#24364b", 700))
    body.append(
        text(
            600,
            570,
            "Measured throughput; no extrapolated points.",
            13,
            "middle",
            "#52657a",
        )
    )
    return chart_frame(
        "Conway scaling", "L7 build, one shared C++ engine", body
    )


def levels_svg(rows: list[dict[str, str]]) -> str:
    """Render each M1 workload's measured speedup across L0..L7"""
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
    maximum = max(value for _, values in series for value in values) * 1.15
    top, bottom = 155, 475
    x_values = [135 + index * 135 for index in range(8)]
    colours = ("#155eef", "#e5484d", "#24a148", "#8e4ec6")
    body = [
        f'<line x1="100" y1="{bottom}" x2="1110" y2="{bottom}" stroke="#52657a"/>',
    ]
    baseline_y = bottom - (bottom - top) / maximum
    body.append(
        f'<line x1="100" y1="{baseline_y:.1f}" x2="1110" '
        f'y2="{baseline_y:.1f}" stroke="#9aa9b8" stroke-dasharray="7 7"/>'
    )
    body.append(text(94, baseline_y + 5, "1x", 12, "end", "#52657a"))
    for index, ((workload, values), colour) in enumerate(
        zip(series, colours, strict=True)
    ):
        points = [
            (x, bottom - value / maximum * (bottom - top))
            for x, value in zip(x_values, values, strict=True)
        ]
        body.append(line(points, colour))
        legend_x = 135 + index * 260
        body.append(
            f'<line x1="{legend_x}" y1="112" x2="{legend_x + 28}" '
            f'y2="112" stroke="{colour}" stroke-width="4"/>'
        )
        body.append(
            text(
                legend_x + 36,
                117,
                f"{workload} · L7 {values[-1]:.2f}x",
                12,
                "start",
                colour,
                700,
            )
        )
    for x, label in zip(x_values, labels, strict=True):
        body.append(text(x, 510, label, 13, "middle", "#24364b", 700))
    body.append(
        text(
            600,
            570,
            "Speedup vs each workload's L0; checksums must match.",
            13,
            "middle",
            "#52657a",
        )
    )
    return chart_frame(
        "M1 optimisation levels",
        "Four registered workloads × eight isolated release builds",
        body,
    )


def pages_svg(rows: list[dict[str, str]]) -> str:
    """Render the verified Linux base-page and huge-page comparison"""
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
    maximum = max(values) * 1.2
    top, bottom = 155, 475
    body = [
        text(
            600, 112, "Throughput by page policy", 18, "middle", "#24364b", 700
        ),
        f'<line x1="180" y1="{bottom}" x2="1020" y2="{bottom}" stroke="#52657a"/>',
    ]
    for x, name, value, colour in zip(
        (300, 720),
        ("base", "huge"),
        values,
        ("#52657a", "#155eef"),
        strict=True,
    ):
        height = value / maximum * (bottom - top)
        y = bottom - height
        body.append(
            f'<rect x="{x}" y="{y:.1f}" width="180" height="{height:.1f}" rx="6" fill="{colour}"/>'
        )
        body.append(
            text(x + 90, y - 16, f"{value:.2f}", 15, "middle", colour, 700)
        )
        body.append(
            text(
                x + 90,
                510,
                "4 KiB base" if name == "base" else "2 MiB huge",
                14,
                "middle",
                "#24364b",
                700,
            )
        )
    body.append(
        text(
            600,
            552,
            f"Huge/base throughput = {values[1] / values[0]:.2f}x",
            16,
            "middle",
            "#24364b",
            700,
        )
    )
    body.append(
        text(
            600,
            578,
            "Backing verified from /proc/self/smaps; advice alone is not evidence.",
            12,
            "middle",
            "#52657a",
        )
    )
    return chart_frame("Page-size effect", "10M-cell Conway case", body)


def page_experiment(
    samples: int, minimum_ms: int, build: Path | None = None
) -> list[dict[str, str]]:
    """Compare Linux base and huge page runs and require backing checks"""
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
    if unverified:
        raise ValueError(f"page backing unverified for {', '.join(unverified)}")
    if len({row["checksum"] for row in rows}) != 1:
        raise ValueError("base and huge pages produced different checksums")
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
                "| Level | Conway 1M | PDE heat | Chess | Carrom |",
                "| ---: | ---: | ---: | ---: | ---: |",
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
        lines.extend(["", f"Huge/base throughput: **{huge / base:.3f}x**"])
    lines.append("")
    return "\n".join(lines)


def read_rows(target: Path) -> list[dict[str, str]]:
    with target.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


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
    if pages:
        (OUT / "pages.svg").write_text(pages_svg(pages), encoding="utf-8")
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
        for name, content in (
            ("scaling.svg", scaling_svg(scaling)),
            ("levels.svg", levels_svg(levels)),
            ("pages.svg", pages_svg(pages)),
        ):
            target = Path(directory) / name
            target.write_text(content, encoding="utf-8")
            ET.parse(target)
    report = summary(scaling, levels, pages)
    if "M cell-updates/s" not in report or "Huge/base throughput" not in report:
        raise AssertionError("summary is incomplete")
    print("report self-check: PASS")


def main() -> int:
    """Dispatch collection, graphing, page checks, or self-check by mode"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        nargs="?",
        choices=("all", "cluster", "graph", "levels", "page", "self-check"),
        default="all",
    )
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
    if options.mode == "levels":
        write_provenance(options.samples, options.minimum_case_ms)
        collect_levels(options.samples, options.minimum_case_ms)
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
