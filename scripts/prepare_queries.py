"""Write the nested 1x/4x/16x query schedules, leaving all updates unchanged."""
import argparse
import json
from pathlib import Path
from data_catalog import PROCESSED
from data_download import sha256

def nested_schedules(original, updates):
    n = len(original)
    occupied = {step for step, _ in original}
    if not n or updates - len({s for s in occupied if s >= 0}) < 15 * n:
        raise ValueError("Not enough distinct update positions to add 15 times the original query count")
    label = max(int(value) for _, value in original) + 1
    added = []
    for i in range(15 * n):
        midpoint = ((2 * i + 1) * updates) // (30 * n)
        delta = 0
        while True:
            choices = [midpoint] if delta == 0 else [midpoint - delta, midpoint + delta]
            available = [step for step in choices if 0 <= step < updates and step not in occupied]
            if available:
                step = available[0]
                break
            delta += 1
        occupied.add(step)
        added.append((step, str(label + i)))
    return {1: list(original), 4: sorted(original + added[2::5]), 16: sorted(original + added)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datasets", nargs="+", choices=["DW", "GE"], default=["DW", "GE"])
    parser.add_argument("--source-dir", type=Path, default=PROCESSED)
    parser.add_argument("--output-dir", type=Path, default=PROCESSED)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name in args.datasets:
        meta = json.loads((args.source_dir / (name + ".json")).read_text(encoding="utf-8"))
        query_path = args.source_dir / meta["files"]["queries"]["filename"]
        if sha256(query_path) != meta["files"]["queries"]["sha256"]:
            raise ValueError("Source query checksum mismatch")
        original = [(int(step), label) for step, label in (line.split("\t", 1) for line in query_path.read_text().splitlines())]
        for factor, schedule in nested_schedules(original, meta["updates"]).items():
            target = args.output_dir / f"{name}_q{factor}.queries.tsv"
            with target.open("x", encoding="utf-8", newline="\n") as output:
                output.writelines(f"{step}\t{label}\n" for step, label in schedule)
            print(json.dumps(dict(dataset=name, factor=factor, queries=len(schedule), filename=target.name, sha256=sha256(target))))

if __name__ == "__main__":
    main()
