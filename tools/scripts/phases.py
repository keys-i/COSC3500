#!/usr/bin/env python3
"""Validate phase diagnostics and summarise their medians"""

from __future__ import annotations

import csv
import re
import statistics
import sys
import tempfile
from pathlib import Path

SEEDS = (31, 32, 33)
THREADS = (1, 2, 4, 8)
RUNS = (1, 2, 3, 4, 5)
PHASES = (
    "prepare",
    "csr_rebuild",
    "csr_reuse_publish",
    "other_search",
    "commit",
)
CSR_REBUILD_DETAILS = (
    "csr_count_traversal",
    "csr_fill_traversal",
    "csr_rebuild_residual",
)
PHASE_LINE = re.compile(r"^M2_PHASE_PROFILE (.+)$")
SUMMARY_CHECKSUM = re.compile(r"(?:^|\s)checksum=([0-9a-f]{16})(?:\s|$)")
SUMMARY_STEPS = re.compile(r"(?:^|\s)steps=(\d+)(?:\s|$)")


def fail(message: str) -> None:
    raise ValueError(message)


def reference_checksums(path: Path) -> dict[int, str]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    required = {"seed", "checksum"}
    if not rows or not required.issubset(rows[0]):
        fail(f"{path}: missing seed or checksum column")
    checksums: dict[int, set[str]] = {seed: set() for seed in SEEDS}
    for row in rows:
        seed = int(row["seed"])
        if seed in checksums:
            checksums[seed].add(row["checksum"])
    result = {}
    for seed, values in checksums.items():
        if len(values) != 1:
            fail(f"{path}: seed {seed} does not have one reference checksum")
        result[seed] = values.pop()
    return result


def read_phase(path: Path) -> dict[str, int | str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if len(lines) != 1:
        fail(f"{path}: expected one stderr line")
    match = PHASE_LINE.fullmatch(lines[0])
    if match is None:
        fail(f"{path}: missing phase record")
    fields: dict[str, str] = {}
    for field in match.group(1).split():
        key, separator, value = field.partition("=")
        if not separator or key in fields:
            fail(f"{path}: malformed phase field {field!r}")
        fields[key] = value
    expected = {
        "diagnostic": "not_comparable",
        "fixed_member_steps": "10",
        "csr_rebuild_steps": "1",
        "csr_count_traversal_steps": "1",
        "csr_reuse_publish_steps": "9",
        "other_search_steps": "0",
        "commit_steps": "10",
    }
    if fields.get("version") not in {"2", "3"} or any(
        fields.get(key) != value for key, value in expected.items()
    ):
        fail(f"{path}: unexpected phase counts or diagnostic marker")
    result: dict[str, int | str] = {"version": fields["version"]}
    for phase in PHASES:
        key = f"{phase}_ns"
        try:
            value = int(fields[key])
        except (KeyError, ValueError) as error:
            raise ValueError(f"{path}: invalid {key}") from error
        if value < 0:
            fail(f"{path}: negative {key}")
        result[phase] = value
    try:
        fill_steps = int(fields["csr_fill_traversal_steps"])
    except (KeyError, ValueError) as error:
        raise ValueError(f"{path}: invalid csr_fill_traversal_steps") from error
    if fill_steps not in (0, 1):
        fail(f"{path}: invalid csr_fill_traversal_steps")
    for detail in CSR_REBUILD_DETAILS:
        key = f"{detail}_ns"
        try:
            value = int(fields[key])
        except (KeyError, ValueError) as error:
            raise ValueError(f"{path}: invalid {key}") from error
        if value < 0:
            fail(f"{path}: negative {key}")
        result[detail] = value
    result["csr_fill_traversal_steps"] = fill_steps
    if result["csr_fill_traversal"] and not fill_steps:
        fail(f"{path}: fill time without a fill traversal")
    if (
        result["csr_count_traversal"]
        + result["csr_fill_traversal"]
        + result["csr_rebuild_residual"]
        != result["csr_rebuild"]
    ):
        fail(f"{path}: CSR rebuild parts do not sum to rebuild time")
    return result


def summary(path: Path) -> tuple[str, str]:
    text = path.read_text(encoding="utf-8")
    checksums = SUMMARY_CHECKSUM.findall(text)
    steps = SUMMARY_STEPS.findall(text)
    if len(checksums) != 1 or steps != ["10"]:
        fail(f"{path}: expected one checksum and steps=10")
    return checksums[0], steps[0]


def write_report(run: Path, reference: Path) -> None:
    expected_checksums = reference_checksums(reference)
    rows: list[dict[str, object]] = []
    run_version: str | None = None
    variants = (("serial", (1,)), ("openmp", THREADS))
    for variant, threads_list in variants:
        for seed in SEEDS:
            for threads in threads_list:
                samples = []
                for iteration in RUNS:
                    directory = (
                        run
                        / variant
                        / f"seed-{seed}"
                        / f"threads-{threads}"
                        / f"run-{iteration}"
                    )
                    actual, _ = summary(directory / "stdout.txt")
                    if actual != expected_checksums[seed]:
                        fail(
                            f"{directory}: checksum {actual} != {expected_checksums[seed]}"
                        )
                    samples.append(read_phase(directory / "stderr.txt"))
                versions = {sample["version"] for sample in samples}
                if len(versions) != 1:
                    fail(
                        f"{variant} seed={seed} threads={threads}: mixed diagnostic versions"
                    )
                version = str(versions.pop())
                if run_version is None:
                    run_version = version
                elif version != run_version:
                    fail(f"{run}: mixed diagnostic versions")
                medians = {
                    phase: statistics.median(
                        sample[phase] for sample in samples
                    )
                    for phase in PHASES
                }
                totals = [
                    sum(sample[phase] for phase in PHASES) for sample in samples
                ]
                if any(total <= 0 for total in totals):
                    fail(
                        f"{variant} seed={seed} threads={threads}: zero diagnostic time"
                    )
                row: dict[str, object] = {
                    "diagnostic": "not_comparable_to_release",
                    "phase_version": version,
                    "variant": variant,
                    "seed": seed,
                    "threads": threads,
                    "runs": len(samples),
                    "checksum": expected_checksums[seed],
                    "source": f"{variant}/seed-{seed}/threads-{threads}/run-{{1..5}}",
                    "reference_timing_csv": str(reference),
                    "median_total_ns": f"{statistics.median(totals):.0f}",
                }
                for phase in PHASES:
                    row[f"median_{phase}_ns"] = f"{medians[phase]:.0f}"
                    row[f"{phase}_share"] = (
                        f"{statistics.median(sample[phase] / total for sample, total in zip(samples, totals)):.6f}"
                    )
                for detail in CSR_REBUILD_DETAILS:
                    median = statistics.median(
                        sample[detail] for sample in samples
                    )
                    row[f"median_{detail}_ns"] = f"{median:.0f}"
                    row[f"{detail}_of_rebuild"] = (
                        f"{statistics.median(sample[detail] / sample['csr_rebuild'] for sample in samples):.6f}"
                        if all(sample["csr_rebuild"] for sample in samples)
                        else "0.000000"
                    )
                fill_steps = {
                    sample["csr_fill_traversal_steps"] for sample in samples
                }
                if len(fill_steps) != 1:
                    fail(
                        f"{variant} seed={seed} threads={threads}: unstable CSR fill occurrence"
                    )
                row["csr_fill_traversal_steps"] = str(fill_steps.pop())
                rows.append(row)
    destination = run / "phase-summary.csv"
    with destination.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=tuple(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Diagnostic phase medians written to {destination}")


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "stderr.txt"
        path.write_text(
            "M2_PHASE_PROFILE version=2 diagnostic=not_comparable fixed_member_steps=10 csr_rebuild_steps=1 csr_count_traversal_steps=1 csr_fill_traversal_steps=1 csr_reuse_publish_steps=9 other_search_steps=0 commit_steps=10 prepare_ns=1 csr_rebuild_ns=6 csr_count_traversal_ns=2 csr_fill_traversal_ns=3 csr_rebuild_residual_ns=1 csr_reuse_publish_ns=3 other_search_ns=0 commit_ns=4\n",
            encoding="utf-8",
        )
        record = read_phase(path)
        assert record["csr_rebuild"] == 6
        assert record["csr_fill_traversal"] == 3
        path.write_text(
            "M2_PHASE_PROFILE version=3 diagnostic=not_comparable fixed_member_steps=10 csr_rebuild_steps=1 csr_count_traversal_steps=1 csr_fill_traversal_steps=0 csr_reuse_publish_steps=9 other_search_steps=0 commit_steps=10 prepare_ns=1 csr_rebuild_ns=6 csr_count_traversal_ns=2 csr_fill_traversal_ns=0 csr_rebuild_residual_ns=4 csr_reuse_publish_ns=3 other_search_ns=0 commit_ns=4\n",
            encoding="utf-8",
        )
        record = read_phase(path)
        assert record["version"] == "3"
        assert record["csr_fill_traversal_steps"] == 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) != 3:
        raise SystemExit("usage: phases.py RUN_DIRECTORY REFERENCE_TIMING_CSV")
    else:
        write_report(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
