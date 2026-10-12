# Data preparation

Large datasets are downloaded from their public providers instead of being
included in this repository. All commands below are run from the repository
root with Python 3.10 or newer; these scripts use only the standard library.
Preparation does not run a densest-subgraph algorithm.

## Quick start

The included `toy.updates` and `toy.queries.tsv` are a small synthetic example
with deletions, reinsertion of an edge, and two queries at the same update.
Check the format and the offline preprocessing fixtures with:

```bash
python scripts/data_validate.py data/toy.updates data/toy.queries.tsv
python scripts/data_selftest.py
```

Download and prepare a small main-experiment input first:

```bash
python scripts/prepare_data.py --datasets DW --download
```

The output is `data/processed/DW.updates`, `DW.queries.tsv`, and `DW.json`.
The JSON contains the vertex count, rank parameter, update/query counts,
preprocessing policy and output checksums. No output is overwritten: use a
fresh `--output-dir` when preparing a second copy.

After the DW example, download and process the remaining inputs with:

```bash
python scripts/prepare_data.py --datasets MA AU GE SO TS4 TS7 TS --download
python scripts/prepare_data.py --datasets graphs --download
```

On a fresh output directory, `--datasets hyper --download` prepares all eight
hypergraph inputs at once. `hyper`, `graphs`, and `all` expand to the labels in
the tables below. Requested output names are checked before downloads or
input conversion begin; existing final and partial outputs are protected. The
download command verifies each archive against the artifact's input checksum.
It handles the public Google Drive confirmation page used by the Cornell
collection. If the provider requires a manual download, use the source page
below and place the archive under `data/raw/` with the filename specified in
`scripts/data_catalog.py`. The preparation command verifies existing archives
and the extracted members before processing. A changed archive causes an
error rather than silently changing the input.

`--raw-dir` and `--output-dir` can select another disk. Full SO and thread
preparation can take substantial time and disk space: the archive is much
smaller than the uncompressed input, temporary SQLite tables and resulting
update files. Allow tens of GB of free disk space for the complete collection.
The thread generator uses disk-based exact deduplication and sorting; it does
not construct a Python object for every edge at once. Do not include data
preparation in algorithm timings.

## Hypergraph inputs

| Label | Public source | Preprocessing and updates |
|---|---|---|
| DW | [DAWN](https://www.cs.cornell.edu/~arb/data/DAWN/) | Native quarter labels, one-quarter window. |
| MA | [Math Stack Exchange tags](https://www.cs.cornell.edu/~arb/data/tags-math-sx/) | Divide relative millisecond timestamps by 90 days, one-bin window. |
| AU | [Ask Ubuntu tags](https://www.cs.cornell.edu/~arb/data/tags-ask-ubuntu/) | Same time conversion and window as MA. |
| GE | [MAG Geology coauthorship](https://www.cs.cornell.edu/~arb/data/coauth-MAG-Geology/) | Provider's rank-at-most-25 archive, publication-year timestamps, ten-year window. |
| SO | [Stack Overflow tags](https://www.cs.cornell.edu/~arb/data/tags-stack-overflow/) | Same time conversion and window as MA. |
| TS4 / TS7 / TS | [Stack Overflow thread participants](https://www.cs.cornell.edu/~arb/data/threads-stack-overflow/) | Whole edges of rank 2–4 / 2–7 / 2–25, chronological insertion, then deletion and reinsertion of one quarter of distinct edges. |

DW, MA, AU, GE and SO use stable timestamp order. Their calendar conversion
discards singleton edges and bins with labels at most 1. Vertex IDs follow
the two first-appearance mappings used by the calendar conversion. An edge
already active is ignored; its repeated arrival does **not** refresh its
expiry. At a calendar boundary, a query is made before the oldest admitted
bucket expires. GE starts annual reports after its first complete ten-year
window. Empty calendar bins still produce queries, and the final observed
bin is queried without draining all remaining edges.

The TS variants use the provider's rank-at-most-25 archive. TS adds no rank
cutoff within that archive; TS4 and TS7 select whole lower-rank edges, without
clipping any endpoint set. Singleton edges are removed, and equal endpoint
sets are represented by their earliest `(timestamp, original row)` occurrence.
Incident raw vertex IDs are sorted and mapped to contiguous integers. All
distinct edges are inserted chronologically. Exactly `floor(m/4)` edges with
the smallest `(splitmix64(original_row XOR seed), original_row)` priorities
are then deleted and reinserted with the same IDs and in the same order.
The fixed seed is in `data_catalog.py`. These two phases are synthetic update
schedules, not observed deletions. Queries occur every 1,000 updates and at
each phase end; coincident positions are merged.

The prepared main inputs have the following counts. The command checks these
counts before marking a full public input as successfully prepared.

| Input | Vertices | Peak active edges | Updates | Queries | Rank parameter |
|---|---:|---:|---:|---:|---:|
| DW | 2,290 | 11,544 | 529,412 | 32 | 16 |
| MA | 1,627 | 21,349 | 611,274 | 29 | 5 |
| AU | 3,021 | 10,458 | 382,007 | 33 | 5 |
| GE | 1,087,111 | 409,152 | 1,471,530 | 210 | 25 |
| SO | 49,918 | 351,521 | 15,897,567 | 35 | 5 |
| TS4 | 2,260,330 | 8,100,825 | 12,151,237 | 12,154 | 4 |
| TS7 | 2,317,462 | 8,558,567 | 12,837,849 | 12,840 | 7 |
| TS | 2,321,751 | 8,589,420 | 12,884,130 | 12,887 | 25 |

## Ordinary graph inputs

| Label | Public source | Update generation |
|---|---|---|
| EM | [Email Eu-core](https://snap.stanford.edu/data/email-Eu-core.html) | Undirected simple topology; fixed-seed random toggles. |
| HT | [HepTh collaboration](https://snap.stanford.edu/data/ca-HepTh.html) | Same toggle construction as EM. |
| WV | [WikiVote](https://snap.stanford.edu/data/wiki-Vote.html) | Same toggle construction as EM. |
| BC | [Bitcoin OTC](https://snap.stanford.edu/data/soc-sign-bitcoin-otc.html) | Chronological ratings with synthetic 90-day expiry. |
| AS | [CAIDA AS snapshots](https://snap.stanford.edu/data/as-Caida.html), [dynamic data distribution](https://dyngraphlab.github.io/) | Observed snapshot changes in stable timestamp/file order. |
| SW | [Simple Wikipedia dynamic links](https://dyngraphlab.github.io/) | Observed insertions/deletions in stable timestamp/file order. |

EM, HT and WV discard self-loops, ignore directions and deduplicate edges.
With seed 0, half of a shuffled edge list is inserted, followed by 20,000
random edge toggles. The initialization updates are part of the reported
total service time; the generated metadata sets `warmup` to zero.

BC ignores rating signs and directions and uses reference counts for repeated
ratings. An old rating expires when its timestamp is strictly less than
`current_timestamp - 90 days`; an edge exists exactly when its count is
positive. The final input is not drained.

AS and SW reference-count directed occurrences and emit a single undirected
edge only on zero-to-positive and positive-to-zero transitions. They introduce
no synthetic deletions. SW excludes every event of any unordered pair with a
negative directed count in chronological order, rather than dropping only
unmatched deletions. The excluded pairs are written to
`SW.excluded_pairs.json`. All original observed vertex IDs enter the sorted
vertex map, including vertices whose events are excluded by this cleaning.
The ordinary inputs provide a query every 100 updates plus the final update.
Their paper service is scalar-query service; using a full-set query driver
changes the measurement contract.

## Size and query-frequency inputs

After preparing GE, SO and TS, generate the 20%, 40%, 60% and 80% samples:

```bash
python scripts/prepare_samples.py --datasets GE SO TS
```

For each source, one fixed-seed vertex permutation defines nested samples.
An update is included only when **all** endpoints of its edge are selected.
The endpoints are never clipped. Edge IDs and surviving update order are
preserved, while selected vertices are remapped in numeric order. All parent
query events are copied to the corresponding sampled update position,
including repeated positions and queries before the first sampled update
(`-1`). The original rank parameter is used at every scale. For the 100%
point, use the full input; no extra copy is needed.

```bash
python scripts/prepare_queries.py --datasets DW GE
```

This writes `DW_q1.queries.tsv`, `DW_q4.queries.tsv`, `DW_q16.queries.tsv` and
the corresponding GE files. Pair each file with the unchanged `DW.updates`
or `GE.updates`. Original calendar queries remain present. Fifteen times the
original count is added at near-even update positions, resolving collisions
by the nearest free position (left first at a tie). The 4x schedule takes the
middle of each group of five added positions, making all three levels nested.

## File format and validation

The first line of an `.updates` file is the vertex count `n`. Each remaining
line has the form:

```text
+ edge_id rank vertex_0 vertex_1 ...
- edge_id rank vertex_0 vertex_1 ...
```

Vertices are zero-based sorted distinct integers in `[0,n)`. IDs are
nonnegative, may be sparse, and identify an active edge; a deletion includes
the same ID and endpoints as its insertion. At most one edge with a given
endpoint set is active. A deleted ID may be reinserted.

The query file has no header; each row is `step<TAB>label`. `step` is the
zero-based index of the last completed update, so `-1` queries the initial
empty graph. Query positions are nondecreasing; equal positions are separate
query events and must not be merged. Labels are opaque strings.

To independently replay a prepared input and verify its checksums:

```bash
python scripts/data_validate.py data/processed/DW.updates data/processed/DW.queries.tsv --metadata data/processed/DW.json
```

This validation is untimed and stores the active graph in memory. It checks
every update, ID, endpoint set and query position; a large TS validation may
therefore need several GB of RAM. The offline self-tests cover the same
preprocessing branches on small synthetic fixtures; they do not claim to
re-execute full public-data experiments.

The public datasets remain subject to their providers' licenses and citation
requirements. Links and source names identify third-party data providers,
not the submitting authors. No real data archive is redistributed here.
