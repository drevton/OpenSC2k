#!/usr/bin/env python3
"""Bake bounded underground artwork; runtime uses ordinary run-sprite drawing."""
import argparse
from pathlib import Path
import json

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from build_landscape_assets import (NATIVE_BAKED_UNDERWORLD_FIRST, NATIVE_UNDERWORLD_GROUND_FIRST,
    NATIVE_HIGHWAY_MERGE_FIRST, NATIVE_COPPER_MATERIAL, NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST,
    GROUND_CORNERS, GroundProjection128, ReadGroundBoundaries128, validate_footprint)
from pack_landscape_assets import SPRITE_CATALOG, ExportSpriteSources

WIDTH = 128
CORNERS = GROUND_CORNERS
GROUND_FIRST = NATIVE_BAKED_UNDERWORLD_FIRST
PIPE_FIRST = GROUND_FIRST + 20
FITTING_PIECE = 45
VERTICAL_DIAGONAL_FIRST = FITTING_PIECE + 1
PIPE_PIECES = VERTICAL_DIAGONAL_FIRST + 6
SUBWAY_FIRST = PIPE_FIRST + PIPE_PIECES
COVER_FIRST = SUBWAY_FIRST + 15 * 20
SELECTION_FIRST = COVER_FIRST + 2 * 20
FACE_FIRST = SELECTION_FIRST + 20
TUNNEL_FIRST = FACE_FIRST + 1
ENDS = np.array(((0, .5), (.5, 1), (1, .5), (.5, 0)))


def obsolete(ident):
  return (NATIVE_UNDERWORLD_GROUND_FIRST <= ident < NATIVE_HIGHWAY_MERGE_FIRST or
          ident == NATIVE_COPPER_MATERIAL or
          NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST <= ident < NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST + 2)


def height(shape, u, v):
  n, e, s, w = CORNERS[shape]
  return n * (1 - u) * (1 - v) + e * (1 - u) * v + s * u * v + w * u * (1 - v)


def projection(shape, detail=1, grade=1):
  return GroundProjection128(shape, detail, grade)


def project_path(path, elevations=0):
  u, v = np.array(path).T
  return np.stack((64 + 64 * (v - u), 64 + 32 * (u + v) - 16 * elevations), axis=1)


def base_path(mask, diagonal=0):
  ends = [side for side in range(4) if mask & (1 << side)]
  if len(ends) != 2:
    return [np.linspace(ENDS[side], (.5, .5), 33) for side in ends]
  a, d = ENDS[ends]
  b, c = a + ((.5, .5) - a) * 2 / 3, d + ((.5, .5) - d) * 2 / 3
  if diagonal & 1: b = a + (d - a) / 3
  if diagonal & 2: c = d + (a - d) / 3
  t = np.linspace(0, 1, 33)[:, None]
  return [(1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t ** 2 * c + t ** 3 * d]


def copper(paths, texture, cap=False):
  y, x = np.indices((WIDTH, WIDTH), dtype=float)
  x += .5; y += .5
  nearest = np.full(x.shape, 1e9)
  normal_x, normal_y = np.zeros_like(x), np.zeros_like(x)
  radius = 8.5
  for path in paths:
    for a, b in zip(path, path[1:]):
      dx, dy = b - a
      length = dx * dx + dy * dy
      t = np.clip(((x - a[0]) * dx + (y - a[1]) * dy) / length, 0, 1)
      px, py = x - a[0] - t * dx, y - a[1] - t * dy
      distance = px * px + py * py
      closer = distance < nearest
      normal_x[closer], normal_y[closer] = px[closer], py[closer]
      np.minimum(nearest, distance, out=nearest)
  # Include exact boundary samples consistently despite Bezier rounding error.
  body = nearest <= radius * radius + 1e-8
  slope = -(8 / 17) / (radius * np.sqrt(np.maximum(.000001, 1 - nearest / (radius * radius))))
  dx, dy = normal_x * slope, normal_y * slope
  light = np.clip((.45 * dx + .9 * dy + .9) / (1.35 * np.sqrt(1 + dx * dx + dy * dy)), 0, 1)
  shade, shine = .35 + .8 * light, light ** 12
  if cap:
    end = paths[0][-1]
    direction = end - paths[0][-2]; direction /= np.linalg.norm(direction)
    along = (x - end[0]) * direction[0] + (y - end[1]) * direction[1]
    across = ((x - end[0]) * direction[1] - (y - end[1]) * direction[0]) / radius
    face = (along / (radius * .35)) ** 2 + across ** 2
    body &= (along <= 0) | (face <= 1)
    visible = (face <= 1) & (along >= -radius * .35)
    shade[visible] = (.82 - .1 * across[visible]) * np.where(face[visible] > .85, .86, 1)
    shine[visible] = .025
  pixels = np.zeros((WIDTH, WIDTH, 4), dtype=np.uint8)
  # Cast the finished silhouette, including its trimmed cap. A wider untrimmed
  # centerline stroke creates round blobs at caps and repeated bumps at arm joins.
  shadow = np.zeros_like(body)
  shadow[3:] = body[:-3]
  pixels[shadow] = (67, 62, 65, 255)
  for channel, (base, specular) in enumerate(((204, 74), (125, 66), (78, 52))):
    pixels[body, channel] = np.clip(base * texture[body] * shade[body] + specular * shine[body], 0, 255)
  pixels[body, 3] = 255
  return Image.fromarray(pixels)


def baked_underworld_sprites(boundaries=None):
  result = {}
  if boundaries is None:
    boundaries = ReadGroundBoundaries128()
  projections = []
  for shape in range(20):
    u, v, area = projection(shape)
    # The master cap includes a front wall, while pipe/subway artwork follows
    # only its raised plane. Ground walls are added separately below.
    projections.append((u, v, np.roll(boundaries[0], -16, axis=0) if shape == 13 else boundaries[shape]))
  y, x = np.indices((WIDTH, WIDTH))
  flat_bottom = np.zeros((1, WIDTH), dtype=int)
  for column in range(WIDTH):
    rows = np.flatnonzero(boundaries[0][:, column])
    if rows.size:
      flat_bottom[0, column] = rows[-1] + 1

  def add(ident, image):
    if PIPE_FIRST <= ident < SUBWAY_FIRST:
      piece = ident - PIPE_FIRST
      if piece < 15 or 31 <= piece < 37 or piece >= FITTING_PIECE:
        area = projections[0][2]
      else:
        direction = (piece - 15) // 4 if piece < 31 else (piece - 37) // 2
        area = projections[direction + 1][2] if piece < 31 else projection(direction + 1, grade=.5)[2]
      pixels = np.array(image)
      pixels[~area] = 0
      image = Image.fromarray(pixels)
    area, solid = None, False
    if GROUND_FIRST <= ident < PIPE_FIRST:
      shape = (ident - GROUND_FIRST) % 20
      area = projections[shape][2].copy()
      for column in range(WIDTH):
        rows = np.flatnonzero(area[:, column])
        if rows.size:
          area[rows[-1] + 1:int(flat_bottom[0, column]), column] = True
      solid = True
    result[ident] = (image, area, solid)

  def indexed_image(pixels):
    image = Image.fromarray(pixels).convert('P')
    image.info['transparency'] = 0
    return image

  # One intensity bitmap per shape. The supplied material palette applies color;
  # texture, grid, slope lighting and face shadows remain baked into the indices.
  # Periodic random samples join at tile edges; a fixed seed keeps builds reproducible.
  grain_texture = np.random.default_rng(0x534F494C).uniform(-3, 3, (128, 128))
  for shape, (u, v, area) in enumerate(projections):
    n, e, s, w = CORNERS[shape]
    du, dv = w - n + (n - e - w + s) * v, e - n + (n - e - w + s) * u
    lighting = 8 * (dv - du) + 4 * (du + dv)
    lighting = np.where(lighting < 0, lighting * .35, lighting)
    gu, gv = u * 128, v * 128
    iu, iv = np.floor(gu).astype(int), np.floor(gv).astype(int)
    fu, fv = gu - iu, gv - iv
    grain = (grain_texture[iu % 128, iv % 128] * (1 - fu) * (1 - fv) +
             grain_texture[(iu + 1) % 128, iv % 128] * fu * (1 - fv) +
             grain_texture[iu % 128, (iv + 1) % 128] * (1 - fu) * fv +
             grain_texture[(iu + 1) % 128, (iv + 1) % 128] * fu * fv)
    intensity = np.clip(128 + lighting, 96, 152) * (128 + grain) / 128
    edge = np.minimum.reduce((u, v, 1 - u, 1 - v))
    intensity[edge < 1 / 64] *= .8
    pixels = np.zeros((WIDTH, WIDTH), dtype=np.uint8)
    pixels[area] = np.clip(np.rint(intensity[area]), 1, 255)
    for column in range(WIDTH):
      rows = np.flatnonzero(area[:, column])
      if rows.size:
        bottom = rows[-1] + 1
        pixels[bottom:int(flat_bottom[0, column]), column] = round(128 * (.6 if column < 64 else .72))
    add(GROUND_FIRST + shape, indexed_image(pixels))
  face = np.zeros((WIDTH, WIDTH), dtype=np.uint8)
  # The half-open ground diamond has empty outer columns: no pixel center
  # lies in its thin corner. A vertical wall still crosses those columns.
  # Project its front edge directly so adjacent height steps close at corners.
  wall_bottom = 128 - np.abs(x + .5 - 64) / 2
  mask = (y + .5 >= wall_bottom - 16) & (y + .5 < wall_bottom)
  face[mask & (x < 64)] = round(128 * .6)
  face[mask & (x >= 64)] = round(128 * .72)
  result[FACE_FIRST] = indexed_image(face), mask, True

  # Restore irregular hammered-copper mottling, entirely offline. Wrapped source
  # samples keep the material periodic at tile boundaries and height steps.
  grain = np.random.default_rng(0xC022E2).integers(0, 256, (16, 32), dtype=np.uint8)
  mottling = Image.fromarray(np.tile(grain, (3, 3))).filter(ImageFilter.GaussianBlur(2))
  mottling = np.array(mottling.crop((32, 16, 64, 32))).astype(float)
  mottling = (mottling - mottling.mean()) / mottling.std()
  copper_texture = (1 + np.tile(mottling, (8, 4)) * .065 +
                    np.tile((grain.astype(float) - 128) / 128, (8, 4)) * .025)

  # Flat catalogue: fifteen masks, including four solid capped endpoints.
  # Extend connected ends beyond the crop so the owning tile keeps the full
  # shaft and its displaced shadow near a shared edge, rather than a round tip.
  for mask in range(1, 16):
    paths = [project_path(path) for path in base_path(mask)]
    if len(paths) == 1 and mask.bit_count() == 2:
      path = paths[0]
      first = path[0] + (path[0] - path[1]) * 32
      last = path[-1] + (path[-1] - path[-2]) * 32
      paths = [np.vstack((first, path, last))]
    else:
      paths = [np.vstack((path[0] + (path[0] - path[1]) * 32, path)) for path in paths]
    add(PIPE_FIRST + mask - 1, copper(paths, copper_texture, mask.bit_count() == 1))

  # Ascending directions: north, west, south, east. Each half meets at h=grade/2,
  # with the constant incline's tangent; the outer end alone may ease to flat.
  for direction in range(4):
    axis, reverse = ((1, True), (0, True), (1, False), (0, False))[direction]
    for half in range(2):
      for grade in (1, .5):
        for eased in range(2 if grade == 1 else 1):
          t = np.linspace(-1, 2, 193)
          values = t.copy()
          if eased and half == 0:
            values = np.where(t < .5, 4 * np.maximum(t, 0) ** 2 - 4 * np.maximum(t, 0) ** 3, t)
          elif eased:
            q = np.maximum(1 - t, 0)
            values = np.where(t > .5, 1 - 4 * q ** 2 + 4 * q ** 3, t)
          path = np.full((len(t), 2), .5)
          path[:, axis] = 1 - t if reverse else t
          image = copper([project_path(path, values * grade)], copper_texture)
          midpoint = project_path([[.5, .5]], grade * .5)[0]
          unit = np.zeros(2); unit[axis] = -1 if reverse else 1
          tangent = np.array((64 * (unit[1] - unit[0]), 32 * sum(unit) - 16 * grade))
          cut = (x + .5 - midpoint[0]) * tangent[0] + (y + .5 - midpoint[1]) * tangent[1]
          pixels = np.array(image)
          pixels[(cut < 0) if half else (cut >= 0)] = 0
          ident = PIPE_FIRST + 15 + direction * 4 + half * 2 + eased if grade == 1 else \
              PIPE_FIRST + 37 + direction * 2 + half
          add(ident, Image.fromarray(pixels))

  for direction, mask in enumerate((9, 6, 3, 12)):
    for diagonal in range(1, 4):
      paths = [project_path(path) for path in base_path(mask, diagonal)]
      path = paths[0]
      paths = [np.vstack((path[0] + (path[0] - path[1]) * 32, path,
                         path[-1] + (path[-1] - path[-2]) * 32))]
      first = 31 + direction * 3 if direction < 2 else VERTICAL_DIAGONAL_FIRST + (direction - 2) * 3
      add(PIPE_FIRST + first + diagonal - 1, copper(paths, copper_texture))

  # A soft circular fitting joins composed slope arms without rectangular cross
  # stubs or a dark spherical rim. Coverage blending is baked into its alpha.
  fitting = np.array(result[PIPE_FIRST + 14][0])
  distance = np.sqrt((x + .5 - 64) ** 2 + (y + .5 - 96) ** 2)
  fitting[:, :, 3] = np.rint(np.clip((9.5 - distance) / 4, 0, 1) * 255)
  fitting[fitting[:, :, 3] == 0] = 0
  add(PIPE_FIRST + FITTING_PIECE, Image.fromarray(fitting))

  for shape, (u, v, area) in enumerate(projections):
    detail = 4
    su, sv, sa = projection(shape, detail)
    edge = np.minimum.reduce((su, sv, 1 - su, 1 - sv))
    coverage = (sa & (edge >= 1 / 64) & (edge < 1 / 64 + 1 / 16)).reshape(
        WIDTH, detail, WIDTH, detail).mean(axis=(1, 3))
    selection = Image.fromarray(np.rint(coverage * 15).astype(np.uint8)).convert('P')
    selection.info['transparency'] = 0
    add(SELECTION_FIRST + shape, selection)
    for mask in range(1, 16):
      image = Image.new('RGBA', (WIDTH, WIDTH))
      draw = ImageDraw.Draw(image)
      for path in base_path(mask):
        projected = project_path(path, height(shape, path[:, 0], path[:, 1]))
        points = [tuple(point) for point in projected]
        for color, width in (((47, 49, 54), 15), ((112, 116, 124), 11), ((52, 56, 63), 5)):
          draw.line(points, fill=(*color, 255), width=width)
      pixels = np.array(image); pixels[~area] = 0
      add(SUBWAY_FIRST + (mask - 1) * 20 + shape, Image.fromarray(pixels))
    for silo in range(2):
      image = Image.new('RGBA', (WIDTH, WIDTH)); draw = ImageDraw.Draw(image)
      points = project_path(np.array(((.2, .2), (.2, .8), (.8, .8), (.8, .2))),
                            height(shape, np.array((.2, .2, .8, .8)), np.array((.2, .8, .8, .2))))
      draw.polygon([tuple(point) for point in points], fill=(112, 116, 121, 255), outline=(40, 45, 52, 255))
      center = project_path([[.5, .5]], height(shape, .5, .5))[0]
      cx, cy = center
      draw.ellipse((cx - 7, cy - 4, cx + 7, cy + 4), fill=(51, 57, 62, 255))
      if silo: draw.line((cx - 5, cy - 3, cx + 5, cy + 3), fill=(180, 183, 155, 255), width=2)
      pixels = np.array(image); pixels[~area] = 0
      add(COVER_FIRST + silo * 20 + shape, Image.fromarray(pixels))
  for axis, mask in enumerate((5, 10)):
    image = Image.new('RGBA', (WIDTH, WIDTH)); draw = ImageDraw.Draw(image)
    path = project_path(base_path(mask)[0])
    for color, width in (((37, 39, 43), 23), ((83, 87, 94), 17), ((45, 48, 53), 9)):
      draw.line([tuple(point) for point in path], fill=(*color, 255), width=width)
    add(TUNNEL_FIRST + axis, image)
  return result


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--catalog', type=Path, default=SPRITE_CATALOG)
  parser.add_argument('--preview', type=Path)
  args = parser.parse_args()
  generated = baked_underworld_sprites()
  retained = [entry for entry in json.loads(args.catalog.read_text())
      if not obsolete(entry["id"]) and not GROUND_FIRST <= entry["id"] < TUNNEL_FIRST + 2]
  records = {}
  for ident, (image, allowed, solid) in generated.items():
    if allowed is not None:
      validate_footprint(ident, image, allowed, solid)
    records[ident] = image, WIDTH, 4, 0
  ExportSpriteSources(args.catalog, records, retained=retained)
  if args.preview:
    sheet = Image.new('RGB', (8 * 144, ((PIPE_PIECES + 7) // 8) * 152), (95, 97, 88))
    draw = ImageDraw.Draw(sheet)
    ground = generated[GROUND_FIRST][0].copy()
    ground.putpalette([min(255, (base * index + 64) // 128)
                       for index in range(256) for base in (113, 115, 106)])
    ground = ground.convert('RGBA')
    for index in range(PIPE_PIECES):
      px, py = index % 8 * 144, index // 8 * 152
      sheet.paste(ground, (px, py), ground)
      image = generated[PIPE_FIRST + index][0]
      sheet.paste(image, (px, py), image)
      draw.text((px + 4, py + 130), str(index), fill='white')
    sheet.save(args.preview)
  print(f'Exported {len(generated)} underground PNGs: {args.catalog}')


if __name__ == '__main__':
  main()
