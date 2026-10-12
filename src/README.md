# CAP implementations

These drivers use the CAP kernels and timer boundaries associated with the
corresponding experiments. Build them with the repository's build command;
`build_targets.json` lists the translation units and libraries. GCC or Clang
with C++20 and `__int128` support is required. The compact driver also uses zlib.
`platform.hpp` provides process-memory telemetry and atomic progress-file
replacement on supported systems.

| Executable | Role | Result files |
|---|---|---|
| `cap_calendar` | Main hypergraph comparison, query-frequency and accuracy experiments on DW, MA, AU, GE and SO | `.csv`, `.stats.json`, `.witnesses.jsonl` |
| `cap_size` | GE and SO vertex-induced samples, including queries before the first sampled update | Same as `cap_calendar` |
| `cap_ablation` | CAP, CAP-Tight, CAP-NF and CAP-Unit on DW, MA, AU, GE and SO | `.stats.json`, `.progress.json`, `.witnesses.jsonl` |
| `cap` | TS4, TS7 and TS comparisons and ablations; TS vertex-induced samples | `.stats.json`, `.progress.json`, `.witnesses.jsonl.gz`; checked runs also produce `.certificates.jsonl` |
| `cap_graph` | Ordinary-graph comparison | One per-update CSV |

`cap_hyper_kernel.cpp` is included by the two calendar drivers rather than
built separately. `mechanism_kernel.hpp` is shared by the ablation and compact
drivers. The two observer headers intentionally differ: `ablation_observer.hpp`
supports the calendar ablation checks, while `independent_observer.hpp` supports
compact query evidence and fixed full-state checkpoints.

## Command-line interfaces

```text
cap_calendar UPDATES PREFIX ep eq rank B mode check QUERIES.tsv
cap_size     UPDATES PREFIX ep eq rank B mode check QUERIES.tsv
cap_ablation UPDATES PREFIX ep eq rank B POLICY check QUERIES.tsv
cap          UPDATES PREFIX ep eq rank B POLICY check QUERIES.tsv
cap_graph    UPDATES OUTPUT.csv ep eq rank B check mode
```

The accuracy parameter is `epsilon = ep / eq`, with `0 < epsilon <= 1`.
Use `B = ceil(2 * rank / epsilon)` and `mode = 1` for CAP. For the ordinary
graph experiment, use `ep=1`, `eq=10`, `rank=2`, `B=40`, `mode=1`.

The ablation drivers accept `CAP`, `Tight`, `NoRefresh`, and `Unit` as `POLICY`,
corresponding to CAP, CAP-Tight, CAP-NF, and CAP-Unit in the paper. All four
policies remain pilot guided. Tight removes capacity slack and reserve
destinations; NoRefresh disables budgeted peeling; Unit transfers one integer
allocation unit along each successful repair path. The historical `NoReserve`
policy is also implemented but is not part of the reported ablation.

The compact driver defaults to full-set queries and lossless delta evidence.
Optional flags are:

```text
--query-mode full|size
--evidence-mode delta|full
--full-checkpoints 64
--stop-after-updates N
```

The `size` option is a separate maintained-size interface; use `full` for
the reported comparisons. A prefix stop is accepted only in checked runs.
`cap_ablation` also accepts `--stop-after-updates N` in checked runs.

## Checks and timing

Use `check=0` for timing. For `cap` and `cap_ablation`, `check=1` enables an
independent observer after every update. `check=2` additionally enumerates
vertex subsets after every update and is restricted to toy inputs with at
most 16 vertices. Checked executions are validation runs, not timing samples.

For the calendar drivers, `check=1` runs the kernel's consistency check at
query points and `check=2` runs it after every update. These are internal
checks, not the separate observer. For `cap_graph`, `check=1` performs the
internal consistency check after every update.

Hypergraph service runtime is `(update_ns + query_ns) / 1e9` seconds. Full-set
query timing includes materializing the returned vertex vector. Input parsing,
initialization, answer serialization, compression and independent observation
are outside these service timers. Wall-clock time is a separate resource-limit
measurement and must not replace a completed service runtime.

For `cap_graph`, sum `update_us` over the complete CSV and divide by `1e6`.
This includes the initial insertions. Scalar reads and vertex enumeration are
untimed in this ordinary-graph protocol. It is distinct from the materialized
full-set hypergraph protocol.

Output prefixes must be new. Parent directories must already exist. The
calendar and compact drivers reject existing result files rather than
overwriting them. A failed or stopped process is not a completed sample.

## Update and query files

An update file starts with the vertex count, followed by one operation per
line:

```text
6
+ 0 2 0 1
+ 1 3 0 1 2
- 0 2 0 1
+ 0 2 0 1
```

Each operation is `+/- edge_id edge_size sorted_vertices...`. Vertex IDs are
zero based. Endpoint lists contain distinct vertices. An insertion must use
an inactive edge ID; a deletion must match that active ID and endpoint list.
An ID may be reused after deletion. The preprocessing scripts enforce the
simple-hypergraph policy of excluding singleton and duplicate active edges.

Query TSV files have no header. Each row is `step<TAB>label`; `step` is a
zero-based update index, and the query runs after that update. Steps are
nondecreasing, and repeated steps represent separate queries. Only `cap_size`
also accepts `-1` for the initial empty state. No driver adds a final query
implicitly.
