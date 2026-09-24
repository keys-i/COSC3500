#!/usr/bin/env python3
"""Validate independent M2 baseline and parallel profile rows"""

from __future__ import annotations

import csv
import math
import statistics
import sys
import tempfile
from pathlib import Path

SEEDS = (31, 32, 33)
THREADS = (1, 2, 4, 8)
VARIANTS = (
    ("baseline", "m2", 0, (1,)),
    ("final-serial", "m2", 7, (1,)),
    ("final-openmp", "m2", 7, THREADS),
)
REQUIRED = {
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
    "checksum",
    "verified",
}


def raw_path(run: Path, variant: str, seed: int, threads: int) -> Path:
    return (
        run / variant / f"seed-{seed}" / f"threads-{threads}" / "timing.raw.csv"
    )


def read_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if not REQUIRED.issubset(reader.fieldnames or ()):
            raise ValueError(f"{path}: missing benchmark fields")
        rows = list(reader)
    if (
        len(rows) != 1
        or rows[0]["verified"] != "true"
        or not rows[0]["checksum"]
    ):
        raise ValueError(f"{path}: expected one verified benchmark row")
    row = rows[0]
    for field in (
        "median_ns_per_unit",
        "ci_low_ns_per_unit",
        "ci_high_ns_per_unit",
    ):
        value = float(row[field])
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"{path}: invalid {field}")
    return row


def validate_provenance(run: Path) -> None:
    lines = set(
        (run / "provenance.txt").read_text(encoding="utf-8").splitlines()
    )
    if not {
        "baseline_engine=m2",
        "final_engine=m2",
        "baseline_level=0",
        "final_level=7",
    }.issubset(lines):
        raise ValueError(f"{run}: expected an M2 L0 to M2 L7 profile run")


def summarise(run: Path) -> None:
    validate_provenance(run)
    rows: list[dict[str, object]] = []
    by_seed: dict[tuple[str, int, int], dict[str, str]] = {}
    for variant, engine, level, thread_counts in VARIANTS:
        for seed in SEEDS:
            for threads in thread_counts:
                row = read_row(raw_path(run, variant, seed, threads))
                by_seed[variant, seed, threads] = row
                rows.append(
                    {
                        "variant": variant,
                        "engine": engine,
                        "opt_level": level,
                        "seed": seed,
                        "threads": threads,
                        **row,
                    }
                )
    for seed in SEEDS:
        reference = by_seed["baseline", seed, 1]
        checksum = reference["checksum"]
        for variant, _, _, thread_counts in VARIANTS[1:]:
            for threads in thread_counts:
                candidate = by_seed[variant, seed, threads]
                if (
                    candidate["case"] != reference["case"]
                    or candidate["unit"] != reference["unit"]
                ):
                    raise ValueError(
                        f"seed {seed}: {variant} p{threads} ran a different workload"
                    )
                if candidate["checksum"] != checksum:
                    raise ValueError(
                        f"seed {seed}: {variant} p{threads} checksum differs from M2 L0"
                    )
    for row in rows:
        seed, threads = int(row["seed"]), int(row["threads"])
        baseline = float(by_seed["baseline", seed, 1]["median_ns_per_unit"])
        final_p1 = float(by_seed["final-openmp", seed, 1]["median_ns_per_unit"])
        value = float(row["median_ns_per_unit"])
        row["speedup_vs_m2_l0"] = f"{baseline / value:.6f}"
        row["speedup_vs_m2_openmp_p1"] = (
            f"{final_p1 / value:.6f}"
            if row["variant"] == "final-openmp"
            else ""
        )
        row["parallel_efficiency"] = (
            f"{final_p1 / value / threads:.6f}"
            if row["variant"] == "final-openmp"
            else ""
        )
    with (run / "profile-summary.csv").open(
        "w", newline="", encoding="utf-8"
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=tuple(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    with (run / "timing.csv").open("w", newline="", encoding="utf-8") as handle:
        fields = (
            "variant",
            "engine",
            "seed",
            "threads",
            "median_ns_per_unit",
            "checksum",
        )
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(
            {field: row[field] for field in fields} for row in rows
        )
    with (run / "strong-scaling.csv").open(
        "w", newline="", encoding="utf-8"
    ) as handle:
        fields = (
            "seed",
            "threads",
            "median_ns_per_unit",
            "speedup",
            "efficiency",
            "checksum",
        )
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            if row["variant"] == "final-openmp":
                writer.writerow(
                    {
                        "seed": row["seed"],
                        "threads": row["threads"],
                        "median_ns_per_unit": row["median_ns_per_unit"],
                        "speedup": row["speedup_vs_m2_openmp_p1"],
                        "efficiency": row["parallel_efficiency"],
                        "checksum": row["checksum"],
                    }
                )
    aggregate: list[dict[str, object]] = []
    for variant, engine, level, thread_counts in VARIANTS:
        for threads in thread_counts:
            selected = [
                row
                for row in rows
                if row["variant"] == variant and row["threads"] == threads
            ]
            aggregate.append(
                {
                    "variant": variant,
                    "engine": engine,
                    "opt_level": level,
                    "threads": threads,
                    "median_ns_per_unit": f"{statistics.median(float(row['median_ns_per_unit']) for row in selected):.6f}",
                    "median_speedup_vs_m2_l0": f"{statistics.median(float(row['speedup_vs_m2_l0']) for row in selected):.6f}",
                    "median_parallel_efficiency": f"{statistics.median(float(row['parallel_efficiency']) for row in selected if row['parallel_efficiency']):.6f}"
                    if variant == "final-openmp"
                    else "",
                }
            )
    with (run / "profile-aggregate.csv").open(
        "w", newline="", encoding="utf-8"
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=tuple(aggregate[0]))
        writer.writeheader()
        writer.writerows(aggregate)


def self_test() -> None:
    assert raw_path(Path("run"), "baseline", 31, 1) == Path(
        "run/baseline/seed-31/threads-1/timing.raw.csv"
    )
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "timing.raw.csv"
        header = "case,samples,median_ns_per_unit,ci_low_ns_per_unit,ci_high_ns_per_unit,cv_percent,unit,state_bytes,pair_evaluations,pair_list_rebuilds,pair_list_bytes,checksum,verified\n"
        row = "continuous/predator-prey/100k,5,1,1,1,0,entity_updates,1,1,1,1,abc,true\n"
        path.write_text(header + row, encoding="utf-8")
        assert read_row(path)["checksum"] == "abc"
        provenance = Path(temporary) / "provenance.txt"
        provenance.write_text(
            "baseline_engine=m1\nfinal_engine=m2\nbaseline_level=0\nfinal_level=7\n",
            encoding="utf-8",
        )
        try:
            validate_provenance(Path(temporary))
        except ValueError:
            pass
        else:
            raise AssertionError("M1 baseline must not be labelled as M2 L0")
        provenance.write_text(
            "baseline_engine=m2\nfinal_engine=m2\nbaseline_level=0\nfinal_level=7\n",
            encoding="utf-8",
        )
        validate_provenance(Path(temporary))
        for variant, _, _, thread_counts in VARIANTS:
            for seed in SEEDS:
                for threads in thread_counts:
                    raw = raw_path(Path(temporary), variant, seed, threads)
                    raw.parent.mkdir(parents=True, exist_ok=True)
                    raw.write_text(header + row, encoding="utf-8")
        summarise(Path(temporary))
        with (Path(temporary) / "profile-aggregate.csv").open(
            newline="", encoding="utf-8"
        ) as handle:
            aggregate = list(csv.DictReader(handle))
        assert aggregate[0]["engine"] == "m2"
        assert aggregate[0]["median_speedup_vs_m2_l0"] == "1.000000"
        raw_path(Path(temporary), "final-openmp", 33, 8).write_text(
            header + row.replace(",abc,true", ",different,true"),
            encoding="utf-8",
        )
        try:
            summarise(Path(temporary))
        except ValueError as error:
            assert "checksum differs" in str(error)
        else:
            raise AssertionError(
                "checksum mismatch must reject the profile run"
            )


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) == 2:
        summarise(Path(sys.argv[1]).resolve())
    else:
        raise SystemExit("usage: profiles.py RUN_DIRECTORY")
