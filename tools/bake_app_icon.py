#!/usr/bin/env python3
"""Package the shared website emblem as a native macOS application icon."""
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/app/sources/city-mark.png"
OUTPUT = ROOT / "assets/app/OpenSC2K.icns"


def main():
  with Image.open(SOURCE) as image:
    icon = image.convert("RGBA").resize((1024, 1024), Image.Resampling.LANCZOS)
  OUTPUT.parent.mkdir(parents=True, exist_ok=True)
  icon.save(OUTPUT, format="ICNS")
  print(f"Packaged {OUTPUT.relative_to(ROOT)} from {SOURCE.relative_to(ROOT)}")


if __name__ == "__main__":
  main()
