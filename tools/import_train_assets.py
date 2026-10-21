#!/usr/bin/env python3
"""Import pinned OpenGFX2 train views offline; normal builds use checked-in PNGs."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from PIL import Image, ImageDraw

from build_landscape_assets import REVISION, fetch, transparent

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/landscape/world'
# Reuse retired creature-piece slots without expanding the compatibility catalog.
ENGINE_IDS = (374, 375, 376, 377, 378, 483, 484, 485)
COACH_IDS = (478, 479, 480, 481, 482, 486, 487, 488)
# OpenGFX2 template_vehicle_rail_8view: left, width, height, x/y origin offset.
VIEWS = ((1, 8, 23, -3, -13), (10, 22, 19, -15, -12),
    (33, 31, 15, -16, -9), (65, 22, 19, -7, -12),
    (88, 8, 23, -3, -13), (97, 22, 19, -15, -12),
    (120, 31, 15, -16, -9), (152, 22, 19, -7, -12))


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--source', type=Path, required=True, help='upstream download cache')
  args = parser.parse_args()
  sources = OUT / 'sources/opengfx2-trains'
  sources.mkdir(parents=True, exist_ok=True)
  manifest = dict(repository='https://github.com/OpenTTD/OpenGFX2', revision=REVISION,
      author='Richard Wheeler (Zephyris) and OpenGFX2 contributors', license='GPL-2.0',
      template='templates/zoom-sensitive.pnml: template_vehicle_rail_8view / template_vehicle_rail_4view',
      sources=[], records=[])
  preview = Image.new('RGB', (8 * 128, 2 * 112), (105, 111, 101))
  draw = ImageDraw.Draw(preview)
  for row, (name, source_row, ids) in enumerate((
      ('rail_engines_temperate_32bpp.png', 6, ENGINE_IDS), ('rail_wagons_32bpp.png', 0, COACH_IDS))):
    path = f'graphics/vehicles/256/{name}'
    source = fetch(args.source, path)
    shutil.copyfile(source, sources / name)
    manifest['sources'].append(dict(file=f'sources/opengfx2-trains/{name}', upstream=path,
        sha256=hashlib.sha256(source.read_bytes()).hexdigest(), row=source_row,
        subject='SH 125 diesel locomotive' if row == 0 else 'Temperate passenger coach'))
    sheet = Image.open(source)
    for heading, ident in enumerate(ids):
      # Coaches are symmetric and upstream supplies four views. Preserve the
      # template's origins, including the half-pixel horizontal-view correction.
      view = heading if row == 0 else heading % 4
      left, width, height, dx, dy = VIEWS[view]
      top = (1 + source_row * 24) * 4
      image = transparent(sheet.crop((left * 4, top, (left + width) * 4, top + height * 4)))
      image = image.resize((width * 2, height * 2), Image.Resampling.LANCZOS)
      canvas = Image.new('RGBA', (128, 96))
      x = 64 + dx * 2 + (1 if view % 4 == 2 else 0)
      canvas.alpha_composite(image, (x, 64 + dy * 2))
      canvas.save(OUT / f'{ident:04}.png', optimize=True)
      manifest['records'].append(dict(id=ident, file=f'../world/{ident:04}.png', baseline=64,
          scale_divisor=4, offset_x=0, civic=True))
      preview.paste(canvas, (heading * 128, row * 112 + 16), canvas)
      draw.text((heading * 128 + 4, row * 112 + 2), f'{ident}: {heading}', fill='white')
  (OUT / 'opengfx2-trains.json').write_text(json.dumps(manifest, indent=2) + '\n')
  preview.save(OUT / 'train-preview.png')
  catalog = OUT / 'catalog.json'
  records = {entry['id']: entry for entry in json.loads(catalog.read_text())}
  records.update({entry['id']: entry for entry in manifest['records']})
  catalog.write_text(json.dumps([records[k] for k in sorted(records)], indent=2) + '\n')
  provenance = OUT / 'provenance.json'
  origins = json.loads(provenance.read_text())
  for entry in manifest['records']:
    origins['slots'][str(entry['id'])] = \
        'OpenGFX2 SH 125 locomotive and passenger coach, GPL-2.0; see opengfx2-trains.json'
  origins['imports'] = {'opengfx2-trains': 'opengfx2-trains.json'}
  provenance.write_text(json.dumps(origins, indent=2) + '\n')
  print(f'Imported {len(manifest["records"])} registered train views from OpenGFX2 {REVISION}')


if __name__ == '__main__':
  main()
