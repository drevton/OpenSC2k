#!/usr/bin/env python3
"""Pack pinned OpenGFX2 extra-zoom demolition frames into native sprite runs."""
import argparse
from pathlib import Path
import struct

from PIL import Image

from build_landscape_assets import LANDSCAPE_MAGIC, encode, fetch, transparent


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source", type=Path, required=True, help="upstream download cache")
  parser.add_argument("--output", type=Path, default=Path("assets/landscape/opengfx2/demolition.dat"))
  args = parser.parse_args()
  frames = []
  # OpenGFX2 spr3709/spr3725 template_animation_frame: 4x source pixels,
  # normal tile width 64. Our large-zoom tile is 32 pixels, hence source scale 8.
  for name, count, width, height, offset_x, baseline in (
      ("large", 16, 64, 96, -31, 85), ("small", 12, 32, 32, -16, 16)):
    sheet = Image.open(fetch(args.source, f"graphics/effects/256/explosion_{name}_32bpp.png"))
    for frame in range(count):
      left = (1 + frame * (width + 1)) * 4
      image = transparent(sheet.crop((left, 4, left + width * 4, (height + 1) * 4)))
      frames.append((image, baseline * 4, offset_x * 4))
  args.output.parent.mkdir(parents=True, exist_ok=True)
  with args.output.open("wb") as output:
    output.write(struct.pack("<8sI5I", LANDSCAPE_MAGIC, len(frames), 0, 0, 0, 0, 0))
    for index, (image, baseline, offset_x) in enumerate(frames):
      payload = encode(image)
      output.write(struct.pack("<5IiI", index, image.width, image.height, baseline, 8, offset_x, len(payload)))
      output.write(payload)
  print(f"{len(frames)} frames, {args.output.stat().st_size:,} bytes: {args.output}")


if __name__ == "__main__":
  main()
