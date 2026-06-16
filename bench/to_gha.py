#!/usr/bin/env python3
"""Convert the harness `--json` output into the JSON array that
benchmark-action/github-action-benchmark consumes with tool=customSmallerIsBetter
(lower value = better, which is true for every metric here — they're ns latencies).

The action stores each entry as a point in a time series on the gh-pages branch
and renders a per-metric chart, giving the historical trend.

By default the noisy multi-thread contention metrics (name contains "N-thread")
are excluded so the charted trend stays readable on shared CI hardware; pass
--all to include them.
"""

import argparse
import json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="harness --json output")
    ap.add_argument("--output", required=True, help="github-action-benchmark JSON path")
    ap.add_argument("--all", action="store_true", help="include noisy N-thread metrics")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as fh:
        doc = json.load(fh)

    out = []
    for r in doc.get("results", []):
        name = r["name"]
        if not args.all and "N-thread" in name:
            continue
        value = r["min_ns"]                      # the clean floor; lower is better
        p99 = r.get("p99_ns", value)
        out.append({
            "name": name,
            "unit": r.get("unit", "ns"),
            "value": round(value, 4),
            "range": "± %.2f" % max(0.0, p99 - value),
            "extra": "median %.2f ns, mean %.2f ns"
                     % (r.get("median_ns", value), r.get("mean_ns", value)),
        })

    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2)
        fh.write("\n")
    print("wrote %d metrics to %s" % (len(out), args.output))


if __name__ == "__main__":
    main()
