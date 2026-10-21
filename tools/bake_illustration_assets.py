#!/usr/bin/env python3
"""Bake the authored illustrations to game-size indexed color with printed-ink screening."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageEnhance, ImageFont, ImageOps

from pack_landscape_assets import encode

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"SC2KPIC1"


def PrintIllustration(image, size, strength, grain_strength):
  image = ImageOps.fit(image.convert("RGB"), size, method=Image.Resampling.LANCZOS)
  image = ImageEnhance.Color(image).enhance(.90)
  image = ImageEnhance.Contrast(image).enhance(.94)
  pixels = []
  for y in range(image.height):
    for x in range(image.width):
      rgb = image.getpixel((x, y))
      tone = sum(c * weight for c, weight in zip(rgb, (.2126, .7152, .0722))) / 255
      # Screen in final image pixels so downsampling cannot erase the print dots.
      u, v = (x + y) * .70710678 / 2.2, (y - x) * .70710678 / 2.2
      threshold = min(1, math.pi * ((u % 1 - .5) ** 2 + (v % 1 - .5) ** 2))
      coverage = max(0, min(1, (1 - tone - threshold) / .25 + .5))
      ink = tuple(a * coverage + b * (1 - coverage)
          for a, b in zip((34, 39, 43), (241, 238, 229)))
      grain = (((x * 17 + y * 61 + x * y * 3) % 23) / 22 - .5) * grain_strength
      pixels.append(tuple(round(max(0, min(255, c * (1 - strength) + i * strength + grain)))
          for c, i in zip(rgb, ink)))
  result = Image.new("RGB", size)
  result.putdata(pixels)
  return result.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)


def MakePreview(source, output, illustrations):
  width, row_height = 1020, 284
  preview = Image.new("RGB", (width, 68 + 5 * row_height), "#f1eee6")
  draw = ImageDraw.Draw(preview)
  font = ImageFont.load_default(size=15)
  draw.text((18, 12), "Color newspaper illustrations - native dimensions, enlarged 2x", font=font, fill="#26313b")
  draw.text((18, 37), "Lighter halftone dots and paper grain; color preserved in the newspaper renderer.",
      font=font, fill="#53616b")
  for index, item in enumerate(illustrations):
    x, y = 10 + index % 3 * 340, 68 + index // 3 * row_height
    image = Image.open(output / item["file"]).convert("RGB")
    image = image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST)
    preview.paste(image, (x + (320 - image.width) // 2, y + (206 - image.height) // 2))
    draw.text((x + 4, y + 214), f'{item["id"]}: {item["name"]}', font=font, fill="#26313b")
    draw.text((x + 4, y + 238), f'{item["size"][0]} x {item["size"][1]}, 256 colors',
        font=font, fill="#53616b")
  preview.save(output / "preview.png")
  # Side-by-side check of the resize and the final print finish at the same scale.
  comparison = Image.new("RGB", (980, 376), "#f1eee6")
  draw = ImageDraw.Draw(comparison)
  record = next(item for item in illustrations if item["id"] == 401)
  original = ImageOps.fit(Image.open(source / record["source"]), tuple(record["size"]),
      method=Image.Resampling.LANCZOS)
  baked = Image.open(output / record["file"]).convert("RGB")
  for x, name, image in ((15, "Resize only", original), (505, "Baked newspaper print", baked)):
    draw.text((x, 12), name, font=font, fill="#26313b")
    comparison.paste(image.resize((465, 300), Image.Resampling.NEAREST), (x, 42))
  comparison.save(output / "before-after.png")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source", type=Path, default=ROOT / "assets/ui/illustrations/rendered-v1")
  parser.add_argument("--output", type=Path, default=ROOT / "assets/ui/illustrations/baked")
  parser.add_argument("--strength", type=float, default=.14)
  parser.add_argument("--grain", type=float, default=1.2)
  args = parser.parse_args()
  if not 0 <= args.strength <= 1:
    parser.error("strength must be between 0 and 1")
  if not 0 <= args.grain <= 3:
    parser.error("grain must be between 0 and 3")
  records = json.loads((args.source / "manifest.json").read_text())["illustrations"]
  records = sorted(records, key=lambda item: item["id"])
  if [item["id"] for item in records] != [128, *range(400, 412)]:
    raise ValueError("expected the key and all twelve newspaper illustrations")
  args.output.mkdir(parents=True, exist_ok=True)
  packed = bytearray(struct.pack("<8sI", MAGIC, len(records)))
  illustrations = []
  source_bytes = 0
  for record in records:
    path = args.source / record["file"]
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != record["sha256"]:
      raise ValueError(f"source artwork changed without updating its manifest: {path}")
    source_bytes += len(raw)
    image = PrintIllustration(Image.open(path), tuple(record["original_size"]), args.strength, args.grain)
    palette = bytes(image.getpalette()[:768]).ljust(768, b"\0")
    payload = encode(image, water_animation=False)
    packed += struct.pack("<4HI", record["id"], image.width, image.height, 256, len(payload))
    packed += palette + payload
    filename = f'{record["id"]}.png'
    image.save(args.output / filename, optimize=True)
    illustrations.append({"id": record["id"], "name": record["name"], "source": record["file"],
        "file": filename, "size": list(image.size), "uses": record["uses"]})
  pack = args.output.parent / "illustrations.dat"
  pack.write_bytes(packed)
  manifest = {"source": "../rendered-v1", "pack": "../illustrations.dat",
      "filter": {"ink_strength": args.strength, "saturation": .90, "contrast": .94,
          "grain_strength": args.grain,
          "screen_period_pixels": 2.2, "screen_angle_degrees": 45, "colors": 256},
      "source_bytes": source_bytes, "pack_bytes": len(packed), "illustrations": illustrations}
  (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
  MakePreview(args.source, args.output, illustrations)
  print(f"Baked {len(records)} illustrations: {source_bytes:,} source bytes -> {len(packed):,} packaged bytes")
  print(pack)


if __name__ == "__main__":
  main()
