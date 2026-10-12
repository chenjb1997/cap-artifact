"""Prepare the paper's canonical update/query inputs using Python's standard library.

Downloads are explicit (--download). Existing outputs are never overwritten.
Large chronological sorts and thread deduplication use temporary SQLite files.
"""
import argparse
from collections import Counter, defaultdict, deque
import gzip
import itertools
import json
import lzma
from pathlib import Path
import random
import sqlite3
import struct
import tempfile
from data_catalog import RAW, PROCESSED, EXPECTED, THREAD_SEED, select, source_for
from data_download import download, sha256

def pack(edge):
    return struct.pack("<" + "I" * len(edge), *edge)

def unpack(value):
    return struct.unpack("<" + "I" * (len(value) // 4), value)

def splitmix64(value):
    mask = (1 << 64) - 1
    value = (value + 0x9e3779b97f4a7c15) & mask
    value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & mask
    value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & mask
    return value ^ (value >> 31)

def sqlite(path):
    connection = sqlite3.connect(path)
    connection.execute("PRAGMA journal_mode=OFF")
    connection.execute("PRAGMA synchronous=OFF")
    connection.execute("PRAGMA temp_store=FILE")
    connection.execute("PRAGMA cache_size=-65536")
    return connection

def source_rows(folder):
    files = {}
    for suffix in ("nverts", "simplices", "times"):
        matches = list(Path(folder).glob("*-" + suffix + ".txt"))
        if len(matches) != 1:
            raise ValueError(f"Expected exactly one *-{suffix}.txt in the source directory")
        files[suffix] = matches[0]
    with files["nverts"].open() as sizes, files["simplices"].open() as vertices, files["times"].open() as times:
        for row, line in enumerate(sizes):
            rank = int(line)
            if rank < 1:
                raise ValueError("Source has an empty or negative-rank edge")
            edge = tuple(int(next(vertices)) for _ in range(rank))
            stamp = int(next(times))
            if len(set(edge)) != rank or any(v < 0 or v > 0xffffffff for v in edge):
                raise ValueError("Source endpoints must be distinct nonnegative uint32 values")
            yield row, stamp, edge
        if any(line.strip() for line in vertices) or any(line.strip() for line in times):
            raise ValueError("Source nverts/simplices/times lengths disagree")

def require_fresh_output(folder, name, include_auxiliary=False):
    """Reject final or partial products before reading or downloading any input."""
    folder = Path(folder)
    suffixes = (".updates", ".queries.tsv", ".json")
    if include_auxiliary and name == "SW":
        suffixes += (".excluded_pairs.json",)
    for suffix in suffixes:
        path = folder / (name + suffix)
        for candidate in (path, path.with_name(path.name + ".partial")):
            if candidate.exists():
                raise FileExistsError(f"Output exists: {candidate.name}; choose a fresh --output-dir")

class Output:
    def __init__(self, folder, name, n, rank, details=None):
        self.folder, self.name, self.n, self.rank = Path(folder), name, n, rank
        require_fresh_output(self.folder, name)
        self.folder.mkdir(parents=True, exist_ok=True)
        self.paths = {suffix: self.folder / (name + suffix) for suffix in (".updates", ".queries.tsv", ".json")}
        self.upath = self.paths[".updates"].with_name(name + ".updates.partial")
        self.qpath = self.paths[".queries.tsv"].with_name(name + ".queries.tsv.partial")
        self.out = self.upath.open("x", encoding="utf-8", newline="\n", buffering=1024 * 1024)
        self.qout = self.qpath.open("x", encoding="utf-8", newline="\n")
        self.out.write(f"{n}\n")
        self.counts = Counter()
        self.active = 0
        self.last_query_step = -2
        self.details = details or {}

    def update(self, op, eid, edge):
        self.out.write(f"{op} {eid} {len(edge)} " + " ".join(map(str, edge)) + "\n")
        self.counts["updates"] += 1
        self.counts["insertions" if op == "+" else "deletions"] += 1
        self.counts["observed_rank"] = max(self.counts["observed_rank"], len(edge))
        self.active += 1 if op == "+" else -1
        if self.active < 0:
            raise ValueError("Active edge count became negative")
        self.counts["peak_edges"] = max(self.counts["peak_edges"], self.active)

    def query(self, label):
        step = self.counts["updates"] - 1
        self.qout.write(f"{step}\t{label}\n")
        self.counts["queries"] += 1
        self.counts["initial_queries"] += step < 0
        self.last_query_step = step

    def cadence(self, period):
        if self.counts["updates"] % period == 0:
            self.query(self.counts["updates"])

    def finish(self, expected=None):
        self.out.close()
        self.qout.close()
        metadata = dict(dataset=self.name, n=self.n, rank=self.rank, warmup=0,
                        **dict(self.counts), final_edges=self.active, **self.details)
        for key, value in (expected or {}).items():
            if metadata.get(key) != value:
                raise ValueError(f"Prepared {self.name} {key}={metadata.get(key)}, expected {value}; partial files preserved")
        self.upath.replace(self.paths[".updates"])
        self.qpath.replace(self.paths[".queries.tsv"])
        metadata["files"] = {kind: dict(filename=self.paths[suffix].name, sha256=sha256(self.paths[suffix]))
                             for kind, suffix in (("updates", ".updates"), ("queries", ".queries.tsv"))}
        metadata["status"] = "prepared"
        self.paths[".json"].write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        return metadata

def prepare_calendar(name, folder, output, expected=None):
    require_fresh_output(output, name, include_auxiliary=True)
    spec = source_for(name)
    scale, window = spec["scale"], spec["window"]
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="calendar-", dir=output) as work:
        db = sqlite(Path(work) / "events.sqlite")
        try:
            db.execute("CREATE TABLE events (t INTEGER, ordinal INTEGER, edge BLOB)")
            # The two first-appearance maps match the calendar conversion order:
            # first all raw vertices, then sorted intermediate endpoints in time order.
            raw_map, batch = {}, []
            for ordinal, stamp, edge in source_rows(folder):
                intermediate = []
                for vertex in edge:
                    if vertex not in raw_map:
                        raw_map[vertex] = len(raw_map)
                    intermediate.append(raw_map[vertex])
                bucket = int(stamp / scale)
                if len(edge) > 1 and bucket > 1:
                    batch.append((bucket, ordinal, pack(sorted(intermediate))))
                if len(batch) >= 10000:
                    db.executemany("INSERT INTO events VALUES (?,?,?)", batch)
                    batch.clear()
            db.executemany("INSERT INTO events VALUES (?,?,?)", batch)
            db.execute("CREATE INDEX chronological ON events(t,ordinal)")
            db.commit()
            del raw_map, batch
            mapping = {}
            for blob, in db.execute("SELECT edge FROM events ORDER BY t,ordinal"):
                for vertex in unpack(blob):
                    if vertex not in mapping:
                        mapping[vertex] = len(mapping)
            if not mapping:
                raise ValueError("No nonsingleton events after initial timestamp filter")
            writer = Output(output, name, len(mapping), spec["rank"], dict(ordinary=False,
                source=spec["page"], time_scale=scale, window_bins=window, timestamp_threshold=1,
                duplicate_policy="Ignore already-active endpoint sets; duplicate arrivals do not refresh expiry.",
                query_policy="Calendar boundaries after the first complete window, plus the final bin; include empty bins.",
                vertex_mapping="Two first-appearance maps, with stable chronological event ordering."))
            active, admitted = {}, defaultdict(list)
            first = previous = None
            next_id = 0
            for stamp, bucket in itertools.groupby(db.execute("SELECT t,edge FROM events ORDER BY t,ordinal"), key=lambda row: row[0]):
                if first is None:
                    first = previous = stamp
                for year in range(previous + 1, stamp + 1):
                    if year - first >= window:
                        writer.query(year - 1)
                        for edge in admitted.pop(year - window, []):
                            eid = active.pop(edge)
                            writer.update("-", eid, edge)
                for _, blob in bucket:
                    edge = tuple(sorted(mapping[v] for v in unpack(blob)))
                    if edge not in active:
                        active[edge] = next_id
                        admitted[stamp].append(edge)
                        writer.update("+", next_id, edge)
                        next_id += 1
                previous = stamp
            writer.query(previous)
            return writer.finish(expected)
        finally:
            db.close()

def prepare_threads(name, folder, output, expected=None, seed=THREAD_SEED):
    require_fresh_output(output, name, include_auxiliary=True)
    cap = {"TS4": 4, "TS7": 7, "TS": 25}[name]
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="threads-", dir=output) as work:
        db = sqlite(Path(work) / "edges.sqlite")
        try:
            db.execute("CREATE TABLE unique_edges (edge BLOB PRIMARY KEY,t INTEGER,raw_id INTEGER) WITHOUT ROWID")
            insert = ("INSERT INTO unique_edges VALUES (?,?,?) ON CONFLICT(edge) DO UPDATE SET t=excluded.t,raw_id=excluded.raw_id "
                      "WHERE excluded.t < t OR (excluded.t=t AND excluded.raw_id < raw_id)")
            batch = []
            for raw_id, stamp, edge in source_rows(folder):
                if len(edge) > 25:
                    raise ValueError("This experiment uses the provider's rank-at-most-25 archive")
                if 2 <= len(edge) <= cap:
                    batch.append((pack(sorted(edge)), stamp, raw_id))
                if len(batch) >= 10000:
                    db.executemany(insert, batch)
                    batch.clear()
            db.executemany(insert, batch)
            db.create_function("priority", 1, lambda index: splitmix64(index ^ seed).to_bytes(8, "big"))
            db.execute("CREATE TABLE ordered AS SELECT row_number() OVER(ORDER BY t,raw_id)-1 AS eid, edge, t,raw_id,priority(raw_id) AS priority FROM unique_edges")
            db.execute("CREATE INDEX ordered_eid ON ordered(eid)")
            db.execute("CREATE INDEX ordered_priority ON ordered(priority,raw_id)")
            db.commit()
            vertices = set()
            for blob, in db.execute("SELECT edge FROM ordered"):
                vertices.update(unpack(blob))
            mapping = {vertex: i for i, vertex in enumerate(sorted(vertices))}
            del vertices
            m = db.execute("SELECT count(*) FROM ordered").fetchone()[0]
            if m == 0:
                raise ValueError("No eligible thread edges")
            quarter = m // 4
            writer = Output(output, name, len(mapping), cap, dict(ordinary=False, source=source_for(name)["page"],
                seed=seed, unique_edges=m, synthetic_deletion_reinsertion_schedule=True,
                duplicate_policy="Exact endpoint sets globally deduplicated; earliest (timestamp, raw row) represents each set.",
                deletion_selection="floor(m/4) smallest (splitmix64(raw_row XOR seed), raw_row) priorities.",
                vertex_mapping="Incident raw numeric IDs sorted, remapped densely.",
                query_policy="Every 1000 updates and each phase end; coincident positions merged."))
            total = m + 2 * quarter
            query_labels = {completed: "every_1000_updates" for completed in range(1000, total + 1, 1000)}
            for completed, label in ((m, "full_inserted"), (m + quarter, "seeded_quarter_deleted"), (total, "seeded_quarter_reinserted")):
                query_labels[completed] = query_labels.get(completed, "") + ("+" if completed in query_labels else "") + label
            phases = [("+", "SELECT eid,edge FROM ordered ORDER BY eid", ()),
                      ("-", "SELECT eid,edge FROM ordered ORDER BY priority,raw_id LIMIT ?", (quarter,)),
                      ("+", "SELECT eid,edge FROM ordered ORDER BY priority,raw_id LIMIT ?", (quarter,))]
            for op, sql, params in phases:
                for eid, blob in db.execute(sql, params):
                    edge = tuple(mapping[v] for v in unpack(blob))
                    writer.update(op, eid, edge)
                    if writer.counts["updates"] in query_labels:
                        writer.query(query_labels[writer.counts["updates"]])
            return writer.finish(expected)
        finally:
            db.close()

def prepare_static(name, archive, output, expected=None, seed=0, toggles=20000):
    require_fresh_output(output, name, include_auxiliary=True)
    raw = set()
    with gzip.open(archive, "rt", encoding="utf-8") as source:
        for line in source:
            if not line.strip() or line.startswith("#"):
                continue
            a, b = map(int, line.split()[:2])
            if a != b:
                raw.add(tuple(sorted((a, b))))
    mapping = {v: i for i, v in enumerate(sorted({v for edge in raw for v in edge}))}
    edges = sorted(tuple(mapping[v] for v in edge) for edge in raw)
    if not edges:
        raise ValueError("Empty graph")
    rng = random.Random(seed)
    ids = list(range(len(edges)))
    rng.shuffle(ids)
    initial = ids[:len(ids) // 2]
    active = set(initial)
    writer = Output(output, name, len(mapping), 2, dict(ordinary=True, source=source_for(name)["page"],
        seed=seed, toggle_updates=toggles, historical_initialization_updates=len(initial),
        query_policy="Every 100 updates and the final update.",
        schedule="Synthetic random toggles on deduplicated undirected public topology; initialize a shuffled half of the edges."))
    for eid in initial:
        writer.update("+", eid, edges[eid])
        writer.cadence(100)
    for _ in range(toggles):
        eid = rng.randrange(len(edges))
        op = "-" if eid in active else "+"
        if op == "-":
            active.remove(eid)
        else:
            active.add(eid)
        writer.update(op, eid, edges[eid])
        writer.cadence(100)
    if writer.last_query_step != writer.counts["updates"] - 1:
        writer.query(writer.counts["updates"])
    return writer.finish(expected)

def prepare_bitcoin(archive, output, expected=None):
    require_fresh_output(output, "BC", include_auxiliary=True)
    rows = []
    with gzip.open(archive, "rt", encoding="utf-8") as source:
        for line in source:
            a, b, _, timestamp = line.split(",")
            a, b = int(a), int(b)
            if a != b:
                rows.append((float(timestamp), tuple(sorted((a, b)))))
    rows.sort()
    mapping = {v: i for i, v in enumerate(sorted({v for _, edge in rows for v in edge}))}
    writer = Output(output, "BC", len(mapping), 2, dict(ordinary=True, source=source_for("BC")["page"],
        window_days=90, query_policy="Every 100 updates and the final update.",
        schedule="Chronological ratings, ignored signs/directions, reference-counted edges, synthetic 90-day expiry with strict old_time < current_time - window."))
    queue, counts, ids = deque(), Counter(), {}
    for timestamp, raw_edge in rows:
        while queue and queue[0][0] < timestamp - 90 * 86400:
            _, old = queue.popleft()
            counts[old] -= 1
            if counts[old] == 0:
                writer.update("-", ids[old], old)
                writer.cadence(100)
        edge = tuple(mapping[v] for v in raw_edge)
        if edge not in ids:
            ids[edge] = len(ids)
        if counts[edge] == 0:
            writer.update("+", ids[edge], edge)
            writer.cadence(100)
        counts[edge] += 1
        queue.append((timestamp, edge))
    if writer.last_query_step != writer.counts["updates"] - 1:
        writer.query(writer.counts["updates"])
    return writer.finish(expected)

def prepare_dynamic(name, archive, output, expected=None):
    require_fresh_output(output, name, include_auxiliary=True)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="graph-", dir=output) as work:
        db = sqlite(Path(work) / "events.sqlite")
        try:
            db.execute("CREATE TABLE events (t INTEGER,line INTEGER,u INTEGER,v INTEGER,delta INTEGER)")
            labels, batch = set(), []
            with lzma.open(archive, "rt", encoding="utf-8") as source:
                for line_number, line in enumerate(source, 1):
                    if not line.strip() or line.startswith(("#", "%")):
                        continue
                    u, v, delta, stamp = map(int, line.split())
                    if delta not in (-1, 1):
                        raise ValueError("Dynamic graph updates require signed unit multiplicities")
                    labels.update((u, v))
                    batch.append((stamp, line_number, u, v, delta))
                    if len(batch) >= 10000:
                        db.executemany("INSERT INTO events VALUES (?,?,?,?,?)", batch)
                        batch.clear()
            db.executemany("INSERT INTO events VALUES (?,?,?,?,?)", batch)
            db.execute("CREATE INDEX chronological ON events(t,line)")
            db.commit()
            sql = "SELECT t,line,u,v,delta FROM events ORDER BY t,line"
            excluded, exclusions = set(), []
            if name == "SW":
                directed = Counter()
                for stamp, line_number, u, v, delta in db.execute(sql):
                    directed[u, v] += delta
                    pair = (min(u, v), max(u, v))
                    if directed[u, v] < 0 and pair not in excluded:
                        excluded.add(pair)
                        exclusions.append(dict(u=pair[0], v=pair[1], first_negative_line=line_number, timestamp=stamp))
                del directed
                (output / "SW.excluded_pairs.json").write_text(json.dumps(exclusions, indent=2) + "\n", encoding="utf-8")
            mapping = {v: i for i, v in enumerate(sorted(labels))}
            writer = Output(output, name, len(mapping), 2, dict(ordinary=True, source=source_for(name)["page"],
                excluded_inconsistent_pairs=len(excluded), query_policy="Every 100 updates and the final update.",
                schedule="Stable (timestamp, original line) order; reference-count both directions; emit only undirected presence transitions.",
                cleaning="SW removes all events of any unordered pair with a negative directed prefix; AS does not apply this removal."))
            directed, projected, ids = Counter(), Counter(), {}
            for stamp, line_number, raw_u, raw_v, delta in db.execute(sql):
                if (min(raw_u, raw_v), max(raw_u, raw_v)) in excluded:
                    continue
                u, v = mapping[raw_u], mapping[raw_v]
                directed[u, v] += delta
                if directed[u, v] < 0:
                    raise ValueError(f"Unmatched directed deletion at input line {line_number}")
                if u == v:
                    continue
                pair = (min(u, v), max(u, v))
                before = projected[pair]
                projected[pair] += delta
                after = projected[pair]
                if (before == 0) == (after == 0):
                    continue
                if pair not in ids:
                    ids[pair] = len(ids)
                writer.update("+" if after else "-", ids[pair], pair)
                writer.cadence(100)
            if writer.last_query_step != writer.counts["updates"] - 1:
                writer.query(writer.counts["updates"])
            return writer.finish(expected)
        finally:
            db.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datasets", nargs="+", default=["DW"], help="Dataset labels, hyper, graphs, or all")
    parser.add_argument("--raw-dir", type=Path, default=RAW)
    parser.add_argument("--output-dir", type=Path, default=PROCESSED)
    parser.add_argument("--download", action="store_true", help="Download and verify missing public archives first")
    args = parser.parse_args()
    names = select(args.datasets)
    # Check every requested output first so a later existing dataset cannot
    # cause an avoidable download or partial batch of new preparations.
    for name in names:
        require_fresh_output(args.output_dir, name, include_auxiliary=True)
    for name in names:
        spec = source_for(name)
        archive = args.raw_dir / spec["filename"]
        if not archive.exists() and not args.download:
            raise ValueError(f"Missing {archive.name}; run data_download.py first or add --download")
        # Existing archives are verified and extracted members checked against
        # them too; this performs no network request when the archive is present.
        download(name, args.raw_dir)
        if name.startswith("TS"):
            result = prepare_threads(name, args.raw_dir / spec["name"], args.output_dir, EXPECTED[name])
        elif spec["kind"] == "cornell":
            result = prepare_calendar(name, args.raw_dir / spec["name"], args.output_dir, EXPECTED[name])
        elif spec["kind"] == "static_graph":
            result = prepare_static(name, archive, args.output_dir, EXPECTED[name])
        elif spec["kind"] == "bitcoin":
            result = prepare_bitcoin(archive, args.output_dir, EXPECTED[name])
        else:
            result = prepare_dynamic(name, archive, args.output_dir, EXPECTED[name])
        print(json.dumps({key: result[key] for key in ("dataset", "n", "rank", "updates", "queries", "peak_edges")}), flush=True)

if __name__ == "__main__":
    main()
