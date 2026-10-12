"""Make the nested whole-edge vertex-induced samples used for Figure 2."""
import argparse
import json
from pathlib import Path
import random
from data_catalog import PROCESSED, SAMPLE_SEEDS
from data_download import sha256
from prepare_data import Output

def prepare(name, source_dir, output_dir, percentages=(20, 40, 60, 80), seed=None):
    source_dir, output_dir = Path(source_dir), Path(output_dir)
    meta = json.loads((source_dir / (name + ".json")).read_text(encoding="utf-8"))
    updates = source_dir / meta["files"]["updates"]["filename"]
    queries = source_dir / meta["files"]["queries"]["filename"]
    for kind, path in (("updates", updates), ("queries", queries)):
        if sha256(path) != meta["files"][kind]["sha256"]:
            raise ValueError(f"Source {kind} checksum mismatch")
    if not percentages or any(p <= 0 or p >= 100 for p in percentages) or sorted(set(percentages)) != list(percentages):
        raise ValueError("Percentages must be distinct increasing integers between 1 and 99")
    seed = SAMPLE_SEEDS[name] if seed is None else seed
    order = list(range(meta["n"]))
    random.Random(seed).shuffle(order)
    writers, mappings = [], []
    for percent in percentages:
        vertices = sorted(order[:meta["n"] * percent // 100])
        mapping = {vertex: i for i, vertex in enumerate(vertices)}
        sample = f"{name}_p{percent}"
        writer = Output(output_dir, sample, len(vertices), meta["rank"], dict(
            ordinary=meta["ordinary"], source_dataset=name, percent=percent, seed=seed,
            parent_update_sha256=meta["files"]["updates"]["sha256"],
            parent_query_sha256=meta["files"]["queries"]["sha256"],
            sample_policy="Nested prefixes of one shuffled vertex ordering; include an edge iff every endpoint is selected.",
            query_policy="Preserve every parent query event, including queries before the first sampled update or at repeated positions."))
        with (output_dir / (sample + ".vertices.txt")).open("x", encoding="utf-8", newline="\n") as output:
            output.writelines(f"{vertex}\n" for vertex in vertices)
        writers.append(writer)
        mappings.append(mapping)
    schedule = []
    for line in queries.read_text(encoding="utf-8").splitlines():
        step, label = line.split("\t", 1)
        schedule.append((int(step), label))
    qi = 0
    while qi < len(schedule) and schedule[qi][0] == -1:
        for writer in writers:
            writer.query(schedule[qi][1])
        qi += 1
    with updates.open(encoding="utf-8") as source:
        if int(next(source)) != meta["n"]:
            raise ValueError("Source vertex count mismatch")
        source_count = 0
        for step, line in enumerate(source):
            fields = line.split()
            op, eid, k = fields[0], int(fields[1]), int(fields[2])
            edge = tuple(map(int, fields[3:]))
            if len(edge) != k:
                raise ValueError("Invalid source edge")
            for writer, mapping in zip(writers, mappings):
                if all(v in mapping for v in edge):
                    writer.update(op, eid, tuple(mapping[v] for v in edge))
            while qi < len(schedule) and schedule[qi][0] == step:
                for writer in writers:
                    writer.query(schedule[qi][1])
                qi += 1
            source_count += 1
    if source_count != meta["updates"] or qi != len(schedule):
        raise ValueError("Source update/query count mismatch")
    return [writer.finish(dict(queries=meta["queries"])) for writer in writers]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datasets", nargs="+", choices=tuple(SAMPLE_SEEDS), default=["GE", "SO", "TS"])
    parser.add_argument("--source-dir", type=Path, default=PROCESSED)
    parser.add_argument("--output-dir", type=Path, default=PROCESSED)
    parser.add_argument("--percentages", nargs="+", type=int, default=[20, 40, 60, 80])
    args = parser.parse_args()
    for name in args.datasets:
        rows = prepare(name, args.source_dir, args.output_dir, args.percentages)
        print(json.dumps([{key: row[key] for key in ("dataset", "n", "updates", "queries")} for row in rows], indent=2))

if __name__ == "__main__":
    main()
