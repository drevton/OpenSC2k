"""Embed the bundled OpenGFX2 cursor source pixels; requires Pillow only at generation."""
from pathlib import Path
from PIL import Image
import struct

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets/ui/opengfx2/cursors"


def transparent(path):
    image = Image.open(path).convert("RGBA")
    image.putdata([(0, 0, 0, 0) if pixel[:3] == (0, 0, 255) else pixel
                   for pixel in image.get_flattened_data()])
    return image


badge = Image.new("RGBA", (72, 72))
badge.paste(transparent(ASSETS / "classic_yellow_icon.png"), (0, 0))
arrow = Image.new("RGBA", (72, 72))
arrow.paste(transparent(ASSETS / "windows_white_cursor.png"), (0, 0))
default = Image.new("RGBA", (72, 72))
default.paste(transparent(ASSETS / "classic_yellow_cursor.png"), (0, 0))
sheet = transparent(ASSETS / "sc2k-cursor-icons-2x.png")
# Corresponding 20px cells in upstream graphics/cursors/cursoroverlay.py.
cells = [(3,9),(5,14),(3,2),(1,2),(2,2),(5,2),(4,6),(4,15),(7,7),(6,7),
         (8,6),(2,7),(6,0),(7,6),(4,5),(3,6),(6,6),(8,6),(2,8),(7,8),
         (5,13),(8,8),(8,0),(0,2)]
icons = [sheet.crop((int((x*21+1)*2), (y*21+1)*2,
                     int((x*21+1)*2)+40, (y*21+1)*2+40)) for x,y in cells]
# Data layout is documented in src/assets/cursors.h.
images = [badge, arrow, default, *icons]
pixels = [(a << 24) | (r << 16) | (g << 8) | b
          for image in images for r, g, b, a in image.get_flattened_data()]
header = struct.pack("<8s5I", b"SC2KCUR1", 72, 72, 40, 40, len(icons))
(ASSETS / "canvas-cursors.dat").write_bytes(header + struct.pack(f"<{len(pixels)}I", *pixels))
