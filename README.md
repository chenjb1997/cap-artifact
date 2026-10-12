# CAP: Dynamic Densest Subgraph Maintenance

This repository contains CAP, its ablation variants, the comparison algorithms,
and the data preparation scripts for dynamic densest-subgraph experiments on
graphs and hypergraphs. The algorithms process edge insertions and deletions;
hypergraph queries return a vertex set with high induced density.

Start with the small example below. Large datasets are downloaded from their
public sources and processed locally; they are not stored in this repository.

## 1. Requirements and build

- **C++:** GCC or Clang with C++20 support, and zlib development headers.
  The kernels use `__int128`; MSVC is not supported.
- **Python:** 3.10 or newer. Data preparation and the run scripts use the
  standard library. Only the Exact LP baseline needs OR-Tools.
- **Julia:** 1.9 or newer, for DI and ExactFlow. Their dependencies are Julia
  standard libraries and the bundled zlib library.

For example, on Ubuntu:

```bash
sudo apt-get install build-essential zlib1g-dev python3 python3-venv
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python scripts/build.py
```

Install Julia separately if you want to run the Julia baselines, and put
`julia` on `PATH` (or use the runner's `--julia /path/to/julia` option).
To build CAP alone, use `python scripts/build.py cap`.
`python scripts/build.py --list` lists all C++ targets; `--cxx clang++` selects
another compiler. On Windows, use MinGW-w64 GCC and its matching zlib package;
the same Python commands build `.exe` files under `build/`.

CMake is also supported:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

## 2. Quick start

Run CAP on the included six-vertex example:

```bash
python scripts/run.py --method cap --input data/toy.updates \
  --queries data/toy.queries.tsv --rank 3 --epsilon 0.1 \
  --output results/toy-cap
```

Each output directory must be new. The runner saves the command, logs, process
status, and runtime summary. CAP also saves the query answers and maintenance
statistics. A separate tiny validation run checks the independent state
observer and enumerates every vertex subset to obtain the optimum:

```bash
python scripts/run.py --method cap --input data/toy.updates \
  --queries data/toy.queries.tsv --rank 3 --epsilon 0.1 --check 2 \
  --output results/toy-cap-checked
```

`--check 2` on the compact/ablation CAP adapters is restricted to at most
16 vertices. Validation work is excluded from performance comparisons.
Run the small regression checks with:

```bash
python tests/smoke.py
# Also exercise the LP and both Julia baselines:
python tests/smoke.py --all-baselines
```

## 3. Download and prepare the data

The download catalog includes public URLs and SHA-256 checksums. This command
downloads and prepares one small hypergraph dataset:

```bash
python scripts/prepare_data.py --datasets DW --download
```

After the DW example, prepare the remaining hypergraph inputs and the
ordinary-graph inputs with:

```bash
python scripts/prepare_data.py --datasets MA AU GE SO TS4 TS7 TS --download
python scripts/prepare_data.py --datasets graphs --download
```

On a fresh data directory, `--datasets hyper` prepares all eight hypergraph
inputs. Existing outputs are never overwritten.

| Input | Public dataset | Rank |
|---|---|---:|
| DW | [DAWN](https://www.cs.cornell.edu/~arb/data/DAWN/) | 16 |
| MA | [tags-math-sx](https://www.cs.cornell.edu/~arb/data/tags-math-sx/) | 5 |
| AU | [tags-ask-ubuntu](https://www.cs.cornell.edu/~arb/data/tags-ask-ubuntu/) | 5 |
| GE | [coauth-MAG-Geology](https://www.cs.cornell.edu/~arb/data/coauth-MAG-Geology/) | 25 |
| SO | [tags-stack-overflow](https://www.cs.cornell.edu/~arb/data/tags-stack-overflow/) | 5 |
| TS4 / TS7 / TS | [threads-stack-overflow](https://www.cs.cornell.edu/~arb/data/threads-stack-overflow/) | 4 / 7 / 25 |
| EM | [email-Eu-core](https://snap.stanford.edu/data/email-Eu-core.html) | 2 |
| HT | [ca-HepTh](https://snap.stanford.edu/data/ca-HepTh.html) | 2 |
| WV | [wiki-Vote](https://snap.stanford.edu/data/wiki-Vote.html) | 2 |
| BC | [Bitcoin OTC](https://snap.stanford.edu/data/soc-sign-bitcoin-otc.html) | 2 |
| AS | [AS-Caida](https://snap.stanford.edu/data/as-Caida.html) | 2 |
| SW | [Dynamic Simple Wikipedia](https://dyngraphlab.github.io/) | 2 |

Each prepared input has three files under `data/processed/`:

- `NAME.updates`: canonical insertions and deletions.
- `NAME.queries.tsv`: explicit query events.
- `NAME.json`: parameters, counts, and checksums for the generated files.

Singleton hyperedges and duplicate active hyperedges are removed. Calendar
inputs use their documented time windows. The thread inputs insert edges in
timestamp order, then delete a fixed-seed quarter and reinsert those same edge
IDs. These last two phases are synthetic. TS uses every supplied rank from 2
through 25; TS4 and TS7 apply rank bounds 4 and 7 to the same source.

See [data/README.md](data/README.md) for the complete transformation rules,
manual-download fallback, sample generation, and input validation. Preparation
of the large inputs can take substantial time and disk space; it is separate
from algorithm timing. Raw archives, prepared inputs, and results are ignored
by Git.

## 4. Run CAP and the baselines

For a prepared dataset, the runner selects the correct CAP adapter and rank:

```bash
python scripts/run.py --dataset DW --method cap --epsilon 0.1 \
  --repeat 3 --output results/DW-cap
python scripts/run.py --dataset DW --method udshp --epsilon 0.1 \
  --repeat 3 --output results/DW-udshp
python scripts/run.py --dataset DW --method exact \
  --repeat 3 --output results/DW-exact
python scripts/run.py --dataset EM --method improved-dynopt \
  --repeat 3 --output results/EM-improved
```

| Runner method | Implementation |
|---|---|
| `cap` | CAP with the maintained solution and budgeted refresh |
| `cap-tight` | Tight thresholds in the CAP ablation |
| `cap-nf` | CAP without budgeted peeling refresh |
| `cap-unit` | CAP with one-unit allocation transfers |
| `udshp` | Original dynamic hypergraph baseline with an input/query adapter |
| `cq` | C++ reconstruction of the CQ algorithm |
| `exact` | Original snapshot LP solved with OR-Tools GLOP |
| `di` | Original density-improvement solver, in Julia |
| `exactflow` | Original exact-flow solver, in Julia |
| `improved-dynopt` | Ordinary-graph orientation baseline with the documented boundary fix |
| `insdel` | Ordinary-graph insertion/deletion baseline and native query candidate |

All baseline source is included. See [baselines/README.md](baselines/README.md)
for direct commands and [baseline provenance](baselines/PROVENANCE.md) for
upstream revisions, adapter changes, and component licenses. Upstream copyright
and license notices are preserved.

The main options are:

| Option | Meaning | Default |
|---|---|---|
| `--dataset` | Prepared input label | Use `--input` for custom data |
| `--input`, `--queries`, `--rank` | Custom update file, schedule, and maximum edge size | From the dataset label |
| `--epsilon` | Accuracy parameter, between 0 and 1 | `0.1` |
| `--budget` | CAP replication parameter B | `ceil(2*rank/epsilon)` |
| `--repeat` | Number of process runs | `1` |
| `--limit` | Whole-process wall-clock seconds per run; `0` disables | `1800` |
| `--check` | Diagnostic checks; `0` is used for timing | `0` |
| `--experiment` | `main`, `ablation`, `size`, or `ordinary` | `main` |
| `--adapter` | Explicit CAP adapter override | `auto` |
| `--cadence` | Query interval for the ordinary-graph baselines | `100` updates |

For custom CAP inputs with a query at step `-1` (the initial empty graph), use
`--adapter size`; the other CAP adapters expect post-update queries.

The 1,800-second limit applies to the whole process. A genuine timeout cancels
only the remaining repetitions of the same configuration. Errors are recorded
as errors and stop a suite. Cancelled runs have no runtime, and configurations
with an incomplete repetition have no mean. Process wall time and algorithm
service time are reported separately. The launcher enforces the wall limit;
it does not impose a RAM limit. Use an external resource limit if reproducing
the 24-GiB experiment envelope.

Hypergraph service time includes updates and materializing the returned vertex
sets. The ordinary-graph comparison uses its scalar-query protocol and is
reported separately. Different CAP adapters preserve these measurement
boundaries; see [src/README.md](src/README.md). A successful process exit alone
is not independent scientific validation of its answers.

## 5. Experiment commands

`scripts/experiments.py` prints the commands for a suite. Add `--execute` to
run them serially. It uses three repetitions and a 1,800-second limit unless
overridden. For example:

```bash
python scripts/experiments.py --suite main --datasets DW --output results/main
python scripts/experiments.py --suite ablation --datasets DW --output results/ablation --execute
```

| Suite | Inputs | Configuration |
|---|---|---|
| `main` | DW, MA, AU, GE, SO, TS4, TS7, TS | CAP/CQ/Udshp at epsilon 1, .7, .5, .2, .1; Exact/DI/ExactFlow once per input configuration |
| `ablation` | The eight hypergraph inputs | CAP, CAP-Tight, CAP-NF, CAP-Unit at epsilon .1 |
| `ordinary` | EM, HT, WV, BC, AS, SW | CAP, ImprovedDynOpt, INS/DEL at epsilon .1 |
| `size` | GE, SO, TS | CAP at 20%, 40%, 60%, 80%, 100% of vertices |
| `query-frequency` | DW, GE | CAP/CQ/Udshp/Exact/DI/ExactFlow at 1x, 4x, 16x the calendar query count, epsilon .5 |
| `accuracy` | GE | Epsilon 1, .7, .5, .2, .1, .08, .06, .04, .02, .01; static methods are run once per input configuration |

Generate the additional sample inputs and schedules before those suites:

```bash
python scripts/prepare_samples.py --datasets GE SO TS
python scripts/prepare_queries.py --datasets DW GE
```

The size suite runs its 100% endpoint itself. When combining suites, an already
validated matching full-input measurement can be reused instead of running the
same configuration again. Do not combine partially completed runs into a full
runtime. See the per-run JSON and answer evidence before using results in a
table or figure.

## Packaging checks

The packaged programs were checked on Windows x64 with GCC 12.1, Python 3.12,
OR-Tools 9.11.4210, and Julia 1.9.4. All nine C++ targets compiled; the smoke
test passed 19 method/adapter cases and independently recounted 146 hypergraph
answers. The exact methods also matched brute-force optima on the tiny cases.
The preprocessing tests cover calendar windows, thread phases, graph updates,
samples, and query schedules without downloading large datasets.

The POSIX build paths are provided, but have not been exercised by this
packaging check. These are small correctness and integration checks, not a
rerun of the paper's large timing experiments.

## Repository layout

```text
src/          CAP kernels, faithful input adapters, independent observers
baselines/    Baseline source, adapters, provenance, and upstream licenses
scripts/      Build, download, preprocessing, validation, and experiment entry points
data/         Tiny example and data documentation (large inputs are downloaded)
tests/        Small build/run/correctness checks
build/        Generated executables (ignored)
results/      Local run outputs (ignored)
```
