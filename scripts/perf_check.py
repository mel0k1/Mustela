#!/usr/bin/env python3
"""Compare mustela perf.jsonl records against a baseline.

usage: perf_check.py <perf.jsonl> [--baseline BASELINE] [--update]
                    [--tolerance 1.25] [--slack-ms 100]

Without --baseline the script only prints the summary table.
--update writes the current summary as the new baseline.
Exit codes: 0 ok, 1 regression, 2 usage error.
"""

import argparse
import json
import math
import sys

KINDS = ("startup", "pageload", "memory")


def load_records(path):
    records = []
    try:
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    except FileNotFoundError:
        print(f"perf_check: {path} not found", file=sys.stderr)
        sys.exit(2)
    return records


def summarize(records):
    summary = {}
    for kind in KINDS:
        if kind == "memory":
            values = [r.get("bytes") for r in records
                      if r.get("kind") == kind and isinstance(r.get("bytes"), (int, float))]
        else:
            values = [r.get("ms") for r in records
                      if r.get("kind") == kind and isinstance(r.get("ms"), (int, float))]
        values = [v for v in values if v is not None and v >= 0]
        if not values:
            continue
        values.sort()
        n = len(values)
        mid = n // 2
        median = values[mid] if n % 2 else (values[mid - 1] + values[mid]) / 2.0
        p95 = values[min(n, max(1, math.ceil(n * 0.95))) - 1]
        unit = "bytes" if kind == "memory" else "ms"
        summary[kind] = {
            "n": n,
            "min": round(values[0], 2),
            "median": round(median, 2),
            "p95": round(p95, 2),
            "max": round(values[-1], 2),
            "unit": unit,
        }
    return summary


def print_table(summary):
    for kind in KINDS:
        entry = summary.get(kind)
        if not entry:
            print(f"{kind:10}  no samples")
            continue
        print("{:10}  n={n:<5} min={min:>10.1f} median={median:>10.1f} "
              "p95={p95:>10.1f} max={max:>10.1f} {unit}".format(kind, **entry))


def check_regressions(summary, baseline, tolerance, slack_ms):
    regressed = False
    for kind in KINDS:
        current = summary.get(kind)
        expected = baseline.get(kind)
        if not current or not expected:
            continue
        if kind == "memory":
            base_value = expected.get("median", 0)
            limit = base_value * tolerance + slack_ms * 1024 * 1024
            value = current["median"]
            unit = "bytes"
        else:
            base_value = expected.get("median", 0)
            limit = base_value * tolerance + slack_ms
            value = current["median"]
            unit = "ms"
        status = "ok"
        if base_value > 0 and value > limit:
            status = "REGRESSED"
            regressed = True
        print(f"{kind:10}  median {value:.1f} {unit} vs baseline {base_value:.1f} "
              f"{unit} (limit {limit:.1f}) {status}")
    return regressed


def main():
    parser = argparse.ArgumentParser(description="mustela perf regression check")
    parser.add_argument("perf", help="path to perf.jsonl")
    parser.add_argument("--baseline", help="baseline JSON produced by --update")
    parser.add_argument("--update", action="store_true", help="write new baseline")
    parser.add_argument("--tolerance", type=float, default=1.25)
    parser.add_argument("--slack-ms", type=float, default=100.0)
    args = parser.parse_args()

    if args.update and not args.baseline:
        print("perf_check: --update requires --baseline", file=sys.stderr)
        sys.exit(2)

    summary = summarize(load_records(args.perf))
    if not summary:
        print(f"perf_check: no usable records in {args.perf}", file=sys.stderr)
        sys.exit(2)

    print_table(summary)

    if args.update:
        with open(args.baseline, "w", encoding="utf-8") as handle:
            json.dump(summary, handle, indent=2, sort_keys=True)
            handle.write("\n")
        print(f"baseline written to {args.baseline}")
        return 0

    if not args.baseline:
        return 0

    try:
        with open(args.baseline, "r", encoding="utf-8") as handle:
            baseline = json.load(handle)
    except FileNotFoundError:
        print(f"perf_check: baseline {args.baseline} not found", file=sys.stderr)
        sys.exit(2)

    if check_regressions(summary, baseline, args.tolerance, args.slack_ms):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())