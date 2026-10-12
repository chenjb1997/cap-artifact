"""Offline tiny fixtures for preprocessing; no downloads or timing experiments."""
import gzip
import json
import lzma
from pathlib import Path
import tempfile
from data_catalog import ROOT
from data_download import sha256
from data_validate import validate
from prepare_data import prepare_calendar, prepare_threads, prepare_static, prepare_bitcoin, prepare_dynamic, require_fresh_output
from prepare_queries import nested_schedules
from prepare_samples import prepare as prepare_samples

def raw_fixture(folder, rows):
    folder.mkdir()
    (folder / "fixture-nverts.txt").write_text("".join(f"{len(edge)}\n" for stamp, edge in rows))
    (folder / "fixture-times.txt").write_text("".join(f"{stamp}\n" for stamp, edge in rows))
    (folder / "fixture-simplices.txt").write_text("".join(f"{v}\n" for stamp, edge in rows for v in edge))

def check(folder, name):
    return validate(folder / (name + ".updates"), folder / (name + ".queries.tsv"), folder / (name + ".json"))

def main():
    checks = []
    assert validate(ROOT / "data/toy.updates", ROOT / "data/toy.queries.tsv")["updates"] == 8
    checks.append("toy updates, deletion/reinsertion, repeated query positions")
    with tempfile.TemporaryDirectory(prefix="data-selftest-") as temporary:
        root = Path(temporary)
        raw = root / "calendar"
        raw_fixture(raw, [(2, (99,)), (4, (20, 10)), (2, (10, 30)), (2, (30, 10)),
                          (4, (10, 20)), (1, (88, 89)), (5, (10, 30))])
        output = root / "output"
        record = prepare_calendar("DW", raw, output)
        assert record["n"] == 3 and record["updates"] == 5 and record["queries"] == 4
        assert (output / "DW.updates").read_text().splitlines() == [
            "3", "+ 0 2 0 1", "- 0 2 0 1", "+ 1 2 0 2", "- 1 2 0 2", "+ 2 2 0 1"]
        assert (output / "DW.queries.tsv").read_text().splitlines() == ["0\t2", "1\t3", "2\t4", "4\t5"]
        check(output, "DW")
        checks.append("calendar stable order, two vertex maps, active deduplication, initial filter, empty bin, no final drain")
        original = (output / "DW.updates").read_bytes()
        try:
            prepare_calendar("DW", root / "missing-source", output)
        except FileExistsError:
            pass
        else:
            raise AssertionError("Existing output must be rejected before the missing source is read")
        assert (output / "DW.updates").read_bytes() == original
        partial_folder = root / "partial"
        partial_folder.mkdir()
        (partial_folder / "TS.updates.partial").write_text("incomplete\n")
        try:
            prepare_threads("TS", root / "missing-source", partial_folder)
        except FileExistsError:
            pass
        else:
            raise AssertionError("Partial output must be rejected before input processing")
        (partial_folder / "SW.excluded_pairs.json").write_text("[]\n")
        try:
            require_fresh_output(partial_folder, "SW", include_auxiliary=True)
        except FileExistsError:
            pass
        else:
            raise AssertionError("Existing auxiliary output must also be protected")
        checks.append("existing final, partial and auxiliary outputs rejected before input access; original bytes unchanged")
        ge = root / "ge"
        raw_fixture(ge, [(2, (0, 1)), (5, (0, 1)), (12, (1, 2)), (15, (0, 1))])
        record = prepare_calendar("GE", ge, output)
        assert record["updates"] == 4 and record["queries"] == 5
        assert (output / "GE.queries.tsv").read_text().splitlines() == ["0\t11", "2\t12", "2\t13", "2\t14", "3\t15"]
        check(output, "GE")
        checks.append("GE ten-year expiry from admission, no duplicate refresh, annual queries")
        threads = root / "threads"
        raw_fixture(threads, [(10, (1, 3)), (1, (3, 1)), (2, (2, 3)), (3, (0, 2)),
                              (4, (0, 1, 2)), (5, (0, 1, 2, 3, 4)), (6, (99,))])
        for name, expected_updates, expected_n in [("TS4", 6, 4), ("TS7", 7, 5), ("TS", 7, 5)]:
            record = prepare_threads(name, threads, output)
            assert record["updates"] == expected_updates and record["n"] == expected_n
            assert record["queries"] == 3
            check(output, name)
            updates = (output / (name + ".updates")).read_text().splitlines()[1:]
            assert updates[-2].startswith("- ") and updates[-1] == "+" + updates[-2][1:]
        checks.append("TS whole-edge rank filters, earliest global representative, quarter deletion and same-ID reinsertion")
        static = root / "edges.gz"
        with gzip.open(static, "wt") as f:
            f.write("# source\n2 1\n1 2\n3 3\n2 4\n1 4\n")
        a, b = root / "static_a", root / "static_b"
        prepare_static("EM", static, a, seed=0, toggles=20)
        prepare_static("EM", static, b, seed=0, toggles=20)
        assert (a / "EM.updates").read_bytes() == (b / "EM.updates").read_bytes()
        assert check(a, "EM")["n"] == 3
        checks.append("static undirected deduplication, removed self loops, reproducible random toggles")
        bitcoin = root / "bitcoin.gz"
        with gzip.open(bitcoin, "wt") as f:
            f.write("1,2,1,0\n2,1,-1,1\n3,4,1,7776001\n3,4,1,7776002\n")
        prepare_bitcoin(bitcoin, output)
        assert check(output, "BC")["updates"] == 3
        checks.append("Bitcoin strict 90-day boundary and reference-counted duplicate expiry")
        graph = root / "dynamic.xz"
        with lzma.open(graph, "wt") as f:
            f.write("% dynamic\n1 2 1 3\n2 1 1 4\n1 2 -1 5\n2 1 -1 6\n8 9 -1 1\n8 9 1 2\n")
        prepare_dynamic("SW", graph, output)
        assert check(output, "SW")["updates"] == 2
        assert len(json.loads((output / "SW.excluded_pairs.json").read_text())) == 1
        checks.append("dynamic time sort, bidirectional reference counts, whole inconsistent-pair removal")
        samples = prepare_samples("TS", output, root / "samples", [20, 40, 60, 80], seed=1)
        assert all(row["queries"] == 3 for row in samples)
        for row in samples:
            check(root / "samples", row["dataset"])
        checks.append("nested whole-edge vertex samples and all parent queries including initial empty queries")
        levels = nested_schedules([(3, "2"), (150, "3")], 200)
        assert [len(levels[x]) for x in (1, 4, 16)] == [2, 8, 32]
        assert set(levels[1]) <= set(levels[4]) <= set(levels[16])
        assert len({step for step, _ in levels[16]}) == 32
        checks.append("nested query count levels and collision resolution")
    print(json.dumps(dict(status="pass", checks=checks), indent=2))

if __name__ == "__main__":
    main()
