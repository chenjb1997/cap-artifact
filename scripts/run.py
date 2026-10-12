#!/usr/bin/env python3
"""Run one method on a prepared dataset with a whole-process wall-clock limit."""
from __future__ import annotations
import argparse
import csv
from fractions import Fraction
import json
import math
import os
from pathlib import Path
import signal
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
METHODS = ("cap", "cap-tight", "cap-nf", "cap-unit", "udshp", "cq", "exact", "di", "exactflow", "improved-dynopt", "insdel")
POLICIES = {"cap": "CAP", "cap-tight": "Tight", "cap-nf": "NoRefresh", "cap-unit": "Unit"}
RANKS = dict(DW=16, MA=5, AU=5, GE=25, SO=5, TS4=4, TS7=7, TS=25, EM=2, HT=2, WV=2, BC=2, AS=2, SW=2)
GRAPHS = {"EM", "HT", "WV", "BC", "AS", "SW"}

def executable(name: str) -> str:
    path = ROOT / "build" / (name + (".exe" if os.name == "nt" else ""))
    if not path.is_file():
        raise ValueError(f"Missing {path.name}; run python scripts/build.py {name}")
    return str(path)

def command_for(a, prefix: Path) -> tuple[list[str], str]:
    method, dataset = a.method, a.dataset or ""
    eps = Fraction(a.epsilon)
    rank = a.rank or RANKS.get(dataset)
    if rank is None or rank < 2 or not 0 < eps <= 1:
        raise ValueError("Supply rank >= 2 and 0 < epsilon <= 1")
    budget = a.budget or math.ceil(2 * rank / eps)
    if budget < math.ceil(2 * rank / eps):
        raise ValueError("B must be at least ceil(2*rank/epsilon)")
    src, dst, queries = str(a.input), str(prefix), str(a.queries)
    if method in POLICIES:
        adapter = a.adapter
        if adapter == "auto":
            if dataset in GRAPHS or a.experiment == "ordinary": adapter = "graph"
            elif dataset in {"TS", "TS4", "TS7"} or not dataset: adapter = "compact"
            elif a.experiment == "ablation" or method != "cap": adapter = "ablation"
            elif a.experiment == "size": adapter = "size"
            else: adapter = "calendar"
        if adapter == "graph":
            if method != "cap" or rank != 2:
                raise ValueError("The ordinary-graph adapter supports CAP at rank 2")
            return [executable("cap_graph"), src, dst + ".csv", str(eps.numerator), str(eps.denominator), "2", str(budget), str(a.check), "1"], "graph"
        if adapter in {"calendar", "size"} and method != "cap":
            raise ValueError("Use --adapter ablation or compact for CAP variants")
        target = {"compact": "cap", "calendar": "cap_calendar", "size": "cap_size", "ablation": "cap_ablation"}[adapter]
        policy = "1" if adapter in {"calendar", "size"} else POLICIES[method]
        command = [executable(target), src, dst, str(eps.numerator), str(eps.denominator), str(rank), str(budget), policy, str(a.check), queries]
        if adapter == "compact":
            command += ["--query-mode", "full", "--evidence-mode", "delta", "--full-checkpoints", "64"]
        return command, adapter
    if a.check > 1:
        raise ValueError("Baseline adapters support --check 0 or 1")
    if method == "udshp":
        return [executable("udshp"), src, dst, "--epsilon", str(float(eps)), "--queries", queries, "--check", str(a.check), "--dump-assignments", "0", "--checkpoint", "1000", "--budget-seconds", "0"], method
    if method == "cq":
        return [executable("cq"), src, queries, dst, str(float(eps)), str(rank), str(a.check), "0"], method
    if method == "exact":
        return [sys.executable, str(ROOT / "baselines/exact/run.py"), src, dst, "--queries", queries, "--check", str(a.check)], method
    if method in {"di", "exactflow"}:
        native = {"di": "www24_di", "exactflow": "swamp_flow"}[method]
        return [a.julia, "--startup-file=no", "--threads=1", str(ROOT / "baselines/static/run.jl"), src, dst, "--method", native, "--queries", queries, "--check", str(a.check), "--checkpoint", "1000", "--budget-seconds", "0"], method
    if rank != 2:
        raise ValueError("Ordinary-graph baselines require rank 2")
    if method == "improved-dynopt":
        return [executable("improved_dynopt"), src, dst + ".csv", str(a.cadence), "fixed", str(float(eps)), "combined", str(a.check), "-", "1"], method
    return [executable("insdel"), src, dst + ".csv", str(a.cadence), str(float(eps)), "native", str(a.check), "-", "1"], method

def service_seconds(prefix: Path, adapter: str) -> float | None:
    suffix = ".summary.json" if adapter == "udshp" else ".csv.summary.json" if adapter in {"insdel", "improved-dynopt"} else ".stats.json"
    stats_path = Path(str(prefix) + suffix)
    if stats_path.exists():
        d = json.loads(stats_path.read_text())
        if d.get("status", "complete") != "complete":
            raise ValueError("Backend did not complete: " + str(d.get("status")))
        if "service_seconds" in d: return d["service_seconds"]
        if "total_service_ns" in d: return d["total_service_ns"] / 1e9
        u = d.get("update_ns", d.get("service_update_ns"))
        q = d.get("query_ns", d.get("total_query_ns"))
        if u is not None and q is not None: return (u + q) / 1e9
    csv_path = Path(str(prefix) + ".csv")
    if adapter == "graph" and csv_path.exists():
        with csv_path.open(newline="") as src:
            return sum(float(row["update_us"]) for row in csv.DictReader(src)) / 1e6
    return None

def run_one(command: list[str], prefix: Path, limit: float, check: int, adapter: str) -> dict:
    started = time.perf_counter()
    kwargs = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt" else {"start_new_session": True}
    # The Julia adapter rejects every existing PREFIX.* file. Keep supervisor
    # logs separate from the backend output namespace.
    log_dir = prefix.parent / "logs"
    log_dir.mkdir(exist_ok=True)
    with (log_dir / (prefix.name + ".stdout.log")).open("wb") as out, (log_dir / (prefix.name + ".stderr.log")).open("wb") as err:
        process = subprocess.Popen(command, cwd=ROOT, stdout=out, stderr=err, **kwargs)
        timeout = False
        try:
            code = process.wait(timeout=limit if limit > 0 else None)
        except subprocess.TimeoutExpired:
            timeout = True
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if process.poll() is None: process.kill()
            else:
                try: os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError: pass
            code = process.wait()
    elapsed = time.perf_counter() - started
    status = "timeout" if timeout else "complete" if code == 0 else "error"
    service = service_seconds(prefix, adapter) if status == "complete" else None
    if service is not None and (not math.isfinite(service) or service < 0):
        raise ValueError("Invalid backend service time")
    result = dict(status=status, exit_code=code, wall_seconds=elapsed, limit_seconds=limit,
                  adapter=adapter, check=check, timing_eligible=status == "complete" and check == 0 and service is not None,
                  service_seconds=service,
                  command=command)
    Path(str(prefix) + ".run.json").write_text(json.dumps(result, indent=2) + "\n")
    return result

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--method", choices=METHODS, default="cap")
    p.add_argument("--dataset", help="Prepared dataset name, e.g. TS or GE")
    p.add_argument("--input", type=Path)
    p.add_argument("--queries", type=Path)
    p.add_argument("--rank", type=int)
    p.add_argument("--epsilon", default="0.1")
    p.add_argument("--budget", type=int, help="CAP B; default ceil(2*rank/epsilon)")
    p.add_argument("--experiment", choices=("main", "ablation", "size", "ordinary"), default="main")
    p.add_argument("--adapter", choices=("auto", "compact", "calendar", "size", "ablation", "graph"), default="auto")
    p.add_argument("--output", type=Path, required=True, help="Fresh output directory; never overwritten")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--limit", type=float, default=1800, help="Whole-process seconds per repeat; 0 disables")
    p.add_argument("--check", type=int, choices=(0, 1, 2), default=0)
    p.add_argument("--cadence", type=int, default=100, help="Ordinary-baseline query cadence")
    p.add_argument("--julia", default=os.environ.get("JULIA", "julia"))
    a = p.parse_args()
    if a.repeat < 1 or not math.isfinite(a.limit) or a.limit < 0 or a.cadence < 1: p.error("Invalid repetition, limit, or cadence")
    if a.input is None:
        if not a.dataset: p.error("Supply --dataset or --input")
        a.input = ROOT / "data/processed" / (a.dataset + ".updates")
    if a.queries is None:
        a.queries = a.input.with_suffix(".queries.tsv")
    if not a.input.is_file(): p.error("Missing input; download and prepare it first")
    if not a.queries.is_file() and a.method not in {"improved-dynopt", "insdel"} and a.dataset not in GRAPHS and a.experiment != "ordinary":
        p.error("Missing query schedule")
    a.input = a.input.resolve(); a.queries = a.queries.resolve()
    if a.output.exists(): p.error("Output exists; choose a fresh directory")
    # Validate arguments and executable paths before creating any results.
    try: command_for(a, a.output.resolve() / "rep1")
    except ValueError as exc: p.error(str(exc))
    a.output.mkdir(parents=True)
    runs = []
    for rep in range(1, a.repeat + 1):
        prefix = a.output.resolve() / f"rep{rep}"
        command, adapter = command_for(a, prefix)
        print(f"{a.method}: repetition {rep}/{a.repeat}", flush=True)
        try: result = run_one(command, prefix, a.limit, a.check, adapter)
        except (OSError, ValueError) as exc:
            result = {"status": "error", "error": str(exc), "timing_eligible": False, "service_seconds": None}
            Path(str(prefix) + ".run.json").write_text(json.dumps(result, indent=2) + "\n")
        runs.append(result)
        if result["status"] != "complete":
            runs += [{"status": "cancelled", "reason": "earlier repetition timed out" if result["status"] == "timeout" else "earlier repetition failed"} for _ in range(rep, a.repeat)]
            break
    eligible = all(r.get("timing_eligible") and r.get("service_seconds") is not None for r in runs)
    status = next((r["status"] for r in runs if r["status"] not in {"complete", "cancelled"}), "complete")
    summary = dict(status=status, method=a.method, dataset=a.dataset, epsilon=a.epsilon, repetitions=a.repeat, runs=runs,
                   mean_service_seconds=statistics.mean(r["service_seconds"] for r in runs) if eligible else None,
                   note="Service times require independent answer validation before scientific use. Cancelled repetitions have no runtime; check>0 is diagnostic.")
    (a.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({"status": status, "mean_service_seconds": summary["mean_service_seconds"], "summary": str(a.output / "summary.json")}, indent=2))
    return 2 if status == "error" else 124 if status == "timeout" else 0

if __name__ == "__main__":
    sys.exit(main())
