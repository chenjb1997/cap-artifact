"""Calendar snapshots through the unchanged WWW22 original LP function.

INPUT: n, then canonical '+/- ID k v1 ... vk' rows. Queries: optional
'step<TAB>label' header, zero-based post-update steps, nondecreasing, duplicates
allowed. Only unweighted physical occurrences are accepted by this adapter.
"""
from __future__ import annotations

import argparse
import contextlib
import csv
import hashlib
import gzip
import importlib.util
import json
import math
import platform
import sys
import time
import traceback
from pathlib import Path

sys.dont_write_bytecode = True
SOURCE = Path(__file__).resolve().parent / "upstream/exactLPSolver.py"
SOURCE_SHA = "594bad79672fd230b9358d2b18cb987cadbd12eccb0a63925d5adde1985343a4"
SUFFIXES = (".csv.gz", ".witnesses.jsonl.gz", ".stats.json", ".author.output")


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def preload(path: Path) -> tuple[int, list[tuple[str, int, tuple[int, ...]]]]:
    with path.open(encoding="utf-8") as src:
        first = src.readline().split()
        if len(first) != 1 or not first[0].isdigit() or int(first[0]) < 1:
            raise ValueError("Expected one positive n header")
        n = int(first[0])
        events, active = [], {}
        for line_no, line in enumerate(src, 2):
            fields = line.split()
            if not fields:
                continue
            if len(fields) < 4 or fields[0] not in ("+", "-"):
                raise ValueError(f"Invalid event at line {line_no}")
            op = fields[0]
            ident, k, *vertices = map(int, fields[1:])
            edge = tuple(vertices)
            if ident < 0 or k < 1 or len(edge) != k or any(v < 0 or v >= n for v in edge) or tuple(sorted(set(edge))) != edge:
                raise ValueError(f"Noncanonical event at line {line_no}")
            if op == "+":
                if ident in active:
                    raise ValueError(f"Active ID reused at line {line_no}")
                active[ident] = edge
            else:
                if active.get(ident) != edge:
                    raise ValueError(f"Invalid occurrence-ID deletion at line {line_no}")
                del active[ident]
            events.append((op, ident, edge))
    return n, events


def calendar(path: Path, count: int) -> list[tuple[int, str]]:
    reports = []
    with path.open(encoding="utf-8") as src:
        for line in src:
            line = line.rstrip("\r\n")
            if not line:
                continue
            if not reports and line == "step\tlabel":
                continue
            step_text, sep, label = line.partition("\t")
            if not sep or not step_text.isascii() or not step_text.isdigit():
                raise ValueError("Queries must be step<TAB>label with nonnegative step")
            step = int(step_text)
            if step >= count or (reports and step < reports[-1][0]):
                raise ValueError("Query steps must be nondecreasing and within the stream")
            reports.append((step, label))
    return reports


def validate_answer(answer: dict, active: dict, n: int) -> None:
    selected = answer["vertices"]
    if len(selected) != len(set(selected)) or any(not isinstance(v, int) or not 0 <= v < n for v in selected):
        raise AssertionError("Invalid original witness membership")
    members = set(selected)
    count = sum(set(edge).issubset(members) for edge in active.values())
    density = count / len(selected) if selected else 0.0
    if count != answer["numerator"] or len(selected) != answer["denominator"] or density != answer["density"]:
        raise AssertionError("Independent witness recount disagrees")
    if active and not selected:
        raise AssertionError("Original LP returned empty set for a positive nonempty graph")
    if not math.isfinite(answer["solver_objective"]) or not math.isclose(density, answer["solver_objective"], rel_tol=1e-9, abs_tol=1e-7):
        raise AssertionError("Original objective and returned-set density disagree")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--queries", required=True, type=Path)
    parser.add_argument("--check", type=int, choices=(0, 1), default=0)
    parser.add_argument("--budget-seconds", type=float, default=0, help="Batch wall budget; 0 unlimited; checked only between completed operations.")
    parser.add_argument("--max-updates", "--stop-after-updates", type=int, default=None, help="Optional deterministic update-prefix budget.")
    parser.add_argument("--max-queries", type=int, default=None, help="Optional deterministic query-prefix budget.")
    args = parser.parse_args()
    if not math.isfinite(args.budget_seconds) or args.budget_seconds < 0 or any(x is not None and x < 0 for x in (args.max_updates, args.max_queries)):
        parser.error("Budgets must be finite and nonnegative")
    prefix = args.prefix.resolve()
    if any(Path(str(prefix) + suffix).exists() for suffix in SUFFIXES):
        parser.error("Output prefix already exists")
    prefix.parent.mkdir(parents=True, exist_ok=True)
    before = sha(SOURCE)
    if before != SOURCE_SHA:
        raise RuntimeError("Original LP source hash changed")
    parse_start = time.perf_counter_ns()
    n, events = preload(args.input)
    reports = calendar(args.queries, len(events))
    parse_ns = time.perf_counter_ns() - parse_start
    initialize = time.perf_counter_ns()
    import ortools
    from ortools.linear_solver import pywraplp

    spec = importlib.util.spec_from_file_location("www22_lp_original_calendar", SOURCE)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    solver_version = pywraplp.Solver("VersionOnly", pywraplp.Solver.GLOP_LINEAR_PROGRAMMING).SolverVersion()
    active: dict[int, tuple[int, ...]] = {}
    completed_updates = completed_queries = 0
    last_step = None
    previous_vertices = None
    initialization_ns = time.perf_counter_ns() - initialize
    total_update = total_query = check_ns = recount_ns = 0
    next_report = 0
    status, reason, error, failed_query_ns = "complete", None, None, 0
    batch_start = time.perf_counter_ns()

    def wall_exhausted() -> bool:
        return args.budget_seconds > 0 and time.perf_counter_ns() - batch_start >= args.budget_seconds * 1e9

    fields = ("step", "op", "update_ns", "query_ns", "total_service_ns", "edges", "queried", "query_count", "density", "upper", "witness_size", "solver_objective")
    with Path(str(prefix) + ".author.output").open("w", encoding="utf-8") as author_output, gzip.open(str(prefix)+".csv.gz", "wt", compresslevel=1, newline="", encoding="utf-8") as update_output, gzip.open(str(prefix)+".witnesses.jsonl.gz", "wt", compresslevel=1, encoding="utf-8") as witness_output:
        writer = csv.DictWriter(update_output, fieldnames=fields)
        writer.writeheader()
        try:
            for step, (op, ident, edge) in enumerate(events):
                if wall_exhausted() or (args.max_updates is not None and completed_updates >= args.max_updates):
                    status, reason = "budget_exceeded", "wall_before_update" if wall_exhausted() else "max_updates"
                    break
                update_start = time.perf_counter_ns()
                if op == "+":
                    active[ident] = edge
                else:
                    del active[ident]
                update_ns = time.perf_counter_ns() - update_start
                total_update += update_ns
                row = dict(step=step, op=op, update_ns=update_ns, query_ns=0, total_service_ns=update_ns, edges=len(active), queried=0, query_count=0, density=None, upper=None, witness_size=None, solver_objective=None)
                completed_updates += 1
                last_step = step
                while next_report < len(reports) and reports[next_report][0] == step:
                    if wall_exhausted() or (args.max_queries is not None and completed_queries >= args.max_queries):
                        status, reason = "budget_exceeded", "wall_before_query" if wall_exhausted() else "max_queries"
                        break
                    label = reports[next_report][1]
                    author_output.write(f"=== BEGIN QUERY {next_report} step={step} label={json.dumps(label, ensure_ascii=False)} ===\n")
                    author_output.flush()
                    query_start = time.perf_counter_ns()
                    try:
                        # Physical occurrence insertion order is preserved. Every
                        # query reconstructs the whole input and native LP model.
                        edge_list = [list(vertices) for vertices in active.values()]
                        weights = [1] * len(edge_list)
                        with contextlib.redirect_stdout(author_output), contextlib.redirect_stderr(author_output):
                            native_vertices, objective = module.densestSubgraph(edge_list, weights)
                        vertices = list(native_vertices)  # no sorting/filtering
                        denominator = len(vertices)
                        answer = dict(step=step, label=label, numerator=None, denominator=denominator, density=None, solver_objective=float(objective), upper=None, vertices=vertices)
                        completed_queries += 1
                        author_output.flush()
                        one_query_ns = time.perf_counter_ns() - query_start
                    except Exception:
                        failed_query_ns = time.perf_counter_ns() - query_start
                        raise
                    answer["query_ns"] = one_query_ns
                    # Independent recount is optional and always excluded from service.
                    # check=0 exports native objective and raw set; density stays null.
                    one_recount_ns = 0
                    if args.check:
                        recount_start = time.perf_counter_ns()
                        members = set(vertices)
                        numerator = sum(all(v in members for v in vertices_e) for vertices_e in edge_list)
                        answer["numerator"] = numerator
                        answer["density"] = numerator / denominator if denominator else 0.0
                        one_recount_ns = time.perf_counter_ns() - recount_start
                    answer["recount_ns_excluded"] = one_recount_ns
                    answer["validation_ns_excluded"] = 0
                    recount_ns += one_recount_ns
                    row.update(query_ns=row["query_ns"] + one_query_ns, queried=1, query_count=row["query_count"] + 1, density=answer["density"], witness_size=denominator, solver_objective=answer["solver_objective"])
                    row["total_service_ns"] = row["update_ns"] + row["query_ns"]
                    total_query += one_query_ns
                    next_report += 1
                    author_output.write(f"=== END QUERY {next_report - 1} ===\n")
                    author_output.flush()
                    if args.check:
                        check_start = time.perf_counter_ns()
                        validate_answer(answer, active, n)
                        one_check_ns = time.perf_counter_ns() - check_start
                        answer["validation_ns_excluded"] = one_check_ns
                        check_ns += one_check_ns
                    current_vertices = set(answer["vertices"])
                    if len(current_vertices) != len(answer["vertices"]):
                        raise AssertionError("Duplicate vertex in returned set")
                    evidence = {k: v for k, v in answer.items() if k != "vertices"}
                    evidence.update(encoding="set-delta-v1", vertex_count=len(current_vertices))
                    if previous_vertices is None:
                        evidence["vertices"] = sorted(current_vertices)
                    else:
                        evidence["added_vertices"] = sorted(current_vertices - previous_vertices)
                        evidence["removed_vertices"] = sorted(previous_vertices - current_vertices)
                    previous_vertices = current_vertices
                    witness_output.write(json.dumps(evidence, ensure_ascii=False, allow_nan=False)+"\n")
                    witness_output.flush()
                writer.writerow(row)
                if (step+1) % 1000 == 0:
                    update_output.flush()
                if status != "complete":
                    break
        except Exception:
            status, reason, error = "error", "native_or_validation_error", traceback.format_exc()
            author_output.write(error)
            author_output.flush()
    batch_ns = time.perf_counter_ns() - batch_start
    after = sha(SOURCE)
    if after != before:
        status, reason = "error", "source_hash_changed"
    stats = dict(status=status, stop_reason=reason, error=error, algorithm="WWW22 original GLOP snapshot LP", updates=completed_updates, queries=completed_queries, planned_updates=len(events), planned_queries=len(reports), last_step=last_step, next_query_index=next_report, next_query={"step": reports[next_report][0], "label": reports[next_report][1]} if next_report < len(reports) else None, input=str(args.input.resolve()), input_sha256=sha(args.input), queries_path=str(args.queries.resolve()), queries_sha256=sha(args.queries), source=str(SOURCE), source_sha256_before=before, source_sha256_after=after, source_unchanged=before == after, adapter_sha256=sha(Path(__file__)), python_version=sys.version, python_executable=sys.executable, platform=platform.platform(), ortools_version=ortools.__version__, solver_version=solver_version, check_level=args.check, timing_eligible=args.check == 0 and status == "complete", parse_ns=parse_ns, initialization_ns=initialization_ns, update_ns=total_update, bookkeeping_ns=total_update, query_ns=total_query, total_service_ns=total_update + total_query, verification_ns=check_ns, validation_ns_excluded=check_ns, recount_ns_excluded=recount_ns, failed_query_ns=failed_query_ns, batch_ns=batch_ns, budget_seconds=args.budget_seconds, max_updates=args.max_updates, max_queries=args.max_queries, native_solver_settings="Unchanged GLOP constructor/default parameters; original strict x>0 support selection.", author_prints_preserved=True, auto_tail_query=False, active_order="Physical occurrence insertion order; delete removes its ID; reinsert appends.", upper_semantics="null: numerical LP objective is separately reported, not a certified upper bound.", density_semantics="check=0: null pending independent result validation; check=1: independently recounted. Native solver_objective is neither recounted density nor certified upper.", witness_order="Native returned set unchanged; sidecar encodes sorted set deltas outside query timer.", update_scope="External active-ID dictionary insertion/deletion only; no LP work or hidden maintained solver state.", query_scope="Full edgeList and unit-weight construction, original LP model construction and Solve, original Python prints and log flush, returned-set copy, native objective and construction of one answer record. Membership-set construction and induced-edge recount are excluded. No warm start, cache, or solver reuse.", batch_scope="Includes loop instrumentation, markers, optional excluded recount/validation and streaming gzip IO; excludes input prevalidation/imports/initialization.", verification_scope="Optional independent membership/recount/objective consistency after query clock; exact subset oracle is only in external tiny checker.", budget_scope="Checked before each update and query; a running original solve is never interrupted. Prefix may end between labels at one step.", memory_scope="Preloaded events, active IDs, native LP, one current answer and streaming gzip sidecars; no algorithm-only memory measurement.")
    Path(str(prefix) + ".stats.json").write_text(json.dumps(stats, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"status": status, "updates": completed_updates, "queries": completed_queries, "prefix": str(prefix)}))
    return 0 if status == "complete" else 124 if status == "budget_exceeded" else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception:
        traceback.print_exc()
        raise SystemExit(2)
