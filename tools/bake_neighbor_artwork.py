#!/usr/bin/env python3
"""Bake neighbor illustrations using landscape RunSprite encoding and individual 8-bit palettes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont

from pack_landscape_assets import encode


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/ui/neighbors"


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--output", type=Path, default=SOURCE / "neighbors.dat")
  args = parser.parse_args()
  manifest = json.loads((SOURCE / "manifest.json").read_text())
  stages = manifest["stages"]
  width, height = manifest["width"], manifest["height"]
  if [entry["id"] for entry in stages] != list(range(8)) or (width, height) != (512, 256):
    raise ValueError("Expected eight ordered 512x256 population stages")
  if (manifest["logical_width"], manifest["logical_height"]) != (128, 64):
    raise ValueError("Neighbor images must retain the original 128x64 logical size")
  # Sample one exact 2:1 diamond at pixel centers. Translations by half the
  # width/height share edges without gaps or overlap; never fit each silhouette.
  mask = bytearray()
  for y in range(height):
    reach = 2 * min(y, height - 1 - y) + 1
    margin = width // 2 - reach
    mask += bytes(margin) + bytes([255]) * (2 * reach) + bytes(margin)
  alpha = Image.frombytes("L", (width, height), bytes(mask))
  packed = bytearray(struct.pack("<8sI", b"SC2KPIC1", len(stages)))
  preview = Image.new("RGB", (1056, 1216), "#e9edf0") if args.output == SOURCE / "neighbors.dat" else None
  if preview:
    draw = ImageDraw.Draw(preview)
    font = ImageFont.load_default(size=18)
    (SOURCE / "baked").mkdir(exist_ok=True)
  labels = ["Empty city: population 0", "Village: below 2,000", "Town: 2,000-9,999",
      "City: 10,000-49,999", "Large city: 50,000-99,999", "Metropolis: 100,000-499,999",
      "Huge city: 500,000-999,999", "Megacity: 1,000,000+"]
  for entry in stages:
    path = SOURCE / "sources" / entry["source"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
      raise ValueError(f"Update provenance for changed neighbor artwork: {path}")
    original = Image.open(path).convert("RGBA")
    if original.width != original.height * 2 or original.getchannel("A").getextrema() != (255, 255):
      raise ValueError(f"Neighbor source must be an opaque 2:1 terrain painting: {path}")
    canvas = original.resize((width, height), Image.Resampling.LANCZOS)
    canvas.putalpha(alpha)
    if foreground := entry.get("foreground"):
      foreground_path = SOURCE / "sources" / foreground["source"]
      if hashlib.sha256(foreground_path.read_bytes()).hexdigest() != foreground["sha256"]:
        raise ValueError(f"Update provenance for changed neighbor foreground: {foreground_path}")
      layer = Image.open(foreground_path).convert("RGBA")
      if layer.size != original.size or layer.getchannel("A").getextrema() != (0, 255):
        raise ValueError(f"Neighbor foreground must retain its canvas and transparency: {foreground_path}")
      # Composite the extracted settlement over the shared empty terrain. Scale
      # only buildings, never the ground footprint; preserve their full alpha.
      canvas = Image.open(SOURCE / "sources" / stages[0]["source"]).convert("RGBA")
      canvas = canvas.resize((width, height), Image.Resampling.LANCZOS)
      canvas.putalpha(alpha)
      foreground_width = foreground["width"]
      if foreground_width <= 0 or foreground_width > width or foreground_width % 2:
        raise ValueError(f"Expected an even foreground width within the tile: {foreground_path}")
      layer = layer.resize((foreground_width, foreground_width // 2), Image.Resampling.LANCZOS)
      foreground_top = foreground.get("top", 0)
      if foreground_top < 0 or foreground_top + layer.height > height:
        raise ValueError(f"Neighbor foreground placement would clip its silhouette: {foreground_path}")
      canvas.alpha_composite(layer, ((width - foreground_width) // 2, foreground_top))
    # Each stage gets 255 colors plus transparent index zero. Palettes are baked
    # alongside their image, never shared with other stages or the game palette.
    # Maximum coverage preserves rare cottage/roof colors in the smallest stages
    # instead of spending almost the whole palette on the surrounding meadow.
    indexed = canvas.convert("RGB").quantize(colors=255, method=Image.Quantize.MAXCOVERAGE,
        dither=Image.Dither.NONE)
    pixels = bytes(value + 1 if alpha >= 128 else 0
        for value, alpha in zip(indexed.tobytes(), canvas.getchannel("A").tobytes()))
    palette = bytes(3) + bytes(indexed.getpalette()[:765]).ljust(765, b"\0")
    baked = Image.frombytes("P", (width, height), pixels)
    baked.putpalette(palette)
    baked.info["transparency"] = 0
    payload = encode(baked, water_animation=False)
    packed += struct.pack("<4HI", entry["id"], width, height, 256, len(payload)) + palette + payload
    if preview:
      baked.save(SOURCE / "baked" / entry["source"], transparency=0, optimize=True)
      index = entry["id"]
      x, y = index % 2 * 528 + 8, index // 2 * 304 + 8
      rgba = baked.convert("RGBA")
      preview.paste(rgba, (x, y), rgba)
      draw.text((x + 8, y + 266), labels[index], font=font, fill="#283746")
  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_bytes(packed)
  if preview:
    preview.save(SOURCE / "preview.png")
  print(f"Baked {len(stages)} neighbor illustrations: {len(packed):,} bytes: {args.output}")


if __name__ == "__main__":
  main()
