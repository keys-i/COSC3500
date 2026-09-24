#!/usr/bin/env python3
"""Validate fixed-density OpenMP measurements and derive size-scaling tables"""

from __future__ import annotations

import csv
import math
import statistics
import sys
from pathlib import Path

SEEDS = (31, 32, 33)
THREADS = (1, 2, 4, 8)
CASES = (
    ("continuous/predator-prey/100k", 100_000, 6_600_012),
    ("continuous/predator-prey/200k", 200_000, 13_200_012),
    ("continuous/predator-prey/400k", 400_000, 26_400_012),
    ("continuous/predator-prey/800k", 800_000, 52_800_012),
)


def raw_path(run: Path, label: str, seed: int, threads: int) -> Path:
    return (
        run / label / f"seed-{seed}" / f"threads-{threads}" / "timing.raw.csv"
    )


def serial_path(run: Path, label: str, seed: int) -> Path:
    return run / "serial" / label / f"seed-{seed}" / "timing.raw.csv"


def measured_row(path: Path, case: str, state_bytes: int) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required = {
            "case",
            "samples",
            "median_ns_per_unit",
            "ci_low_ns_per_unit",
            "ci_high_ns_per_unit",
            "cv_percent",
            "state_bytes",
            "checksum",
            "verified",
            "pair_evaluations",
            "pair_list_rebuilds",
            "pair_list_bytes",
        }
        if not required.issubset(reader.fieldnames or ()):
            raise ValueError(f"{path}: missing benchmark fields")
        rows = list(reader)
    if (
        len(rows) != 1
        or rows[0]["case"] != case
        or rows[0]["verified"] != "true"
    ):
        raise ValueError(f"{path}: invalid or unverified benchmark row")
    row = rows[0]
    if int(row["samples"]) != 5 or int(row["state_bytes"]) != state_bytes:
        raise ValueError(f"{path}: unexpected sample count or state size")
    if not row["checksum"]:
        raise ValueError(f"{path}: missing checksum")
    for field in (
        "median_ns_per_unit",
        "ci_low_ns_per_unit",
        "ci_high_ns_per_unit",
    ):
        value = float(row[field])
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"{path}: invalid {field}")
    return row


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=tuple(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def weak_scaling_metrics(
    per_unit: float, entities: int, reference_per_unit: float
) -> tuple[float, float]:
    normalized_elapsed = per_unit * entities / (reference_per_unit * 100_000)
    return normalized_elapsed, 1.0 / normalized_elapsed


def summarize(run: Path) -> None:
    timings: list[dict[str, object]] = []
    summaries: list[dict[str, object]] = []
    rows_by_case: dict[str, dict[int, dict[int, dict[str, str]]]] = {}
    for case, entities, state_bytes in CASES:
        label = case.rsplit("/", 1)[-1]
        rows_by_seed: dict[int, dict[int, dict[str, str]]] = {}
        for seed in SEEDS:
            serial = measured_row(
                serial_path(run, label, seed), case, state_bytes
            )
            rows = {
                threads: measured_row(
                    raw_path(run, label, seed, threads), case, state_bytes
                )
                for threads in THREADS
            }
            if (
                len(
                    {
                        serial["checksum"],
                        *(row["checksum"] for row in rows.values()),
                    }
                )
                != 1
            ):
                raise ValueError(
                    f"{label} seed {seed}: serial and OpenMP checksums differ"
                )
            rows_by_seed[seed] = rows
            reference = float(rows[1]["median_ns_per_unit"])
            serial_elapsed = (
                float(serial["median_ns_per_unit"]) * entities * 10 / 1e9
            )
            timings.append(
                {
                    "case": case,
                    "entities": entities,
                    "variant": "serial",
                    "seed": seed,
                    "threads": 1,
                    "samples": serial["samples"],
                    "median_ns_per_unit": serial["median_ns_per_unit"],
                    "ci_low_ns_per_unit": serial["ci_low_ns_per_unit"],
                    "ci_high_ns_per_unit": serial["ci_high_ns_per_unit"],
                    "cv_percent": serial["cv_percent"],
                    "state_bytes": serial["state_bytes"],
                    "median_elapsed_seconds": f"{serial_elapsed:.9f}",
                    "speedup_vs_openmp_p1": f"{float(rows[1]['median_ns_per_unit']) / float(serial['median_ns_per_unit']):.6f}",
                    "efficiency": "",
                    "checksum": serial["checksum"],
                    "pair_evaluations": serial["pair_evaluations"],
                    "pair_list_rebuilds": serial["pair_list_rebuilds"],
                    "pair_list_bytes": serial["pair_list_bytes"],
                }
            )
            for threads, row in rows.items():
                elapsed_seconds = (
                    float(row["median_ns_per_unit"]) * entities * 10 / 1e9
                )
                speedup = reference / float(row["median_ns_per_unit"])
                timings.append(
                    {
                        "case": case,
                        "entities": entities,
                        "variant": "openmp",
                        "seed": seed,
                        "threads": threads,
                        "samples": row["samples"],
                        "median_ns_per_unit": row["median_ns_per_unit"],
                        "ci_low_ns_per_unit": row["ci_low_ns_per_unit"],
                        "ci_high_ns_per_unit": row["ci_high_ns_per_unit"],
                        "cv_percent": row["cv_percent"],
                        "state_bytes": row["state_bytes"],
                        "median_elapsed_seconds": f"{elapsed_seconds:.9f}",
                        "speedup_vs_openmp_p1": f"{speedup:.6f}",
                        "efficiency": f"{speedup / threads:.6f}",
                        "checksum": row["checksum"],
                        "pair_evaluations": row["pair_evaluations"],
                        "pair_list_rebuilds": row["pair_list_rebuilds"],
                        "pair_list_bytes": row["pair_list_bytes"],
                    }
                )
        for threads in THREADS:
            values = [
                float(rows_by_seed[seed][threads]["median_ns_per_unit"])
                for seed in SEEDS
            ]
            speedups = [
                float(rows_by_seed[seed][1]["median_ns_per_unit"])
                / float(rows_by_seed[seed][threads]["median_ns_per_unit"])
                for seed in SEEDS
            ]
            summaries.append(
                {
                    "case": case,
                    "entities": entities,
                    "threads": threads,
                    "median_ns_per_unit": f"{statistics.median(values):.6f}",
                    "min_seed_ns_per_unit": f"{min(values):.6f}",
                    "max_seed_ns_per_unit": f"{max(values):.6f}",
                    "median_speedup_vs_p1": f"{statistics.median(speedups):.6f}",
                    "median_efficiency": f"{statistics.median(speedups) / threads:.6f}",
                }
            )
        rows_by_case[case] = rows_by_seed
    write_csv(run / "size-scaling.csv", timings)
    write_csv(run / "fixed-workload-scaling-summary.csv", summaries)

    # Hold density and steps constant while growing N with the thread count
    weak_rows: list[dict[str, object]] = []
    for (case, entities, _), threads in zip(CASES, THREADS):
        for seed in SEEDS:
            reference = float(
                rows_by_case[CASES[0][0]][seed][1]["median_ns_per_unit"]
            )
            row = rows_by_case[case][seed][threads]
            per_unit = float(row["median_ns_per_unit"])
            normalized_elapsed, efficiency = weak_scaling_metrics(
                per_unit, entities, reference
            )
            weak_rows.append(
                {
                    "case": case,
                    "entities": entities,
                    "threads": threads,
                    "seed": seed,
                    "median_ns_per_unit": row["median_ns_per_unit"],
                    "normalised_elapsed_vs_100k_p1": f"{normalized_elapsed:.6f}",
                    "weak_scaling_efficiency": f"{efficiency:.6f}",
                    "checksum": row["checksum"],
                }
            )
    write_csv(run / "weak-scaling.csv", weak_rows)
    weak_summary = []
    for case, entities, _ in CASES:
        values = [row for row in weak_rows if row["case"] == case]
        weak_summary.append(
            {
                "case": case,
                "entities": entities,
                "threads": values[0]["threads"],
                "median_normalised_elapsed_vs_100k_p1": f"{statistics.median(float(row['normalised_elapsed_vs_100k_p1']) for row in values):.6f}",
                "median_weak_scaling_efficiency": f"{statistics.median(float(row['weak_scaling_efficiency']) for row in values):.6f}",
            }
        )
    write_csv(run / "weak-scaling-summary.csv", weak_summary)


def self_test() -> None:
    elapsed, efficiency = weak_scaling_metrics(50.0, 200_000, 100.0)
    assert elapsed == 1.0
    assert efficiency == 1.0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) != 2:
        raise SystemExit("usage: scaling.py RUN_DIRECTORY")
    else:
        summarize(Path(sys.argv[1]).resolve())
