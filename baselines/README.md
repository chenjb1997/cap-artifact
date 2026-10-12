# Baselines

This directory contains the baseline implementations and input adapters used by
the experiments. All commands below run from the repository root. Build the C++
targets with the root build instructions before running them. The Python and
Julia methods do not require a C++ build.

| Method | Entry point | Requirements |
|---|---|---|
| Udshp | `build/udshp` | C++20, zlib |
| CQ | `build/cq` | C++20, zlib |
| Exact | `baselines/exact/run.py` | Python 3.10+, OR-Tools |
| DI | `baselines/static/run.jl --method www24_di` | Julia 1.9+ |
| ExactFlow | `baselines/static/run.jl --method swamp_flow` | Julia 1.9+ |
| ImprovedDynOpt | `build/improved_dynopt` | C++20 |
| INS/DEL | `build/insdel` | C++20 |

On Windows, add `.exe` to the C++ executable names. The build descriptions are
in `build_targets.json` and `CMakeLists.txt`. GCC or Clang is required: the
ordinary-graph adapters use `__int128` for exact certificate comparisons.

## Input and output

Each input starts with the number of vertices. Every following line is an
insertion or deletion in the form `+ ID k v1 ... vk` or `- ID k v1 ... vk`.
Vertices are zero-based, sorted, and distinct within each hyperedge. A deletion
must name an active ID and the same vertices as its insertion. For example:

```text
4
+ 0 2 0 1
+ 1 3 0 1 2
+ 2 2 2 3
- 1 3 0 1 2
```

For hypergraph methods, the query file contains `step<TAB>label`, without a
header. A zero-based step identifies the update after which to query. Steps
must be nondecreasing; multiple queries after the same update are allowed.
Use labels without spaces for compatibility with every adapter.

```text
1	q0
3	q1
```

Create the output directory first and choose a fresh prefix for every run.
The hypergraph adapters write timing summaries, progress, and compressed
returned-set evidence under that prefix. The `set-delta-v1` evidence format
stores the first complete vertex set and then added/removed vertex IDs.
`--check 1` enables expensive validation for small examples; use `--check 0`
for timing. Do not compare validation runs with timing runs.

## Hypergraph methods

The following examples assume the input and schedule have been prepared as
`data/example.updates` and `data/example.queries.tsv`.

```bash
mkdir -p results

build/udshp data/example.updates results/udshp \
  --epsilon 0.1 --queries data/example.queries.tsv \
  --check 0 --dump-assignments 0 --checkpoint 1000 --budget-seconds 0

# Positional arguments: input, queries, prefix, epsilon, rank, check, copies override.
build/cq data/example.updates data/example.queries.tsv results/cq 0.1 25 0 0

python -m pip install ortools
python baselines/exact/run.py data/example.updates results/exact \
  --queries data/example.queries.tsv --check 0 --budget-seconds 0

julia --startup-file=no --threads=1 baselines/static/run.jl \
  data/example.updates results/di --method www24_di \
  --queries data/example.queries.tsv --check 0 --checkpoint 1000 --budget-seconds 0

julia --startup-file=no --threads=1 baselines/static/run.jl \
  data/example.updates results/exactflow --method swamp_flow \
  --queries data/example.queries.tsv --check 0 --checkpoint 1000 --budget-seconds 0
```

Set CQ's rank argument to the maximum hyperedge size of the prepared input.
Its last argument must be `0` to use the prescribed replication count; a
positive value is a diagnostic override, not the experimental configuration.
Udshp preserves the upstream replication and `eta=1` behavior. Its admission
check rejects inputs whose replicated IDs or native integer arithmetic would
overflow, instead of silently changing the algorithm.

DI and ExactFlow use only the Julia standard libraries and the zlib library
bundled with Julia. Their adapters perform a small warmup before recording the
workload. Exact invokes the bundled upstream LP function with OR-Tools GLOP.
It solves each requested snapshot from scratch.

The adapter `--budget-seconds` option is checked between operations and cannot
interrupt an operation already running. Use the repository's experiment runner
for a hard whole-process timeout. A timeout has no completed-run mean; partial
output is evidence of a prefix only.

## Ordinary-graph methods

These use the same update format with `k=2` on every row. Their query service
uses a cadence rather than a separate schedule. The experimental settings are:

```bash
# input, output CSV, cadence, variant, epsilon, query service, check, dump, gate
build/improved_dynopt data/graph.updates results/improved.csv \
  100 fixed 0.1 combined 0 - 1

# input, output CSV, cadence, epsilon, query service, check, dump, gate
build/insdel data/graph.updates results/insdel.csv \
  100 0.1 native 0 - 1
```

ImprovedDynOpt's `fixed` variant includes the documented maximum-count boundary
correction; `improved` is included for inspection of the original behavior.
INS/DEL uses its native query candidate. Both adapters include the shared
certificate-gated query service used by the ordinary-graph comparison. These
scalar-query results are separate from hypergraph full-set-query results.

See [PROVENANCE.md](PROVENANCE.md) for upstream revisions, adapter changes, and
the licenses applying to each component.
