#!/usr/bin/env python3
"""Bake original Blender vehicle renders into indexed sprites and 32 shared body palettes."""
from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1] / 'assets/landscape/vehicles'
# Color families are deliberately balanced; names keep future edits auditable.
PAINTS = [
    ('Royal blue', (60, 106, 175), False),
    ('Navy', (32, 49, 86), True),
    ('Steel blue', (70, 113, 151), True),
    ('Sky blue', (110, 159, 195), True),
    ('Petrol blue', (34, 97, 115), True),
    ('Cherry red', (178, 38, 39), False),
    ('Burgundy', (94, 31, 43), True),
    ('Brick red', (144, 57, 42), True),
    ('Cranberry', (139, 35, 60), True),
    ('Vermilion', (205, 66, 42), False),
    ('Forest green', (27, 70, 49), True),
    ('Racing green', (32, 92, 67), False),
    ('Olive', (87, 99, 58), True),
    ('Jade', (63, 129, 101), True),
    ('Sage', (112, 138, 107), True),
    ('Yellow', (215, 166, 40), False),
    ('Ochre', (181, 134, 42), True),
    ('Orange', (218, 113, 37), False),
    ('Burnt orange', (175, 79, 40), True),
    ('Gold', (169, 133, 76), True),
    ('Bronze', (117, 90, 70), True),
    ('Copper', (153, 89, 60), True),
    ('Sand', (190, 174, 146), True),
    ('Chocolate', (93, 62, 45), True),
    ('Champagne', (194, 181, 151), True),
    ('Pearl white', (232, 232, 225), True),
    ('Ivory', (216, 204, 178), True),
    ('Silver', (200, 204, 210), True),
    ('Titanium', (132, 139, 137), True),
    ('Graphite', (65, 69, 76), True),
    ('Charcoal', (40, 42, 44), True),
    ('Black', (24, 25, 27), False),
]
COLORS = [color for _, color, _ in PAINTS]
METALLIC = frozenset(style for style, (_, _, metallic) in enumerate(PAINTS) if metallic)
def BodyShades(style, color):
  shades = []
  for shade in range(32):
    level = .15 + .85 * shade / 31
    # Bright paint regions pick up neutral reflections; shadow colors retain
    # the body hue. This preserves the source highlights without adding noise.
    reflection = .20 * (max(0, shade - 22) / 9) ** 2 if style in METALLIC else 0
    shades.append(tuple(round(c * level * (1 - reflection) + 245 * reflection) for c in color))
  return shades

def ReviewSheet(directory):
  names = ['hatchback', 'sedan', 'wagon', 'van', 'pickup', 'suv', 'coupe']
  sheet = Image.new('RGB', (1260, 64 + 7*284), (204, 205, 202))
  draw = ImageDraw.Draw(sheet)
  font = ImageFont.load_default(size=22)
  draw.text((20, 12), 'Blender vehicles: front, side and rear review', fill=(35, 40, 43), font=font)
  for row, name in enumerate(names):
    y = 64+row*284
    draw.text((20, y), name.upper(), fill=(35, 40, 43), font=font)
    for column, view in enumerate(('front', 'side', 'rear')):
      source = Image.open(directory / f'{name}-{view}.png').convert('RGBA')
      image = Image.new('RGBA', source.size, (204, 205, 202, 255))
      image.alpha_composite(source)
      sheet.paste(image.convert('RGB').resize((420, 262), Image.Resampling.LANCZOS), (column*420, y+22))
  path = ROOT / 'blender/review.png'
  sheet.save(path)
  print(f'Large model review: {path}')


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--review-directory', type=Path, help='Assemble the separate large Blender review renders')
  args = parser.parse_args()
  if args.review_directory:
    ReviewSheet(args.review_directory)
    return
  source = ROOT / 'blender'
  manifest = json.loads((source / 'manifest.json').read_text())
  size = manifest['canvas']
  if size != 64 or manifest['ground_anchor'] != [32, 48] or manifest['pitch'] != 0:
    raise ValueError('Expected the registered 64px flat vehicle renders')
  # Scale around the registered wheel anchor, keeping the 64px canvas and
  # baseline unchanged so every heading stays centred on its existing lane.
  artwork_size = size * 3 // 4
  anchor_x, anchor_y = manifest['ground_anchor']
  artwork_offset = (anchor_x - anchor_x * 3 // 4, anchor_y - anchor_y * 3 // 4)
  frames = manifest['frames']
  models, views = len(manifest['models']), manifest['views']
  steering = len(manifest['steering_angles'])
  if len(frames) != models * steering * views: raise ValueError('Incomplete Blender heading set')
  images = []
  previous_partial = partial = 0
  for slot, frame in enumerate(frames):
    if (frame['model'] * steering + frame['steering']) * views + frame['view'] != slot: raise ValueError('Vehicle frames are out of order')
    path = source / frame['file']
    if hashlib.sha256(path.read_bytes()).hexdigest() != frame['sha256']:
      raise ValueError(f'Unregistered Blender render: {path}')
    source_image = Image.open(path).convert('RGBA')
    image = source_image.resize((artwork_size, artwork_size), Image.Resampling.LANCZOS)
    previous_partial += sum(image.getchannel('A').histogram()[5:250])
    # Average actual supersampled coverage: Lanczos would spread a faint halo
    # into neighbouring empty pixels. Keep that filter for colour detail only.
    # Snap the outer 9% of coverage to transparent/opaque, leaving antialiasing
    # only on genuinely mixed silhouette pixels, including mirrors and wheels.
    alpha = source_image.getchannel('A').resize((artwork_size, artwork_size), Image.Resampling.BOX)
    alpha = alpha.point(lambda value: 0 if value <= 24 else 255 if value >= 231 else value)
    partial += sum(alpha.histogram()[1:255])
    image.putalpha(alpha)
    canvas = Image.new('RGBA', (size, size))
    canvas.alpha_composite(image, artwork_offset)
    images.append(canvas)
  print(f'Vehicle coverage: {partial:,} partial-alpha pixels across {len(images)} frames '
      f'(previous filter: {previous_partial:,})')
  neutral = Image.new('RGB', (size * len(images), size))
  def body(r, g, b):
    return r > g * 1.5 and b > g * 1.3 and r > b * .8
  for slot, image in enumerate(images):
    neutral.paste(image.convert('RGB'), (slot * size, 0))
  data = list(neutral.getdata())
  neutral.putdata([(0, 0, 0) if body(*p) else p for p in data])
  quantized = neutral.quantize(colors=224, dither=Image.Dither.NONE)
  base = [tuple(quantized.getpalette()[i*3:i*3+3]) for i in range(224)]
  base.extend(BodyShades(0, COLORS[0]))
  # The shared packer maps exact RGB keys to indices; every key must be unique.
  seen = set()
  for i, color in enumerate(base):
    while color in seen:
      color = (color[0], (color[1] + (color[2] == 255)) % 256, (color[2] + 1) % 256)
    seen.add(color)
    base[i] = color
  key = Image.new('RGB', (256, 1)); key.putdata(base); key.save(ROOT / 'palette.png')
  palettes = []
  for style, color in enumerate(COLORS):
    palettes.extend(base[:224] + BodyShades(style, color))
  bank = Image.new('RGB', (256, len(COLORS))); bank.putdata(palettes); bank.save(ROOT / 'palettes.png')
  preview = Image.new('RGBA', (16 * 64, models * 2 * 64), (110, 112, 106, 255))
  directions_preview = Image.new('RGBA', (views * 96, models * 96), (110, 112, 106, 255))
  for slot, image in enumerate(images):
    rgb = image.convert('RGB').quantize(palette=quantized, dither=Image.Dither.NONE)
    pixels = []
    for p, index in zip(image.getdata(), rgb.getdata()):
      r, g, b, a = p
      if body(r, g, b): index = 224 + round(max(r, b) * 31 / 255)
      elif index >= 224: index = 0
      pixels.append((*base[index], a))
    encoded = Image.new('RGBA', image.size); encoded.putdata(pixels)
    encoded.save(ROOT / f'car-{slot:04d}.png')
    frame = frames[slot]
    model, turn, view = frame['model'], frame['steering'], frame['view']
    if turn == 0:
      directions_preview.alpha_composite(encoded.resize((96, 96), Image.Resampling.NEAREST),
          (view * 96, model * 96))
    if turn == 0 and view == views // 4:
      for style in range(len(COLORS)):
        colored = Image.new('RGBA', image.size)
        lookup = {c: i for i, c in enumerate(base)}
        colored.putdata([(*palettes[style*256+lookup[p[:3]]], p[3]) for p in pixels])
        preview.alpha_composite(colored.resize((64, 64), Image.Resampling.LANCZOS),
            (style % 16 * 64, (model * 2 + style // 16) * 64))
  preview.convert('RGB').save(ROOT / 'preview.png')
  directions_preview.convert('RGB').save(ROOT / 'directions.png')

if __name__ == '__main__': main()
