#!/usr/bin/env python3
"""Compare two perf-harness JSON runs and gate on regressions.

Usage:
    compare.py --baseline ci-baseline.json --current current.json [--opt opt.json]
               [--threshold 25] [--gate]

Reads the `--json` output of the bench harness (see bench/main.cpp). Prints a
markdown delta table (also appended to $GITHUB_STEP_SUMMARY when set) and, with
--gate, exits non-zero if a *stable* metric regressed past --threshold percent.

Robustness notes for shared CI hardware:
  * Compares min_ns (the cleanest floor), not mean/median.
  * Gates only stable metrics — anything whose name contains "N-thread" is the
    multi-thread contention number, which is noisy on virtualized runners, so it
    is reported but never gates.
  * If the baseline carries `"placeholder": true` (the seed baseline committed
    before CI has produced its own), comparison is advisory: shown, never fails.
"""

import argparse
import json
import os
import sys


def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    by_name = {r["name"]: r for r in doc.get("results", [])}
    return doc, by_name


def is_gated(name):
    # Multi-thread contention is too noisy on shared runners to gate on.
    return "N-thread" not in name


def fmt_pct(p):
    return f"{p:+.1f}%"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--current", required=True)
    ap.add_argument("--opt", help="optional second run (e.g. LTO+AVX2) to show headroom")
    ap.add_argument("--threshold", type=float, default=25.0,
                    help="max %% regression of a stable metric before failing (with --gate)")
    ap.add_argument("--gate", action="store_true", help="exit non-zero on regression")
    args = ap.parse_args()

    base_doc, base = load(args.baseline)
    _, curr = load(args.current)
    opt = load(args.opt)[1] if args.opt else None

    placeholder = bool(base_doc.get("placeholder"))

    lines = []
    lines.append(f"### perf harness — current vs baseline `{base_doc.get('label', '?')}`")
    if placeholder:
        lines.append("")
        lines.append("> ⚠️ **Seed baseline** (`placeholder: true`) — generated off-CI, so "
                     "deltas are advisory until CI refreshes it (push to `master` or run the "
                     "*Update baseline* workflow).")
    lines.append("")
    lines.append("| metric | baseline (min) | current (min) | Δ |")
    lines.append("|---|---:|---:|---:|")

    regressions = []
    for name in curr:
        c = curr[name]["min_ns"]
        if name in base:
            b = base[name]["min_ns"]
            pct = (c - b) / b * 100.0 if b > 0 else 0.0
            flag = ""
            if is_gated(name) and pct > args.threshold:
                flag = " 🔴"
                regressions.append((name, pct))
            elif pct < -args.threshold:
                flag = " 🟢"
            lines.append(f"| `{name}` | {b:.2f} ns | {c:.2f} ns | {fmt_pct(pct)}{flag} |")
        else:
            lines.append(f"| `{name}` | — (new) | {c:.2f} ns | n/a |")

    if opt:
        lines.append("")
        lines.append("#### codegen headroom (optimized build vs current)")
        lines.append("| metric | current | optimized | speedup |")
        lines.append("|---|---:|---:|---:|")
        for name in curr:
            if name in opt and curr[name]["min_ns"] > 0:
                c = curr[name]["min_ns"]
                o = opt[name]["min_ns"]
                lines.append(f"| `{name}` | {c:.2f} ns | {o:.2f} ns | {c / o:.2f}× |")

    report = "\n".join(lines)
    print(report)

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write(report + "\n")

    if args.gate and not placeholder and regressions:
        print("\nREGRESSION: " + ", ".join(f"{n} {fmt_pct(p)}" for n, p in regressions),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
