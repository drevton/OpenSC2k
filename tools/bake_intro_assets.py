#!/usr/bin/env python3
"""Bake the shared emblem and smooth intro typography into immutable native artwork."""
from pathlib import Path
from pack_landscape_assets import picture_pack

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/ui/intro"
BRAND = ROOT / "assets/app/sources"
MUTED = "#fff1c9"
STAGES = ("Loading resources", "Loading graphics", "Preparing graphics", "Preparing sprites and previews")
CITY_ART = OUTPUT / "city-background.png"


def TextImage(text, size, color, serif=False, tracking=0, stroke=0):
  detail = 3
  face = BRAND / ("noto-serif.ttf" if serif else "noto-sans.ttf")
  font = ImageFont.truetype(str(face), size * detail)
  width = round(sum(font.getlength(c) for c in text) + tracking * detail * max(0, len(text) - 1))
  image = Image.new("RGBA", (width + 12 * detail, (size + 18) * detail))
  draw = ImageDraw.Draw(image)
  x = 6 * detail
  for c in text:
    draw.text((round(x), (size + 5) * detail), c, font=font, fill=color, anchor="ls",
        stroke_width=round(stroke * detail), stroke_fill=color)
    x += font.getlength(c) + tracking * detail
  return image.resize((round(image.width / detail), round(image.height / detail)), Image.Resampling.LANCZOS)


def main():
  OUTPUT.mkdir(parents=True, exist_ok=True)
  brand = Image.new("RGBA", (900, 170))
  with Image.open(BRAND / "city-mark.png") as source:
    logo = source.convert("RGBA").resize((160, 160), Image.Resampling.LANCZOS)
  brand.alpha_composite(logo, (0, 4))
  title = TextImage("OpenSC2k", 90, "#ffffff", serif=True, tracking=-.8, stroke=.5)
  brand.alpha_composite(title, (170, 0))
  caption = TextImage("OPEN SOURCE CITY SIMULATOR", 22, MUTED, tracking=1, stroke=.25)
  brand.alpha_composite(caption, (178, 118))
  bounds = brand.getbbox()
  brand = brand.crop((bounds[0], 0, bounds[2], brand.height))
  images = [TextImage(stage, 17, MUTED) for stage in STAGES]
  for c in "0123456789%":
    glyph = TextImage(c, 14, MUTED)
    images.append(glyph.crop((5, 0, glyph.width - 5, glyph.height)))
  with Image.open(CITY_ART) as source:
    background = source.convert("RGBA").resize((1024, 683), Image.Resampling.LANCZOS)
  # Keep fine city detail away from the lettering. The opaque heading field
  # fades into the panorama below the identity and is baked into the same image.
  heading = Image.new("RGBA", background.size)
  draw = ImageDraw.Draw(heading)
  for y in range(248):
    alpha = 250 if y < 182 else round(250 * (1 - (y - 182) / 66) ** 2)
    draw.line((0, y, background.width, y), fill=(18, 42, 34, alpha))
  background.alpha_composite(heading)
  shadow = Image.new("RGBA", brand.size, "#172b24")
  shadow.putalpha(brand.getchannel("A").filter(ImageFilter.GaussianBlur(3)))
  left = (background.width - brand.width) // 2
  background.alpha_composite(shadow, (left + 3, 15))
  background.alpha_composite(brand, (left, 12))
  background.save(OUTPUT / "background.png")
  images.append(background)
  packed = picture_pack(list(enumerate(images)))
  (OUTPUT / "intro.dat").write_bytes(packed)
  brand.save(OUTPUT / "brand.png")
  MakePreview(images)
  print(f"Baked {len(images)} intro images, {len(packed):,} bytes of immutable artwork")


def MakePreview(images):
  """Shared background and logo composition; development reference only."""
  width, height = 1200, 980
  city = images[15]
  scale = max(width / city.width, height / city.height)
  city = city.resize((round(city.width * scale), round(city.height * scale)), Image.Resampling.LANCZOS)
  preview = Image.new("RGBA", (width, height))
  preview.alpha_composite(city, ((width - city.width) // 2, 0))
  draw = ImageDraw.Draw(preview)
  bounds = (135, 492, 1065, 732)
  draw.rectangle(bounds, fill="#97835b", outline="#d3bb93", width=4)
  draw.line((1065, 492, 1065, 732, 135, 732), fill="#674f2b", width=4)
  label = images[3].resize((round(images[3].width * 1.5), round(images[3].height * 1.5)), Image.Resampling.LANCZOS)
  preview.alpha_composite(label, ((width - label.width) // 2, 540))
  draw = ImageDraw.Draw(preview)
  draw.rectangle((330, 615, 870, 621), fill="#67532b")
  draw.rectangle((330, 615, 535, 621), fill="#fcb030")
  label = TextImage("38%", 21, MUTED)
  preview.alpha_composite(label, ((width - label.width) // 2, 654))
  preview.convert("RGB").save(OUTPUT / "preview.png")


if __name__ == "__main__":
  main()
