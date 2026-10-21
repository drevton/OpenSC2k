"""Bake newspaper typefaces into a compressed coverage atlas; no runtime font engine."""
from pathlib import Path
import struct
import zlib

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SIZES = (8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 56, 64, 72, 80, 96, 112, 128)
FACES = ("NotoSerif-Regular.ttf", "Anton-Regular.ttf", "Chomsky.otf")


def build():
    records = bytearray()
    pixels = bytearray()
    for name in FACES:
        for size in SIZES:
            font = ImageFont.truetype(str(ROOT / "assets/fonts/newspaper" / name), size)
            for code in range(256):
                text = bytes([code]).decode("cp1252", errors="replace")
                if code < 32 or text == "\ufffd":
                    text = "?"
                left, top, right, bottom = font.getbbox(text, anchor="ls")
                width, height = right - left, bottom - top
                image = Image.new("L", (max(1, width), max(1, height)))
                ImageDraw.Draw(image).text((-left, -top), text, font=font, fill=255, anchor="ls")
                records += struct.pack("<IBBhhH", len(pixels), width, height, left, top,
                                       round(font.getlength(text) * 64))
                if width and height:
                    pixels += image.tobytes()
    raw = records + pixels
    packed = zlib.compress(raw, 9)
    header = struct.pack("<8sIIII", b"SC2KNPF1", len(FACES), len(SIZES), len(raw), len(packed))
    (ROOT / "assets/fonts/newspaper-font.dat").write_bytes(header + bytes(SIZES) + packed)
    print(f"Newspaper font: {len(packed):,} compressed bytes; {len(pixels):,} coverage bytes")


if __name__ == "__main__":
    build()
