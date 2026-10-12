"""Replay a canonical input and check its IDs, endpoints, queries and checksums."""
import argparse
import json
from pathlib import Path
from data_download import sha256

def validate(updates, queries, metadata=None):
    updates, queries = Path(updates), Path(queries)
    active, endpoint_sets = {}, set()
    total = insertions = deletions = peak = rank = 0
    with updates.open(encoding="utf-8") as source:
        n = int(next(source))
        if n < 0:
            raise ValueError("Negative vertex count")
        for line in source:
            fields = line.split()
            if len(fields) < 5:
                raise ValueError(f"Malformed update {total}")
            op, eid, k = fields[0], int(fields[1]), int(fields[2])
            edge = tuple(map(int, fields[3:]))
            if eid < 0 or k < 2 or len(edge) != k or edge != tuple(sorted(set(edge))) or any(v < 0 or v >= n for v in edge):
                raise ValueError(f"Invalid edge at update {total}")
            if op == "+":
                if eid in active or edge in endpoint_sets:
                    raise ValueError(f"Duplicate active ID or endpoint set at update {total}")
                active[eid] = edge
                endpoint_sets.add(edge)
                insertions += 1
            elif op == "-":
                if active.pop(eid, None) != edge:
                    raise ValueError(f"Unmatched deletion at update {total}")
                endpoint_sets.remove(edge)
                deletions += 1
            else:
                raise ValueError(f"Unknown update operator at update {total}")
            total += 1
            rank = max(rank, k)
            peak = max(peak, len(active))
    count, previous = 0, -1
    with queries.open(encoding="utf-8") as source:
        for line in source:
            step, label = line.rstrip("\n").split("\t", 1)
            step = int(step)
            if not label or step < previous or not -1 <= step < total:
                raise ValueError(f"Invalid query at row {count}")
            previous = step
            count += 1
    result = dict(n=n, updates=total, queries=count, insertions=insertions, deletions=deletions,
                  peak_edges=peak, final_edges=len(active), observed_rank=rank, status="pass")
    if metadata:
        meta = json.loads(Path(metadata).read_text(encoding="utf-8"))
        for key, value in result.items():
            if key != "status" and key in meta and meta[key] != value:
                raise ValueError(f"Metadata mismatch: {key}")
        if rank > meta["rank"]:
            raise ValueError("Observed rank exceeds declared parameter")
        for kind, path in (("updates", updates), ("queries", queries)):
            if sha256(path) != meta["files"][kind]["sha256"]:
                raise ValueError(f"Checksum mismatch: {kind}")
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("updates", type=Path)
    parser.add_argument("queries", type=Path)
    parser.add_argument("--metadata", type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.updates, args.queries, args.metadata), indent=2))

if __name__ == "__main__":
    main()
