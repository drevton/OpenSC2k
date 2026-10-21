"""Package OpenGFX2's normal-font rasterization at 1x, 2x and 4x.

Match graphics/fonts/charactergrab.py: 10px OpenTTD Sans, monochrome ink,
embedded one-pixel shadow and baseline stabilization with a trailing i.
Runtime loads pixels only; Pillow is an offline asset-build dependency.
"""
from pathlib import Path
import struct
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "assets/fonts/openttd/OpenTTD-Sans.ttf"


def glyph(code, scale):
    character = chr(58029) if code == 256 else bytes([code]).decode("cp1252", errors="replace")
    if code < 32 or character == '\ufffd':
        character = '?'
    font = ImageFont.truetype(str(FONT), 10 * scale)
    image = Image.new("RGBA", (20 * scale, 11 * scale))
    draw = ImageDraw.Draw(image)
    draw.fontmode = "1"
    draw.text((scale, scale), character + "                i", font=font, fill=(32, 32, 32, 255))
    draw.text((0, 0), character + "                i", font=font, fill=(16, 16, 16, 255))
    bounds = image.getbbox()
    width = bounds[2] if bounds else (3 if code in (32, 160) else 1) * scale
    if 0 <= ord(character) <= 31 or 127 <= ord(character) <= 159:
        image = Image.new("RGBA", image.size)
        width = scale
    return image, width


def build():
    base_widths = [glyph(code, 1)[1] for code in range(257)]
    output = bytearray(b"SC2KUIF3")
    for scale in (1, 2, 4):
        for code, advance in enumerate(base_widths):
            image, _ = glyph(code, scale)
            # OpenGFX2 alternative sprites use the 1x NML rectangle scaled up.
            width, height = advance * scale, 11 * scale
            assert width <= 64
            output += struct.pack('<BBB', width, height, advance)
            for y in range(height):
                ink = shadow = 0
                for x in range(width):
                    pixel = image.getpixel((x, y))
                    if pixel == (16, 16, 16, 255):
                        ink |= 1 << x
                    elif pixel == (32, 32, 32, 255):
                        shadow |= 1 << x
                output += struct.pack('<QQ', ink, shadow)
    (ROOT / "assets/fonts/interface-font.dat").write_bytes(output)


if __name__ == '__main__':
    build()
