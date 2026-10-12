#!/usr/bin/env python3
"""Print or execute the paper experiment commands on prepared data."""
from __future__ import annotations
import argparse
from fractions import Fraction
from pathlib import Path
import subprocess
import sys
from data_catalog import HYPER, GRAPHS, PROCESSED

ROOT = Path(__file__).resolve().parents[1]

def configurations(suite, datasets):
    for dataset in datasets:
        if suite == "main":
            for eps in ("1", ".7", ".5", ".2", ".1"):
                for method in ("cap", "cq", "udshp"):
                    yield dataset, method, eps, "main", []
            for method in ("exact", "di", "exactflow"):
                yield dataset, method, ".1", "main", []
        elif suite == "ablation":
            for method in ("cap", "cap-tight", "cap-nf", "cap-unit"):
                yield dataset, method, ".1", "ablation", []
        elif suite == "ordinary":
            for method in ("cap", "improved-dynopt", "insdel"):
                yield dataset, method, ".1", "ordinary", []
        elif suite == "size":
            for percent in (20, 40, 60, 80, 100):
                name = dataset if percent == 100 else f"{dataset}_p{percent}"
                yield dataset, "cap", ".1", f"size_p{percent}", ["--input", str(PROCESSED / (name + ".updates")), "--queries", str(PROCESSED / (name + ".queries.tsv"))]
        elif suite == "query-frequency":
            for factor in (1, 4, 16):
                for method in ("cap", "cq", "udshp", "exact", "di", "exactflow"):
                    yield dataset, method, ".5", f"query_q{factor}", ["--queries", str(PROCESSED / f"{dataset}_q{factor}.queries.tsv")]
        elif suite == "accuracy":
            for eps in ("1", ".7", ".5", ".2", ".1", ".08", ".06", ".04", ".02", ".01"):
                for method in ("cap", "cq", "udshp"):
                    yield dataset, method, eps, "accuracy", []
            for method in ("exact", "di", "exactflow"):
                yield dataset, method, ".1", "accuracy", []

def main():
    allowed = {"main": HYPER, "ablation": HYPER, "ordinary": GRAPHS,
               "size": ("GE", "SO", "TS"), "query-frequency": ("DW", "GE"), "accuracy": ("GE",)}
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--suite", choices=allowed, required=True)
    p.add_argument("--datasets", nargs="+")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--repeat", type=int, default=3)
    p.add_argument("--limit", type=float, default=1800)
    p.add_argument("--julia", default=None)
    p.add_argument("--execute", action="store_true", help="Without this flag, only print commands")
    a = p.parse_args()
    datasets = a.datasets or allowed[a.suite]
    if set(datasets) - set(allowed[a.suite]): p.error("Dataset is not part of this suite")
    for dataset, method, eps, label, extra in configurations(a.suite, datasets):
        rational = Fraction(eps)
        name = f"{dataset}_{method}_eps{rational.numerator}of{rational.denominator}_{label}"
        experiment = "size" if a.suite == "size" else "ablation" if a.suite == "ablation" else "ordinary" if a.suite == "ordinary" else "main"
        command = [sys.executable, str(ROOT / "scripts/run.py"), "--dataset", dataset,
                   "--method", method, "--epsilon", eps, "--experiment", experiment,
                   "--repeat", str(a.repeat), "--limit", str(a.limit), "--output", str(a.output / name)] + extra
        if a.julia: command += ["--julia", a.julia]
        print(subprocess.list2cmdline(command), flush=True)
        if a.execute:
            result = subprocess.run(command, cwd=ROOT)
            # A true timeout skips only repetitions of that configuration.
            # Errors stop the suite; they must not silently become timeouts.
            if result.returncode not in (0, 124): return result.returncode
    return 0

if __name__ == "__main__":
    sys.exit(main())
