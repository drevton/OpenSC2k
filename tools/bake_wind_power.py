#!/usr/bin/env python3
"""Bake painted turbine bodies and projected rotor frames; no runtime raster work."""
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops

from bake_civic_sprites import Foreground, GroundCorners

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/landscape/civic/200-wind-power'
ANIMATION = DIRECTORY / 'animation'
FORMS = ('old', 'modern')
FRAMES = 16
CANVAS = (128, 320)


def BakeBodies(form, prism):
  sheet = Image.open(ANIMATION / f'{form}-body-source.png').convert('RGBA')
  result = []
  for view in range(4):
    x, y = view % 2, view // 2
    source = sheet.crop((x * sheet.width // 2, y * sheet.height // 2,
        (x + 1) * sheet.width // 2, (y + 1) * sheet.height // 2))
    corners, slopes = GroundCorners(source, 0)
    body = Foreground(source, corners, slopes, CANVAS, CANVAS[1], 2)
    body.putalpha(ImageChops.multiply(body.getchannel('A'), prism))
    ground = Image.open(DIRECTORY / f'ground-{view}.png').convert('RGBA')
    body = Image.alpha_composite(ground, body)
    body.save(ANIMATION / f'{form}-base-{view}.png')
    result.append(body)
  return result


def ReadRotor(form):
  image = Image.open(ANIMATION / f'{form}-rotor-source.png').convert('RGBA')
  pixels = np.asarray(image)
  yy, xx = np.mgrid[:image.height, :image.width]
  red = (pixels[:, :, 0] > 160) & (pixels[:, :, 1] < 120) & (pixels[:, :, 2] < 120) & (pixels[:, :, 3] > 128)
  red &= (xx > image.width / 3) & (xx < image.width * 2 / 3) & (yy > image.height / 3) & (yy < image.height * 2 / 3)
  center = np.array((xx[red].mean(), yy[red].mean()))
  opaque = pixels[:, :, 3] > 128
  radius = np.sqrt((xx[opaque] - center[0]) ** 2 + (yy[opaque] - center[1]) ** 2).max()
  return image.convert('RGBa'), center, radius


def ProjectRotor(source, center, source_radius, hub, radius, view, frame, cycle):
  angle = math.radians(cycle * frame / FRAMES)
  rotation = np.array(((math.cos(angle), -math.sin(angle)), (math.sin(angle), math.cos(angle))))
  # Same vertical rotor plane in four rigid camera directions, never a flat
  # screen-space rotation of an already foreshortened ellipse.
  across = ((1, 1), (1, -1), (-1, -1), (-1, 1))[view]
  projection = np.array(((across[0] / math.sqrt(2), 0), (across[1] / math.sqrt(8), math.sqrt(3) / 2)))
  inverse = np.linalg.inv(projection @ rotation * radius / source_radius)
  offset = center - inverse @ hub
  coefficients = (inverse[0, 0] / 2, inverse[0, 1] / 2, offset[0],
      inverse[1, 0] / 2, inverse[1, 1] / 2, offset[1])
  return source.transform((CANVAS[0] * 2, CANVAS[1] * 2), Image.Transform.AFFINE,
      coefficients, Image.Resampling.BICUBIC).resize(CANVAS, Image.Resampling.LANCZOS).convert('RGBA')


def main():
  mask = np.asarray(Image.open(DIRECTORY / 'foundation-mask.png').convert('L'))
  last = np.where(mask != 0, np.arange(CANVAS[1])[:, None], -1).max(axis=0)
  prism = Image.fromarray(np.where(np.arange(CANVAS[1])[:, None] <= last, 255, 0).astype('uint8'))
  spec = json.loads((ANIMATION / 'wind.json').read_text())
  previews = [Image.new('RGBA', (128 * 4, 320 * 2), (49, 55, 50, 255)) for _ in range(FRAMES)]
  for form_index, form in enumerate(FORMS):
    bodies = BakeBodies(form, prism)
    source, center, source_radius = ReadRotor(form)
    hubs = spec['hubs'][form_index]
    print(form, 'hub positions:', hubs)
    for view, (body, hub) in enumerate(zip(bodies, hubs)):
      for frame in range(FRAMES):
        rotor = ProjectRotor(source, center, source_radius, hub, spec['radii'][form_index],
            view, frame, 90 if form == 'old' else 120)
        alpha = ImageChops.multiply(rotor.getchannel('A'), prism)
        if view >= 2:
          alpha = ImageChops.multiply(alpha, ImageChops.invert(body.getchannel('A')))
        rotor.putalpha(alpha)
        rotor.save(ANIMATION / f'{form}-rotor-{view}-{frame:02d}.png')
        previews[frame].alpha_composite(Image.alpha_composite(body, rotor), (view * 128, form_index * 320))
  output = ROOT / 'build/art-reference/wind-power'
  output.mkdir(parents=True, exist_ok=True)
  previews = [image.convert('RGB') for image in previews]
  previews[0].save(output / 'assembled.png')
  previews[0].save(output / 'animation.gif', save_all=True, append_images=previews[1:], duration=83, loop=0)
  print('Baked eight turbine bodies and 128 rotor frames')


if __name__ == '__main__':
  main()
