#!/usr/bin/env python3
"""Extract shared Imagegen smoke loops and register smoke-free plant layers offline."""
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

from pack_landscape_assets import CleanCivicAlpha

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/landscape/civic/power-smoke'


def ExtractFrames(name, spec):
  sheet = Image.open(DIRECTORY / f'{name}-source-sheet.png').convert('RGBA')
  boundaries = [np.arange(5) * size // 4 for size in sheet.size]
  if name == 'steam':
    # Generated rows are not exact quarters. Split at their transparent gutters
    # so a neighbouring cloud cannot leak into a frame's top or move its anchor.
    alpha = np.asarray(sheet.getchannel('A'))
    for axis, size in enumerate(sheet.size):
      coverage = (alpha > 64).sum(axis=axis)
      for part in range(1, 4):
        nominal = size * part // 4
        low, high = nominal - size // 16, nominal + size // 16
        gutters = np.flatnonzero(coverage[low:high] == 0) + low
        if not len(gutters): raise ValueError(f'Missing {name} gutter {axis}:{part}')
        boundaries[axis][part] = gutters[np.argmin(abs(gutters - nominal))]
  cells, origins = [], []
  for frame in range(spec['frames']):
    x, y = frame % 4, frame // 4
    cell = sheet.crop((boundaries[0][x], boundaries[1][y], boundaries[0][x + 1], boundaries[1][y + 1]))
    alpha = np.asarray(cell.getchannel('A'))
    ys, xs = np.where(alpha > 16)
    if not len(xs): raise ValueError(f'Empty {name} frame {frame}')
    bottom = ys.max() + 1
    weights = alpha[max(0, bottom - 6):bottom].sum(axis=0)
    if name == 'steam':
      # Register the broad outlet, not the changing wisps at its lowest tip.
      root = alpha[max(0, bottom - (bottom - ys.min()) // 4):bottom]
      weights = np.where(root > 64, root, 0).sum(axis=0)
    origin = (np.arange(cell.width) * weights).sum() / weights.sum()
    cells.append(cell)
    origins.append((origin, bottom, xs.min(), xs.max() + 1, ys.min()))
  width, height = spec['canvas']
  ax, ay = spec['anchors'][0 if name == 'smoke' else 1]
  scale = min(min(ax / max(1, ox - left), (width - ax) / max(1, right - ox),
      ay / (bottom - top)) for ox, bottom, left, right, top in origins)
  result = []
  for frame, (cell, (ox, bottom, _, _, _)) in enumerate(zip(cells, origins)):
    image = cell.convert('RGBa').transform((width, height), Image.Transform.AFFINE,
        (1 / scale, 0, ox - ax / scale, 0, 1 / scale, bottom - ay / scale),
        Image.Resampling.BICUBIC).convert('RGBA')
    pixels = np.asarray(image).copy()
    # Thin the outer billows without shifting the registered source or dimming
    # its dense center. Bake the falloff once; runtime uses ordinary sprite alpha.
    coverage = np.asarray(image.getchannel('A').filter(ImageFilter.GaussianBlur(6))) / 255.0
    density = coverage * coverage * (3 - 2 * coverage)
    pixels[:, :, 3] = np.rint(pixels[:, :, 3] * (.15 + .85 * density)).astype(np.uint8)
    pixels[pixels[:, :, 3] == 0] = 0
    image = Image.fromarray(pixels)
    image.save(DIRECTORY / f'{name}-{frame:02d}.png')
    result.append(image)
  return result


def RegisterCleaned(clean, original):
  """Keep the original site; register only the smoke-removal insert to its fixed pixels."""
  before = np.asarray(original)
  best, score = clean, float('inf')
  for dy in range(-4, 5):
    for dx in range(-4, 5):
      shifted = Image.new('RGBA', original.size)
      shifted.alpha_composite(clean, (dx, dy))
      after = np.asarray(shifted)
      keep = (before[450:, :, 3] > 192) & (after[450:, :, 3] > 192)
      error = np.abs(before[450:, :, :3].astype(float) - after[450:, :, :3])[keep].mean()
      if error < score: best, score = shifted, error
  return best


def PlantLayers(plant, frames, spec):
  directory = (DIRECTORY / plant['directory']).resolve()
  manifest = json.loads((directory / 'sprites.json').read_text())
  width, height = manifest['canvas']
  sheet = Image.open(directory / 'smoke-cleaned-source.png').convert('RGBa').resize(
      (width * 2, height * 2), Image.Resampling.LANCZOS).convert('RGBA')
  bases = []
  for view, emitters in enumerate(plant['views']):
    original = Image.open(directory / f'view-{view}.png').convert('RGBA')
    x, y = view % 2 * width, view // 2 * height
    clean = RegisterCleaned(sheet.crop((x, y, x + width, y + height)), original)
    mask = Image.new('L', original.size)
    draw = ImageDraw.Draw(mask)
    # Copy the complete cleaned mouth; a plume-width strip cuts across the oval rim.
    for emitter in emitters:
      x, y = emitter['x'], emitter['y']
      if emitter['type'] == 1:
        draw.rectangle((x - 56, y - 160, x + 130, y + 8), fill=255)
        draw.ellipse((x - 56, y - 24, x + 56, y + 18), fill=255)
      elif emitter['flipped']:
        draw.rectangle((x - 170, y - 160, x + 28, y + 4), fill=255)
        draw.ellipse((x - 28, y - 20, x + 28, y + 10), fill=255)
      else:
        draw.rectangle((x - 28, y - 160, x + 170, y + 4), fill=255)
        draw.ellipse((x - 28, y - 20, x + 28, y + 10), fill=255)
    base = CleanCivicAlpha(Image.composite(clean, original, mask))
    base.save(directory / f'smoke-base-{view}.png')
    if plant['kind'] == 203:
      # Bake each plume's visibility against the actual painted front rim.
      # The building remains a single sprite; no foreground strip is redrawn.
      pixels = np.asarray(base)
      ax, ay = spec['anchors'][1][0], spec['baselines'][1]
      for index, emitter in enumerate(emitters):
        x, y = emitter['x'], emitter['y']
        light = pixels[y - 10:y + 12, x - 2:x + 3, :3].astype(int).mean(axis=(1, 2))
        front_y = y - 10 + light.argmax()
        visibility = Image.new('L', base.size, 255)
        for column in range(x - ax, x - ax + spec['canvas'][0]):
          edge = round(front_y - 18 + 18 * np.sqrt(max(0, 1 - ((column - x) / 49) ** 2)))
          row = edge
          if abs(column - x) < 49:
            low, high = edge - 6, edge + 7
            light = pixels[low:high, column, :3].astype(int).sum(axis=1)
            row = low + np.flatnonzero(light >= light.max() * .95)[-1]
          visibility.paste(0, (column, row, column + 1, height))
        visibility.crop((x - ax, y - ay, x - ax + spec['canvas'][0], y - ay + spec['canvas'][1])).save(
            directory / f'steam-mask-{view}-{index}.png')
    manifest['views'][view]['file'] = f'smoke-base-{view}.png'
    bases.append(base)
  manifest['art_notes'] = 'Fixed painted plant with a smoke-free base and shared sixteen-frame smoke/steam overlays.'
  (directory / 'sprites.json').write_text(json.dumps(manifest, indent=2) + '\n')
  return bases


def Preview(plants, frames, spec, view):
  images = []
  emitters = spec['kinds'][1]['views'][view]
  left, top = min(p['x'] for p in emitters) - 70, min(p['y'] for p in emitters) - 140
  right, bottom = max(p['x'] for p in emitters) + 70, max(p['y'] for p in emitters) + 28
  width, height = right - left, bottom - top
  mouths = Image.new('RGBA', (width * 4, height * 4), (49, 55, 50, 255))
  for frame in range(spec['frames']):
    image = Image.new('RGBA', (1536, 640), (49, 55, 50, 255))
    for index, (plant, bases) in enumerate(plants):
      layer = bases[view].copy()
      for emitter, point in enumerate(plant['views'][view]):
        plume = frames[point['type']][(frame + emitter * 5) % spec['frames']]
        if point['flipped']: plume = plume.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if point['type'] == 1:
          directory = (DIRECTORY / plant['directory']).resolve()
          mask = Image.open(directory / f'steam-mask-{view}-{emitter}.png').convert('L')
          plume = plume.copy()
          plume.putalpha(ImageChops.multiply(plume.getchannel('A'), mask))
        ax = spec['anchors'][point['type']][0]
        ay = spec['baselines'][point['type']]
        if point['flipped']: ax = spec['canvas'][0] - ax
        layer.alpha_composite(plume, (point['x'] - ax, point['y'] - ay))
      if plant['kind'] == 203:
        mouths.alpha_composite(layer.crop((left, top, right, bottom)), (frame % 4 * width, frame // 4 * height))
      image.alpha_composite(layer, (index * 512, 0))
    images.append(image.convert('RGB'))
  filename = 'animation-preview' if view == 0 else f'preview-view-{view}'
  preview = ROOT / f'build/art-reference/power-smoke/{filename}.gif'
  preview.parent.mkdir(parents=True, exist_ok=True)
  images[0].save(preview.with_suffix('.png'))
  images[0].save(preview, save_all=True, append_images=images[1:], duration=1000 // spec['fps'], loop=0)
  mouths.convert('RGB').save(preview.with_name(f'mouths-view-{view}.png'))


def main():
  spec = json.loads((DIRECTORY / 'smoke.json').read_text())
  frames = [ExtractFrames(name, spec) for name in ('smoke', 'steam')]
  plants = [(plant, PlantLayers(plant, frames, spec)) for plant in spec['kinds']]
  for view in range(4): Preview(plants, frames, spec, view)
  print('Baked two shared sixteen-frame loops and twelve smoke-free plant views')


if __name__ == '__main__':
  main()
