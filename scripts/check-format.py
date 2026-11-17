#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import argparse
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--formatter", default="clang-format")
parser.add_argument("--fix", action="store_true")
args = parser.parse_args()
files = sorted(p for directory in ("include", "src", "port", "tests", "config")
               for p in (root / directory).rglob("*") if p.suffix in (".c", ".h", ".cpp"))
subprocess.run([args.formatter, *( ["-i"] if args.fix else ["--dry-run", "--Werror"]),
                *map(str, files)], check=True)
print("format: PASS")
