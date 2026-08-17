#!/usr/bin/env python3
# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.

import argparse
import json
import os
import re
from pathlib import Path


SCENARIOS = ("Initial", "Resize", "Prepend", "FontSize")
ITEM_COUNTS = (50, 200, 1000)
NAME_PATTERN = re.compile(
    r"^GridLanesRelease/(GridLanes|Waterfall)/"
    r"(Initial|Resize|Prepend|FontSize)/(50|200|1000)_median$"
)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("--max-ratio", type=float, default=1.30)
    parser.add_argument("--max-bytes-per-item", type=float, default=64.0)
    return parser.parse_args()


def load_medians(path):
    report = json.loads(path.read_text())
    medians = {}
    for benchmark in report.get("benchmarks", []):
        match = NAME_PATTERN.match(benchmark.get("name", ""))
        if match:
            kind, scenario, item_count = match.groups()
            medians[(kind, scenario, int(item_count))] = benchmark
    return medians


def main():
    args = parse_args()
    medians = load_medians(args.results)
    rows = []
    failures = []
    for scenario in SCENARIOS:
        for item_count in ITEM_COUNTS:
            grid_key = ("GridLanes", scenario, item_count)
            waterfall_key = ("Waterfall", scenario, item_count)
            if grid_key not in medians or waterfall_key not in medians:
                failures.append(f"missing median pair: {scenario}/{item_count}")
                continue
            grid = medians[grid_key]
            waterfall = medians[waterfall_key]
            ratio = grid["cpu_time"] / waterfall["cpu_time"]
            bytes_per_item = grid.get("BytesPerItem")
            if ratio > args.max_ratio:
                failures.append(
                    f"{scenario}/{item_count}: ratio {ratio:.4f} > "
                    f"{args.max_ratio:.2f}"
                )
            if bytes_per_item is None:
                failures.append(
                    f"{scenario}/{item_count}: missing BytesPerItem counter"
                )
            elif bytes_per_item > args.max_bytes_per_item:
                failures.append(
                    f"{scenario}/{item_count}: {bytes_per_item:.3f} B/item > "
                    f"{args.max_bytes_per_item:.1f}"
                )
            rows.append(
                (
                    scenario,
                    item_count,
                    grid["cpu_time"],
                    waterfall["cpu_time"],
                    ratio,
                    bytes_per_item,
                )
            )

    lines = [
        "| Scenario | Items | Grid Lanes CPU | Waterfall CPU | Ratio | B/item |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for scenario, item_count, grid_cpu, waterfall_cpu, ratio, bytes_per_item in rows:
        bytes_text = "missing" if bytes_per_item is None else f"{bytes_per_item:.3f}"
        lines.append(
            f"| {scenario} | {item_count} | {grid_cpu:.0f} ns | "
            f"{waterfall_cpu:.0f} ns | {ratio:.3f}x | "
            f"{bytes_text} |"
        )
    table = "\n".join(lines)
    print(table)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as output:
            output.write(
                "\n## Grid Lanes Performance\n\n"
                f"Budgets: ratio <= {args.max_ratio:.2f}x; "
                f"bookkeeping <= {args.max_bytes_per_item:.1f} B/item.\n\n"
                f"{table}\n"
            )
    if failures:
        raise SystemExit("benchmark budget failed:\n- " + "\n- ".join(failures))
    print(
        f"PASS ratio<={args.max_ratio:.2f}x "
        f"bytes/item<={args.max_bytes_per_item:.1f}"
    )


if __name__ == "__main__":
    main()
