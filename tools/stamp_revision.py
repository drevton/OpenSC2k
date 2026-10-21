#!/usr/bin/env python3
"""Patch the reserved Git revision slot without recompiling source files."""
import argparse
import mmap
from pathlib import Path
import subprocess
import sys


MARKER = b"SC2K_GIT_REVISION_SLOT_V1:______"
CAPACITY = 256
MACH_O = {b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce",
    b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"}


def StampRevision(binary, source_root):
  revision = subprocess.check_output(
      ["git", "describe", "--always", "--tags", "--long", "--dirty"], cwd=source_root, text=True).strip()
  value = revision.encode("utf-8")
  if not value or b"\0" in value or len(value) >= CAPACITY:
    raise ValueError(f"Git description must fit in {CAPACITY - 1} UTF-8 bytes")
  payload = value.ljust(CAPACITY, b"\0")
  with binary.open("r+b") as stream, mmap.mmap(stream.fileno(), 0) as image:
    at = image.find(MARKER)
    if at < 0 or image.find(MARKER, at + len(MARKER)) >= 0:
      raise ValueError("Expected exactly one Git revision slot in the executable")
    at += len(MARKER)
    if at + CAPACITY > len(image) or b"\0" not in image[at:at + CAPACITY]:
      raise ValueError("Invalid reserved Git revision slot")
    changed = image[at:at + CAPACITY] != payload
    mach_o = image[:4] in MACH_O
    if changed:
      image[at:at + CAPACITY] = payload
      image.flush()
  if mach_o and sys.platform == "darwin":
    signed = not changed and subprocess.run(["codesign", "--verify", str(binary)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not signed:
      subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)
  print(f"Git revision: {revision}", flush=True)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("binary", type=Path)
  parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
  args = parser.parse_args()
  try:
    StampRevision(args.binary, args.source_root)
  except (OSError, ValueError, subprocess.CalledProcessError) as error:
    parser.exit(1, f"stamp_revision: {error}\n")


if __name__ == "__main__":
  main()
