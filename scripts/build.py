#!/usr/bin/env python3
"""Build the C++ programs with GCC or Clang; no Python packages required."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main() -> int:
    targets = {}
    for name in ("src", "baselines"):
        targets.update(json.loads((ROOT / name / "build_targets.json").read_text()))
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("targets", nargs="*", help="Target names; default: all C++ targets")
    p.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    p.add_argument("--list", action="store_true")
    a = p.parse_args()
    if a.list:
        print("\n".join(targets)); return 0
    chosen = a.targets or list(targets)
    if set(chosen) - targets.keys():
        p.error("Unknown target(s): " + ", ".join(sorted(set(chosen) - targets.keys())))
    cxx = shutil.which(a.cxx)
    if not cxx:
        p.error("Compiler not found. Install GCC/Clang and zlib development headers, or pass --cxx.")
    (ROOT / "build").mkdir(exist_ok=True)
    version = subprocess.check_output([cxx, "--version"], text=True).splitlines()[0]
    built = {}
    for name in chosen:
        spec = targets[name]
        dest = "build/" + name + (".exe" if os.name == "nt" else "")
        command = [cxx, "-std=c++20", "-O3", "-DNDEBUG"]
        command += ["-I" + x for x in spec.get("include_dirs", [])]
        command += ["-D" + x for x in spec.get("defines", [])]
        command += spec["sources"] + ["-o", dest]
        command += ["-l" + x for x in spec.get("libraries", [])]
        if os.name == "nt":
            command += ["-lpsapi"]
        print("Building " + name, flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
        built[name] = {"executable": dest, "compiler": version, "command": command}
    path = ROOT / "build/build.json"
    previous = json.loads(path.read_text()) if path.exists() else {}
    previous.update(built)
    path.write_text(json.dumps(previous, indent=2) + "\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
