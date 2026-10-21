"""Package Noto Sans coverage at native UI pixel sizes; no runtime font dependency.

Glyph advances remain those of the traditional font, so changing font mode does not
move controls. Alpha coverage is retained for AA and thresholded when AA is disabled.
The atlas uses byte runs on disk and one contiguous allocation in Assets at startup.
"""
from pathlib import Path
import struct
from itertools import groupby
from PIL import Image, ImageDraw, ImageFont
from build_interface_font import glyph

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "assets/fonts/noto/NotoSans-Regular.ttf"


def build():
    advances = [glyph(code, 1)[1] for code in range(256)]
    dimensions = bytearray()
    pixels = bytearray()
    for size in range(5, 56):
        font = ImageFont.truetype(str(FONT), size)
        for code, advance in enumerate(advances):
            width = (advance * size + 9) // 10 + 2
            height = (11 * size + 9) // 10
            character = bytes([code]).decode("cp1252", errors="replace")
            image = Image.new("L", (width, height))
            if not (code < 32 or 127 <= code <= 159):
                # Align the scalable typeface to the sprite font's baseline. Pillow's
                # default top anchor includes extra ascent space and clips descenders.
                baseline = (9 * size + 5) // 10
                left = min(0, font.getbbox(character, anchor="ls")[0])
                ImageDraw.Draw(image).text((-left, baseline), character, font=font, fill=255, anchor="ls")
            dimensions += bytes((width, height))
            pixels += image.tobytes()
    runs = bytearray()
    for value, group in groupby(pixels):
        count = sum(1 for _ in group)
        while count:
            length = min(count, 255)
            runs += bytes((length, value))
            count -= length
    header = b"SC2KAAF1" + struct.pack("<II", len(pixels), len(runs))
    output = ROOT / "assets/fonts/interface-coverage.dat"
    output.write_bytes(header + dimensions + runs)
    print(f"{output}: {len(runs)} encoded bytes, {len(pixels)} coverage bytes")


if __name__ == "__main__":
    build()
