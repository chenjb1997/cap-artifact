#!/usr/bin/env python3
"""Exercise built programs on tiny inputs; no downloaded data is needed."""
from __future__ import annotations

import argparse
import csv
from fractions import Fraction
import gzip
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UPDATES = """4
+ 0 2 0 1
+ 1 3 0 1 2
+ 2 2 2 3
- 1 3 0 1 2
- 0 2 0 1
+ 0 2 0 1
- 2 2 2 3
- 0 2 0 1
"""
GRAPH = UPDATES.replace("+ 1 3 0 1 2", "+ 1 2 1 2").replace("- 1 3 0 1 2", "- 1 2 1 2")
QUERY_STEPS = [0, 1, 2, 2, 3, 4, 5, 6, 7]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def snapshots(text):
    lines = text.splitlines()
    n, active, states = int(lines[0]), {}, []
    for line in lines[1:]:
        op, ident, size, *vertices = line.split()
        ident, edge = int(ident), frozenset(map(int, vertices))
        require(len(edge) == int(size), "Invalid test fixture")
        if op == "+":
            active[ident] = edge
        else:
            require(active.pop(ident) == edge, "Invalid fixture deletion")
        states.append(list(active.values()))
    optimum = [max(Fraction(sum(edge <= set(vertices) for edge in state), k)
                   for k in range(1, n + 1)
                   for vertices in itertools.combinations(range(n), k)) for state in states]
    return n, states, optimum


def check_witnesses(prefix, exact=False, query_steps=QUERY_STEPS):
    path = Path(str(prefix) + ".witnesses.jsonl.gz")
    if path.exists():
        src = gzip.open(path, "rt", encoding="utf-8")
    else:
        path = Path(str(prefix) + ".witnesses.jsonl")
        src = path.open(encoding="utf-8")
    n, states, optimum = snapshots(UPDATES)
    previous, count = set(), 0
    with src:
        for line in src:
            row = json.loads(line)
            if "vertices" in row:
                require(len(row["vertices"]) == len(set(row["vertices"])), "Duplicate answer vertex")
                previous = set(row["vertices"])
            else:
                removed, added = set(row.get("removed_vertices", [])), set(row.get("added_vertices", []))
                require(removed <= previous, "Delta removes absent vertices")
                require(not (added & (previous - removed)), "Delta adds existing vertices")
                previous = (previous - removed) | added
            require(all(isinstance(v, int) and 0 <= v < n for v in previous), "Answer vertex out of range")
            require(count < len(query_steps) and row["step"] == query_steps[count], "Query schedule mismatch")
            state = [] if row["step"] == -1 else states[row["step"]]
            induced = sum(edge <= previous for edge in state)
            density = Fraction(induced, len(previous)) if previous else Fraction(0)
            for key in ("vertex_count", "denominator"):
                if key in row:
                    require(row[key] == len(previous), "Incorrect answer size")
            for key in ("numerator", "induced_edges"):
                if key in row and row[key] is not None:
                    require(row[key] == induced, "Incorrect induced-edge count")
            if row.get("density") is not None:
                require(math.isclose(float(density), row["density"], abs_tol=1e-10), "Incorrect answer density")
            if exact:
                expected = Fraction(0) if row["step"] == -1 else optimum[row["step"]]
                require(density == expected, "Exact method differs from subset enumeration")
            count += 1
    require(count == len(query_steps), "Missing queries")
    return count


def run_cli(work, name, options, check=1, repeat=1, expected_code=0):
    target = work / name
    command = [sys.executable, str(ROOT / "scripts/run.py"), "--output", str(target),
               "--check", str(check), "--repeat", str(repeat), "--epsilon", "1", "--limit", "90"] + options
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=120)
    if result.returncode != expected_code:
        details = ""
        for filename in ("summary.json", "logs/rep1.stderr.log"):
            path = target / filename
            if path.exists():
                details += path.read_text(errors="replace")[-6000:]
        raise AssertionError(f"{name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}\n{details}")
    summary = json.loads((target / "summary.json").read_text())
    if expected_code == 0:
        require(summary["status"] == "complete", name + ": incorrect overall status")
        for row in summary["runs"]:
            require(row["status"] == "complete", name + ": incomplete run")
            require(row["service_seconds"] is not None and row["service_seconds"] >= 0, name + ": missing service time")
            require(row["timing_eligible"] == (check == 0), name + ": wrong timing eligibility")
        if check:
            require(summary["mean_service_seconds"] is None, "Diagnostic run got a timing mean")
        else:
            mean = sum(r["service_seconds"] for r in summary["runs"]) / repeat
            require(math.isclose(summary["mean_service_seconds"], mean), "Incorrect repetition mean")
    return target, summary


def available(name):
    return (ROOT / "build" / (name + (".exe" if os.name == "nt" else ""))).is_file()


def check_process_semantics(work):
    spec = importlib.util.spec_from_file_location("artifact_runner", ROOT / "scripts/run.py")
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    for code in (7, 124):
        row = runner.run_one([sys.executable, "-c", f"raise SystemExit({code})"], work / f"exit{code}", 5, 0, "cq")
        require(row["status"] == "error" and row["exit_code"] == code, "Exit code was mislabeled as timeout")
        require(not row["timing_eligible"] and row["service_seconds"] is None, "Error got a timing value")
    row = runner.run_one([sys.executable, "-c", "import time; time.sleep(10)"], work / "watchdog", 0.1, 0, "cq")
    require(row["status"] == "timeout" and row["service_seconds"] is None, "Watchdog timeout misclassified")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--all-baselines", action="store_true", help="Also require and test OR-Tools, DI, and ExactFlow")
    p.add_argument("--julia", default=os.environ.get("JULIA", "julia"))
    a = p.parse_args()
    if not available("cap"):
        p.error("Build CAP first: python scripts/build.py")
    if a.all_baselines and importlib.util.find_spec("ortools") is None:
        p.error("--all-baselines needs OR-Tools in this Python environment")
    checked, skipped = [], []
    with tempfile.TemporaryDirectory(prefix="artifact-smoke-") as directory:
        work = Path(directory)
        hyper, graph, queries = work / "tiny.updates", work / "graph.updates", work / "tiny.queries.tsv"
        hyper.write_text(UPDATES, encoding="utf-8")
        graph.write_text(GRAPH, encoding="utf-8")
        queries.write_text("".join(f"{step}\tq{i}\n" for i, step in enumerate(QUERY_STEPS)), encoding="utf-8")
        common = ["--input", str(hyper), "--queries", str(queries), "--rank", "3"]
        cases = []
        for adapter, target in (("compact", "cap"), ("calendar", "cap_calendar"), ("size", "cap_size"), ("ablation", "cap_ablation")):
            if not available(target):
                skipped.append(target)
                continue
            methods = ("cap", "cap-tight", "cap-nf", "cap-unit") if adapter in {"compact", "ablation"} else ("cap",)
            for method in methods:
                cases.append((f"{method}-{adapter}", common + ["--method", method, "--adapter", adapter], False))
        for method in ("udshp", "cq"):
            if available(method):
                cases.append((method, common + ["--method", method], False))
            else:
                skipped.append(method)
        if a.all_baselines:
            for method in ("exact", "di", "exactflow"):
                cases.append((method, common + ["--method", method, "--julia", a.julia], True))
        for name, options, exact in cases:
            target, _ = run_cli(work, name, options)
            count = check_witnesses(target / "rep1", exact)
            checked.append(name)
            print(f"PASS {name}: {count} independently recounted queries", flush=True)
        if available("cap_size"):
            initial_steps = [-1, -1] + QUERY_STEPS
            initial_queries = work / "initial.queries.tsv"
            initial_queries.write_text("".join(f"{step}\tq{i}\n" for i, step in enumerate(initial_steps)), encoding="utf-8")
            target, _ = run_cli(work, "cap-size-initial", ["--method", "cap", "--adapter", "size", "--input", str(hyper), "--rank", "3", "--queries", str(initial_queries)])
            count = check_witnesses(target / "rep1", query_steps=initial_steps)
            with (target / "rep1.witnesses.jsonl").open(encoding="utf-8") as src:
                initial = [json.loads(next(src)), json.loads(next(src))]
            require(all(row["vertices"] == [] and row["numerator"] == 0 and row["denominator"] == 0 for row in initial), "Nonempty initial query answer")
            checked.append("cap-size-initial")
            print(f"PASS cap-size-initial: {count} queries, including two before the first update", flush=True)
        graph_options = ["--input", str(graph), "--rank", "2", "--experiment", "ordinary", "--cadence", "1"]
        for method, executable in (("cap", "cap_graph"), ("improved-dynopt", "improved_dynopt"), ("insdel", "insdel")):
            if not available(executable):
                skipped.append(executable)
                continue
            target, _ = run_cli(work, executable, graph_options + ["--method", method])
            with (target / "rep1.csv").open(newline="") as src:
                require(len(list(csv.DictReader(src))) == 8, "Ordinary output row count mismatch")
            checked.append(executable)
            print(f"PASS {executable}: 8 graph updates", flush=True)
        run_cli(work, "mean", common + ["--method", "cap"], check=0, repeat=3)
        malformed = work / "malformed.updates"
        malformed.write_text("4\n+ 0 5 0 1 2 3 4\n", encoding="utf-8")
        _, failure = run_cli(work, "invalid-input", ["--method", "cap", "--rank", "3", "--input", str(malformed), "--queries", str(queries)], check=0, repeat=2, expected_code=2)
        require(failure["status"] == "error" and failure["runs"][1]["status"] == "cancelled", "Failure/cancellation status mismatch")
        require("wall_seconds" not in failure["runs"][1] and failure["mean_service_seconds"] is None, "Cancelled run acquired a runtime")
        check_process_semantics(work)
        print("PASS mean aggregation, malformed input, cancellation, and watchdog semantics", flush=True)
    print(json.dumps({"status": "pass", "methods_and_adapters": checked, "unbuilt_targets_skipped": skipped}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
