"""Download the public inputs; verify archives before extracting data members."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import tarfile
import urllib.parse
import urllib.request
from data_catalog import RAW, select, source_for

def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def download(name, raw=RAW, max_bytes=2 * 1024**3):
    spec = source_for(name)
    raw = Path(raw)
    raw.mkdir(parents=True, exist_ok=True)
    archive = raw / spec["filename"]
    if not archive.exists():
        url = spec["url"]
        response = urllib.request.urlopen(url, timeout=120)
        first = response.read(8192)
        if spec["kind"] == "cornell" and not first.startswith(b"\x1f\x8b"):
            body = first + response.read(2 * 1024 * 1024)
            response.close()
            form = re.search(r'<form[^>]+action="([^"]+)"[^>]*>(.*?)</form>', body.decode("utf-8"), re.S)
            if not form or html.unescape(form.group(1)) != "https://drive.usercontent.google.com/download":
                raise RuntimeError("Download is not a gzip archive. Download from the dataset page and place the archive in the raw directory.")
            fields = {key: html.unescape(value) for key, value in re.findall(r'<input[^>]+name="([^"]+)"[^>]+value="([^"]*)"', form.group(2))}
            if fields.get("id") != spec["drive_id"]:
                raise RuntimeError("Unexpected public download identifier")
            response = urllib.request.urlopen(form.group(1) + "?" + urllib.parse.urlencode(fields), timeout=120)
            first = response.read(8192)
            if not first.startswith(b"\x1f\x8b"):
                response.close()
                raise RuntimeError("Confirmed download did not return a gzip archive")
        partial = archive.with_name(archive.name + ".partial")
        with response, partial.open("xb") as output:
            output.write(first)
            for block in iter(lambda: response.read(1024 * 1024), b""):
                if output.tell() + len(block) > max_bytes:
                    raise RuntimeError("Download exceeds --max-bytes")
                output.write(block)
        if sha256(partial) != spec["sha256"]:
            raise RuntimeError("Archive checksum differs from the published artifact input; partial file preserved for inspection")
        partial.replace(archive)
    if sha256(archive) != spec["sha256"]:
        raise RuntimeError(f"Checksum mismatch for {archive.name}; no preprocessing performed")
    if spec["kind"] == "cornell":
        folder = raw / spec["name"]
        folder.mkdir(exist_ok=True)
        with tarfile.open(archive, "r:gz") as bundle:
            for member in bundle:
                basename = Path(member.name).name
                if not member.isfile() or not basename.endswith(("-nverts.txt", "-simplices.txt", "-times.txt")):
                    continue
                target = folder / basename
                if target.exists():
                    with bundle.extractfile(member) as original:
                        expected = hashlib.sha256()
                        for block in iter(lambda: original.read(4 * 1024 * 1024), b""):
                            expected.update(block)
                    if sha256(target) != expected.hexdigest():
                        raise RuntimeError(f"Existing extracted member differs: {target.name}")
                else:
                    with bundle.extractfile(member) as original, target.open("xb") as output:
                        shutil.copyfileobj(original, output)
        members = {suffix: next(folder.glob("*-" + suffix + ".txt")) for suffix in ("nverts", "simplices", "times")}
        record = dict(source=spec["page"], archive=archive.name, archive_sha256=spec["sha256"],
                      files={key: dict(filename=value.name, sha256=sha256(value)) for key, value in members.items()})
        (folder / "source.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return archive

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datasets", nargs="+", default=["hyper"], help="DW MA AU GE SO TS4 TS7 TS EM HT WV BC AS SW, hyper, graphs, or all")
    parser.add_argument("--raw-dir", type=Path, default=RAW)
    parser.add_argument("--max-bytes", type=int, default=2 * 1024**3)
    args = parser.parse_args()
    seen = set()
    for name in select(args.datasets):
        key = source_for(name)["filename"]
        if key not in seen:
            print(download(name, args.raw_dir, args.max_bytes), flush=True)
            seen.add(key)

if __name__ == "__main__":
    main()
