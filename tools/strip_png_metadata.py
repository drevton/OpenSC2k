#!/usr/bin/env python3
"""Remove PNG text, EXIF and timestamp metadata without recompressing image data."""
import argparse
from pathlib import Path
import struct


PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'
METADATA_CHUNKS = {b'tEXt', b'zTXt', b'iTXt', b'eXIf', b'tIME'}


def StripPngMetadata(path):
  original = path.read_bytes()
  if not original.startswith(PNG_SIGNATURE):
    raise ValueError(f'Not a PNG: {path}')
  output = bytearray(PNG_SIGNATURE)
  at = len(PNG_SIGNATURE)
  while at < len(original):
    if at + 12 > len(original):
      raise ValueError(f'Truncated PNG chunk: {path}')
    size = struct.unpack_from('>I', original, at)[0]
    end = at + size + 12
    if end > len(original):
      raise ValueError(f'Truncated PNG payload: {path}')
    if original[at + 4:at + 8] not in METADATA_CHUNKS:
      output += original[at:end]
    at = end
  if output == original:
    return False
  path.write_bytes(output)
  return True


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('files', type=Path, nargs='+')
  args = parser.parse_args()
  changed = sum(StripPngMetadata(path) for path in args.files)
  print(f'Removed metadata from {changed} PNGs; compressed image data preserved')


if __name__ == '__main__':
  main()
