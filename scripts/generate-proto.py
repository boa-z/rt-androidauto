#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regenerate the checked-in nanopb C profile without network access."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
with tempfile.TemporaryDirectory() as temp:
    output = Path(temp) if args.check else root / "generated"
    subprocess.run([sys.executable, "-m", "nanopb.generator.nanopb_generator",
                    "-I", str(root / "proto"), "-D", str(output),
                    str(root / "proto/aap.proto")], check=True)
    if args.check:
        for name in ("aap.pb.h", "aap.pb.c"):
            if (output / name).read_bytes() != (root / "generated" / name).read_bytes():
                raise SystemExit("Generated file differs: " + name)
print("nanopb generated sources: PASS")
