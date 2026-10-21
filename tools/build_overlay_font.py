"""Bake map-label glyphs from the bundled OpenTTD Sans, without platform fonts."""
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'assets/fonts/openttd/OpenTTD-Sans.ttf'
OUTPUT = ROOT / 'assets/fonts/overlay-font.dat'
SIZES = (12, 14, 16)
FIRST, COUNT, HEIGHT, ORIGIN = 32, 224, 32, 4


def Build():
  data = bytearray(struct.pack('<8s7I', b'SC2KFNT1', 1, 1252, FIRST, len(SIZES), COUNT, HEIGHT, 131))
  for size in SIZES:
    font = ImageFont.truetype(str(FONT), size, layout_engine=ImageFont.Layout.BASIC)
    baseline = font.getmetrics()[0] + 1
    for code in range(FIRST, FIRST + COUNT):
      character = bytes([code]).decode('cp1252', errors='replace')
      if character == '\ufffd': character = '?'
      left, top, right, bottom = font.getbbox(character, anchor='ls')
      if not (0 <= ORIGIN + left <= ORIGIN + right <= 32 and
          0 <= baseline + top <= baseline + bottom <= HEIGHT):
        raise ValueError(f'Overlay glyph does not fit: {size}px, byte {code}')
      image = Image.new('1', (32, HEIGHT))
      ImageDraw.Draw(image).text((ORIGIN, baseline), character, font=font, fill=1, anchor='ls')
      advance = round(font.getlength(character))
      data += struct.pack('<BBb', advance, advance, ORIGIN)
      for y in range(HEIGHT):
        row = sum(1 << x for x in range(32) if image.getpixel((x, y)))
        data += struct.pack('<I', row)
  OUTPUT.write_bytes(data)
  print(f'Baked {len(SIZES) * COUNT} OpenTTD Sans overlay glyphs: {len(data):,} bytes')


if __name__ == '__main__':
  Build()
