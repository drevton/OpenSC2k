#!/usr/bin/env python3
"""Patch the reserved Git revision slot without recompiling source files."""
import argparse
import mmap
from pathlib import Path
import struct
import subprocess
import sys


MARKER = b"SC2K_GIT_REVISION_SLOT_V1:______"
CAPACITY = 256
MACH_O = {b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce",
    b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf", b"\xbe\xba\xfe\xca", b"\xbf\xba\xfe\xca"}


def StampRevision(binary, source_root):
  revision = subprocess.check_output(
      ["git", "describe", "--always", "--tags", "--long", "--dirty"], cwd=source_root, text=True).strip()
  value = revision.encode("utf-8")
  if not value or b"\0" in value or len(value) >= CAPACITY:
    raise ValueError(f"Git description must fit in {CAPACITY - 1} UTF-8 bytes")
  payload = value.ljust(CAPACITY, b"\0")
  with binary.open("r+b") as stream, mmap.mmap(stream.fileno(), 0) as image:
    # Universal Mach-O files contain an independent revision slot in each architecture.
    # Validate every slice before writing any of them; duplicate slots within a slice still fail.
    magic = image[:4]
    slices = [(0, len(image))]
    if magic in (b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf", b"\xbe\xba\xfe\xca", b"\xbf\xba\xfe\xca"):
      endian = ">" if magic[:1] == b"\xca" else "<"
      wide = magic in (b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca")
      if len(image) < 8:
        raise ValueError("Truncated universal executable header")
      count = struct.unpack_from(endian + "I", image, 4)[0]
      stride = 32 if wide else 20
      directory_end = 8 + count * stride
      if not count or directory_end > len(image):
        raise ValueError("Invalid universal executable directory")
      slices = []
      for i in range(count):
        offset, size = struct.unpack_from(endian + ("QQ" if wide else "II"), image, 16 + i * stride)
        if offset < directory_end or not size or offset + size > len(image):
          raise ValueError("Invalid universal executable slice")
        slices.append((offset, offset + size))
      ordered = sorted(slices)
      if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
        raise ValueError("Overlapping universal executable slices")
    slots = []
    for first, end in slices:
      at = image.find(MARKER, first, end)
      if at < 0 or image.find(MARKER, at + len(MARKER), end) >= 0:
        raise ValueError("Expected exactly one Git revision slot per executable architecture")
      at += len(MARKER)
      if at + CAPACITY > end or b"\0" not in image[at:at + CAPACITY]:
        raise ValueError("Invalid reserved Git revision slot")
      slots.append(at)
    changed = any(image[at:at + CAPACITY] != payload for at in slots)
    mach_o = magic in MACH_O
    if changed:
      for at in slots:
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
