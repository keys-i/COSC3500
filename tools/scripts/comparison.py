#!/usr/bin/env python3
"""Validate paired M2 CSR A/B measurements and write deterministic summaries"""

from __future__ import annotations

import csv
import math
import statistics
import sys
import tempfile
from pathlib import Path

SEEDS = (31, 32, 33)
THREADS = (1, 2, 4, 8)
ROUNDS = (1, 2)
VARIANTS = ("baseline", "candidate")
CASE = "continuous/predator-prey/100k"
SAMPLES = 5
STATE_BYTES = 6_600_012
REQUIRED = {
    "case",
    "samples",
    "median_ns_per_unit",
    "checksum",
    "verified",
    "pair_evaluations",
    "pair_list_bytes",
    "peak_rss_bytes",
    "state_bytes",
}


def raw_path(
    run: Path, seed: int, threads: int, round_number: int, variant: str
) -> Path:
    return (
        run
        / f"seed-{seed}"
        / f"threads-{threads}"
        / f"round-{round_number}"
        / f"{variant}.raw.csv"
    )


def integer(path: Path, row: dict[str, str], field: str) -> int:
    try:
        value = int(row[field])
    except (KeyError, ValueError) as error:
        raise ValueError(f"{path}: invalid {field}") from error
    if value < 0:
        raise ValueError(f"{path}: negative {field}")
    return value


def read_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if not REQUIRED.issubset(reader.fieldnames or ()):
            raise ValueError(f"{path}: missing benchmark fields")
        rows = list(reader)
    if len(rows) != 1:
        raise ValueError(f"{path}: expected exactly one benchmark row")
    row = rows[0]
    if row["case"] != CASE or row["verified"] != "true" or not row["checksum"]:
        raise ValueError(f"{path}: invalid or unverified workload")
    if (
        integer(path, row, "samples") != SAMPLES
        or integer(path, row, "state_bytes") != STATE_BYTES
    ):
        raise ValueError(f"{path}: unexpected samples or state size")
    for field in ("pair_evaluations", "pair_list_bytes", "peak_rss_bytes"):
        integer(path, row, field)
    try:
        elapsed = float(row["median_ns_per_unit"])
    except ValueError as error:
        raise ValueError(f"{path}: invalid median_ns_per_unit") from error
    if not math.isfinite(elapsed) or elapsed <= 0:
        raise ValueError(f"{path}: invalid median_ns_per_unit")
    return row


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=tuple(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarise(run: Path) -> None:
    summaries: list[dict[str, object]] = []
    checksums: dict[int, str] = {}
    for seed in SEEDS:
        for threads in THREADS:
            rows = {
                (round_number, variant): read_row(
                    raw_path(run, seed, threads, round_number, variant)
                )
                for round_number in ROUNDS
                for variant in VARIANTS
            }
            observed = {row["checksum"] for row in rows.values()}
            if len(observed) != 1:
                raise ValueError(
                    f"seed {seed} p{threads}: checksum differs across rounds or variants"
                )
            checksum = observed.pop()
            previous = checksums.setdefault(seed, checksum)
            if previous != checksum:
                raise ValueError(
                    f"seed {seed}: checksum differs across thread counts"
                )
            baseline = [
                float(rows[round_number, "baseline"]["median_ns_per_unit"])
                for round_number in ROUNDS
            ]
            candidate = [
                float(rows[round_number, "candidate"]["median_ns_per_unit"])
                for round_number in ROUNDS
            ]
            ratios = [
                baseline[index] / candidate[index]
                for index in range(len(ROUNDS))
            ]
            summaries.append(
                {
                    "seed": seed,
                    "threads": threads,
                    "checksum": checksum,
                    "baseline_median_ns_per_unit": f"{statistics.median(baseline):.6f}",
                    "candidate_median_ns_per_unit": f"{statistics.median(candidate):.6f}",
                    "median_paired_speedup_baseline_over_candidate": f"{statistics.median(ratios):.6f}",
                    "round_1_paired_speedup": f"{ratios[0]:.6f}",
                    "round_2_paired_speedup": f"{ratios[1]:.6f}",
                    "baseline_max_peak_rss_bytes": max(
                        integer(
                            raw_path(
                                run, seed, threads, round_number, "baseline"
                            ),
                            rows[round_number, "baseline"],
                            "peak_rss_bytes",
                        )
                        for round_number in ROUNDS
                    ),
                    "candidate_max_peak_rss_bytes": max(
                        integer(
                            raw_path(
                                run, seed, threads, round_number, "candidate"
                            ),
                            rows[round_number, "candidate"],
                            "peak_rss_bytes",
                        )
                        for round_number in ROUNDS
                    ),
                    "baseline_max_pair_list_bytes": max(
                        integer(
                            raw_path(
                                run, seed, threads, round_number, "baseline"
                            ),
                            rows[round_number, "baseline"],
                            "pair_list_bytes",
                        )
                        for round_number in ROUNDS
                    ),
                    "candidate_max_pair_list_bytes": max(
                        integer(
                            raw_path(
                                run, seed, threads, round_number, "candidate"
                            ),
                            rows[round_number, "candidate"],
                            "pair_list_bytes",
                        )
                        for round_number in ROUNDS
                    ),
                    "baseline_max_pair_evaluations": max(
                        integer(
                            raw_path(
                                run, seed, threads, round_number, "baseline"
                            ),
                            rows[round_number, "baseline"],
                            "pair_evaluations",
                        )
                        for round_number in ROUNDS
                    ),
                    "candidate_max_pair_evaluations": max(
                        integer(
                            raw_path(
                                run, seed, threads, round_number, "candidate"
                            ),
                            rows[round_number, "candidate"],
                            "pair_evaluations",
                        )
                        for round_number in ROUNDS
                    ),
                }
            )
    write_csv(run / "ab-summary.csv", summaries)
    aggregates: list[dict[str, object]] = []
    for threads in THREADS:
        selected = [row for row in summaries if row["threads"] == threads]
        aggregates.append(
            {
                "threads": threads,
                "seed_count": len(selected),
                "median_baseline_ns_per_unit": f"{statistics.median(float(row['baseline_median_ns_per_unit']) for row in selected):.6f}",
                "median_candidate_ns_per_unit": f"{statistics.median(float(row['candidate_median_ns_per_unit']) for row in selected):.6f}",
                "median_seed_paired_speedup_baseline_over_candidate": f"{statistics.median(float(row['median_paired_speedup_baseline_over_candidate']) for row in selected):.6f}",
                "baseline_max_peak_rss_bytes": max(
                    int(row["baseline_max_peak_rss_bytes"]) for row in selected
                ),
                "candidate_max_peak_rss_bytes": max(
                    int(row["candidate_max_peak_rss_bytes"]) for row in selected
                ),
                "baseline_max_pair_list_bytes": max(
                    int(row["baseline_max_pair_list_bytes"]) for row in selected
                ),
                "candidate_max_pair_list_bytes": max(
                    int(row["candidate_max_pair_list_bytes"])
                    for row in selected
                ),
            }
        )
    write_csv(run / "ab-aggregate.csv", aggregates)


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        run = Path(temporary)
        for seed in SEEDS:
            for threads in THREADS:
                for round_number in ROUNDS:
                    for variant in VARIANTS:
                        path = raw_path(
                            run, seed, threads, round_number, variant
                        )
                        path.parent.mkdir(parents=True, exist_ok=True)
                        elapsed = 100 if variant == "baseline" else 80
                        path.write_text(
                            "case,samples,median_ns_per_unit,checksum,verified,pair_evaluations,pair_list_bytes,peak_rss_bytes,state_bytes\n"
                            f"{CASE},{SAMPLES},{elapsed},checksum-{seed},true,10,20,30,{STATE_BYTES}\n",
                            encoding="utf-8",
                        )
        summarise(run)
        with (run / "ab-aggregate.csv").open(
            newline="", encoding="utf-8"
        ) as handle:
            rows = list(csv.DictReader(handle))
        assert len(rows) == len(THREADS)
        assert (
            rows[0]["median_seed_paired_speedup_baseline_over_candidate"]
            == "1.250000"
        )


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) == 2:
        summarise(Path(sys.argv[1]).resolve())
    else:
        raise SystemExit("usage: comparison.py RUN_DIRECTORY")
