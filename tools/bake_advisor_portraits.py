#!/usr/bin/env python3
"""Bake the eight authored advisor portraits into a native indexed asset pack."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont, ImageOps

from pack_landscape_assets import encode


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/ui/advisors"


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--output", type=Path, default=SOURCE / "advisors.dat")
  args = parser.parse_args()
  manifest = json.loads((SOURCE / "manifest.json").read_text())
  records = manifest["advisors"]
  if [entry["id"] for entry in records] != list(range(197, 205)):
    raise ValueError("Expected the eight ordered advisor IDs 197..204")
  width, height = manifest["width"], manifest["height"]
  if (width, height) != (256, 328) or (manifest["logical_width"], manifest["logical_height"]) != (64, 82):
    raise ValueError("Advisor portraits must use 256x328 pixels for a 64x82 logical slot")
  packed = bytearray(struct.pack("<8sI", b"SC2KPIC1", len(records)))
  preview = Image.new("RGB", (1088, 760), "#edf0f2") if args.output == SOURCE / "advisors.dat" else None
  if preview:
    draw = ImageDraw.Draw(preview)
    font = ImageFont.load_default(size=16)
    (SOURCE / "baked").mkdir(exist_ok=True)
  for index, entry in enumerate(records):
    path = SOURCE / "sources" / entry["source"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
      raise ValueError(f"Update portrait provenance for changed artwork: {path}")
    original = Image.open(path).convert("RGB")
    image = ImageOps.fit(original, (width, height), method=Image.Resampling.LANCZOS)
    indexed = image.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    payload = encode(indexed, water_animation=False)
    packed += struct.pack("<4HI", entry["id"], width, height, 256, len(payload))
    packed += bytes(indexed.getpalette()[:768]).ljust(768, b"\0") + payload
    if preview:
      indexed.save(SOURCE / "baked" / f'{entry["id"]}.png', optimize=True)
      x, y = index % 4 * 272 + 8, index // 4 * 380 + 8
      preview.paste(indexed.convert("RGB"), (x, y))
      draw.text((x, y + 334), entry["name"].replace("-", " ").title(), font=font, fill="#283746")
      draw.text((x, y + 354), f'{width} x {height} baked pixels', font=font, fill="#536270")
  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_bytes(packed)
  if preview:
    preview.save(SOURCE / "preview.png")
  print(f"Baked eight advisor portraits: {len(packed):,} bytes: {args.output}")


if __name__ == "__main__":
  main()
