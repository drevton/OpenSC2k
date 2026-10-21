#!/usr/bin/env python3
"""Regenerate checked-in OpenGFX2 landscape PNGs offline (Pillow, numpy, blend_modes).

Source artwork and tree processing are pinned to the revision below. Runtime
needs only landscape.dat, never PNGs, upstream tools or the original executable.
"""
import argparse
import importlib
import json
from pathlib import Path
import re
import struct
import sys
import urllib.request

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from pack_landscape_assets import (RUN_PAYLOAD_BYTES, ANIMATION_WATER_FIRST, LANDSCAPE_MAGIC,
    row_runs, encode, ExportSpriteSources, SPRITE_CATALOG, WATER_COLORS, WATER_GLITTER_COLORS, WATER_PALETTE_COLORS)

REVISION = "7a17fb370440aed7f2586715d606e9fe190b9dd3"
SLOPE_CORNERS = (0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 15, 15)
GROUND_TILE_WIDTH = 128
GROUND_BOUNDARIES = Path(__file__).resolve().parents[1] / "assets/landscape/opengfx2/detail128/terrain-masks.png"
GROUND_CORNERS = tuple(tuple(int(mask & (1 << corner) != 0) for corner in range(4))
    for mask in (*SLOPE_CORNERS[:14], 5, 10)) + ((0, 1, 2, 1), (1, 0, 1, 2), (2, 1, 0, 1), (1, 2, 1, 0))
# OpenTTD templates: x, height, y offset at its normal 64-pixel tile resolution.
NATIVE_GROUND_FIRST = 542
NATIVE_ROUGH_FIRST = NATIVE_GROUND_FIRST + 36 * 14
NATIVE_SHORE_FIRST = NATIVE_ROUGH_FIRST + 16
NATIVE_GRASS_BLEND_FIRST = NATIVE_SHORE_FIRST + 8 * 20
NATIVE_GRASS_BLEND_SHORE_FIRST = NATIVE_GRASS_BLEND_FIRST + 3 * 7 * 14
NATIVE_SNOW_GRASS_FIRST = NATIVE_GRASS_BLEND_SHORE_FIRST + 3 * 20
NATIVE_SNOW_GRASS_SHORE_FIRST = NATIVE_SNOW_GRASS_FIRST + 2 * 2 * 14
NATIVE_TREE_FIRST = NATIVE_SNOW_GRASS_SHORE_FIRST + 2 * 20
NATIVE_POINT = NATIVE_TREE_FIRST + 62 * 7
NATIVE_RAPIDS_FIRST = NATIVE_POINT + 3
NATIVE_RIVER_SIDE_FIRST = NATIVE_RAPIDS_FIRST + 4
NATIVE_RIVER_BANK_FIRST = NATIVE_RIVER_SIDE_FIRST + 16 * 4 * 2
NATIVE_COAST_FIRST = NATIVE_RIVER_BANK_FIRST + 16 * 12
NATIVE_SLOPED_WATER_FIRST = NATIVE_COAST_FIRST + 16 * 14
NATIVE_TERRAIN_FACE_FIRST = NATIVE_SLOPED_WATER_FIRST + 14
NATIVE_LEVEL_FOUNDATION_FIRST = NATIVE_TERRAIN_FACE_FIRST + 14
NATIVE_INCLINED_FOUNDATION_FIRST = NATIVE_LEVEL_FOUNDATION_FIRST + 4 * 14
NATIVE_UNDERWORLD_GROUND_FIRST = NATIVE_INCLINED_FOUNDATION_FIRST + 4 * 8
NATIVE_UNDERWORLD_PIPE_FIRST = NATIVE_UNDERWORLD_GROUND_FIRST + 4 * 2 * 14
NATIVE_UNDERWORLD_SUBWAY_FIRST = NATIVE_UNDERWORLD_PIPE_FIRST + 15 * 14
NATIVE_UNDERWORLD_STATION_FIRST = NATIVE_UNDERWORLD_SUBWAY_FIRST + 15 * 14
NATIVE_UNDERWORLD_SILO_FIRST = NATIVE_UNDERWORLD_STATION_FIRST + 14
NATIVE_UNDERWORLD_FACE_FIRST = NATIVE_UNDERWORLD_SILO_FIRST + 14
NATIVE_UNDERWORLD_SELECTION_FIRST = NATIVE_UNDERWORLD_FACE_FIRST + 8
NATIVE_HIGHWAY_MERGE_FIRST = NATIVE_UNDERWORLD_SELECTION_FIRST + 14
NATIVE_COPPER_MATERIAL = NATIVE_HIGHWAY_MERGE_FIRST + 8 * 3
NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST = NATIVE_COPPER_MATERIAL + 1
NATIVE_DIAGONAL_ROAD_FIRST = NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST + 2
NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST = NATIVE_DIAGONAL_ROAD_FIRST + 12
NATIVE_DIAGONAL_ROAD_SHARED_FIRST = NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST + 16
NATIVE_DIAGONAL_RAIL_FIRST = NATIVE_DIAGONAL_ROAD_SHARED_FIRST + 8
NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST = NATIVE_DIAGONAL_RAIL_FIRST + 12
NATIVE_DIAGONAL_RAIL_SHARED_FIRST = NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST + 16
NATIVE_BAKED_UNDERWORLD_FIRST = NATIVE_DIAGONAL_RAIL_SHARED_FIRST + 8
NATIVE_HIGHWAY_SLOPE_FIRST = 4447  # Four compact slopes follow the road/rail bridge heads.
NATIVE_DIAGONAL_HIGHWAY_FIRST = NATIVE_HIGHWAY_SLOPE_FIRST + 4
NATIVE_SIMCITY_SHORE_FIRST = NATIVE_GROUND_FIRST + 32 * 14
NATIVE_SIMCITY_SHORE_EXTRA = NATIVE_GROUND_FIRST + 34 * 14
UNDERGROUND_FIRST = 305
UNDERGROUND_LAST = 353

GROUND = ((1, 32, 0), (81, 32, 0), (161, 24, 0), (241, 24, 0),
          (321, 32, 0), (399, 32, 0), (479, 24, 0), (559, 24, 0),
          (639, 40, -8), (719, 40, -8), (799, 32, -8), (879, 32, -8),
          (959, 40, -8), (1039, 40, -8), (1119, 32, -8))


def fetch(root, path, binary=True):
  target = root / path
  if not target.exists():
    target.parent.mkdir(parents=True, exist_ok=True)
    base = "https://media.githubusercontent.com/media" if binary else "https://raw.githubusercontent.com"
    urllib.request.urlretrieve(f"{base}/OpenTTD/OpenGFX2/{REVISION}/{path}", target)
  return target


def transparent(image, white=False):
  pixels = np.array(image.convert("RGBA"))
  keyed = np.all(pixels[:, :, :3] == (0, 0, 255), axis=2)
  if white:
    keyed |= np.all(pixels[:, :, :3] == (255, 255, 255), axis=2)
  pixels[keyed, 3] = 0
  return Image.fromarray(pixels)


def GroundProjection128(shape, detail=1, grade=1):
  """One pixel-center projection for every surface and underground boundary.

  At native detail the diamond advances (+/-64, +32), with a 16px height step.
  Half-open map coordinates give adjacent tiles exclusive pixel ownership.
  Color artwork resolution never participates in this boundary definition.
  """
  width = GROUND_TILE_WIDTH
  y, x = np.indices((width * detail, width * detail), dtype=float)
  x = (x + .5) / detail; y = (y + .5) / detail
  n, e, south, w = GROUND_CORNERS[shape]
  low, high = y.copy(), y + 32
  for _ in range(24):
    middle = (low + high) / 2
    u = (middle - 64) / 64 - (x - 64) / 128
    v = (middle - 64) / 64 + (x - 64) / 128
    height = n * (1 - u) * (1 - v) + e * (1 - u) * v + south * u * v + w * u * (1 - v)
    below = middle - 16 * grade * height < y
    low, high = np.where(below, middle, low), np.where(below, high, middle)
  middle = (low + high) / 2
  u = (middle - 64) / 64 - (x - 64) / 128
  v = (middle - 64) / 64 + (x - 64) / 128
  return u, v, (u >= 0) & (u < 1) & (v >= 0) & (v < 1)


def MasterGroundBoundaries128():
  areas = [GroundProjection128(shape)[2] for shape in range(len(GROUND_CORNERS))]
  # The raised flat SC2 cap includes its exposed front wall. Other surface
  # slopes use separate foundation sprites; their masks contain only the plane.
  for x in range(GROUND_TILE_WIDTH):
    flat = np.flatnonzero(areas[0][:, x])
    raised = np.flatnonzero(areas[13][:, x])
    if flat.size and raised.size:
      areas[13][raised[-1] + 1:flat[-1] + 1, x] = True
  return areas


def WriteGroundBoundaries128(path, areas):
  sheet = np.zeros((GROUND_TILE_WIDTH, len(areas) * GROUND_TILE_WIDTH, 4), dtype=np.uint8)
  for shape, allowed in enumerate(areas):
    sheet[:, shape * GROUND_TILE_WIDTH:(shape + 1) * GROUND_TILE_WIDTH][allowed] = 255
  path.parent.mkdir(parents=True, exist_ok=True)
  Image.fromarray(sheet).save(path)


def ReadGroundBoundaries128(path=GROUND_BOUNDARIES):
  pixels = np.array(Image.open(path).convert("RGBA"))
  width = GROUND_TILE_WIDTH
  if pixels.shape != (width, len(GROUND_CORNERS) * width, 4) or \
      np.any((pixels[:, :, 3] != 0) & (pixels[:, :, 3] != 255)):
    raise ValueError(f"{path}: expected binary 128px ground boundaries")
  return [pixels[:, shape * width:(shape + 1) * width, 3] != 0 for shape in range(len(GROUND_CORNERS))]


def LandscapeDetail128(image):
  """Reduce original 256px color detail without filtering the ownership mask."""
  pixels = np.array(image)
  samples = pixels[::2, ::2]
  # Pillow filters RGBA in premultiplied space, avoiding transparent blue/black
  # fringes. Coverage follows runtime's floor sampling, not BOX's alpha average.
  result = np.array(image.resize((image.width // 2, image.height // 2), Image.Resampling.BOX))
  result[:, :, 3] = samples[:, :, 3]
  for color in WATER_PALETTE_COLORS:
    animated = np.all(samples[:, :, :3] == color, axis=2)
    result[animated] = samples[animated]
  # Sparse glitter can fall between the nearest samples. Preserve its palette
  # identity from any pixel in the block, inside the opaque water footprint.
  water = np.zeros(result.shape[:2], dtype=bool)
  for color in WATER_COLORS:
    water |= np.all(result[:, :, :3] == color, axis=2) & (result[:, :, 3] == 255)
  for dy, dx in ((0, 0), (0, 1), (1, 0), (1, 1)):
    nearby = pixels[dy::2, dx::2]
    for color in WATER_GLITTER_COLORS:
      glitter = water & np.all(nearby[:, :, :3] == color, axis=2) & (nearby[:, :, 3] == 255)
      result[glitter] = nearby[glitter]
      water[glitter] = False
  result[result[:, :, 3] == 0] = 0
  return Image.fromarray(result)


def TerrainFace128(wall, shape):
  """Rasterize the whole front wall, including corners with no ground-plane pixel."""
  pixels = np.array(wall)
  y, x = np.indices((GROUND_TILE_WIDTH, GROUND_TILE_WIDTH), dtype=float)
  _, e, south, w = GROUND_CORNERS[shape]
  distance = abs(x + .5 - 64)
  bottom = 128 - distance / 2
  top = bottom - 16 * (south + (np.where(x < 64, w, e) - south) * distance / 64)
  allowed = (y + .5 >= top) & (y + .5 < bottom)
  face = np.zeros_like(pixels)
  columns = np.flatnonzero(np.any(pixels[:, :, 3], axis=0))
  for column in range(GROUND_TILE_WIDTH):
    rows = np.flatnonzero(allowed[:, column])
    if not len(rows):
      continue
    source_x = columns[np.abs(columns - column).argmin()]
    source_y = np.flatnonzero(pixels[:, source_x, 3])
    desired = source_y[-1] - (rows[-1] - rows)
    nearest = np.abs(desired[:, None] - source_y[None, :]).argmin(axis=1)
    face[rows, column] = pixels[source_y[nearest], source_x]
  face[allowed, 3] = 255
  return Image.fromarray(face), allowed


def ZoneGround(image, color, *, dense=False):
  """Bake a smooth zone-colored overlay; the renderer supplies the actual ground."""
  pixels = np.array(image)
  allowed = pixels[:, :, 3] != 0
  color = np.array(color, dtype=float)
  if dense:
    color = np.rint(color * .85)
  tint = .36 if dense else .28
  y, x = np.indices(pixels.shape[:2], dtype=float)
  pitch = 8 if dense else 16
  dx = (x - 2 + pitch / 2) % pitch - pitch / 2
  dy = (y - 2 + pitch / 2) % pitch - pitch / 2
  dots = np.clip(1.95 - np.hypot(dx, dy), 0, 1)
  u, v, _ = GroundProjection128(0)
  distance = np.minimum.reduce((u, v, 1 - u, 1 - v)) * GROUND_TILE_WIDTH / np.sqrt(5)
  border = np.clip(1.25 - distance, 0, 1)
  coverage = np.maximum(dots, border)
  pixels[:, :, :3] = color
  pixels[:, :, 3] = np.where(allowed, np.rint(255 * (tint + (1 - tint) * coverage)), 0).astype(np.uint8)
  pixels[~allowed] = 0
  return Image.fromarray(pixels)


ZONE_COLORS = ((96, 224, 56), (96, 224, 56), (48, 104, 248), (48, 104, 248),
    (248, 208, 40), (248, 208, 40), (176, 176, 144), (208, 144, 80), (80, 208, 224))


def ZoneGroundSprites(ground):
  result = {290 + zone: ZoneGround(ground, color, dense=zone in (2, 4, 6))
      for zone, color in enumerate(ZONE_COLORS, 1)}
  for offset, zone in enumerate((1, 3, 5, 0, 7)):
    result[300 + offset] = result[290 + zone] if zone else ground.copy()
  return result


def BuildZoneGroundSprites(catalog, detail_path):
  entries = json.loads(catalog.read_text())
  source = next(entry for entry in entries if entry['id'] == 256)
  ground = Image.open(catalog.parent / source['file']).convert('RGBA')
  records = {}
  allowed = ReadGroundBoundaries128()[0]
  for ident, image in ZoneGroundSprites(ground).items():
    validate_footprint(ident, image, allowed, solid=ident == 303)
    records[ident] = (image, 128, 4, 0)
  ExportSpriteSources(catalog, records, retained=[entry for entry in entries if entry['id'] not in records])
  detail = json.loads((detail_path / 'manifest.json').read_text())
  sheets = {}
  for entry in detail['sprites']:
    if entry['id'] not in records:
      continue
    path = detail_path / entry['file']
    if path not in sheets:
      sheets[path] = Image.open(path).convert('RGBA')
    sheets[path].paste(records[entry['id']][0], tuple(entry['rect'][:2]))
  for path, image in sheets.items():
    image.save(path)
  print(f'Rebuilt {len(records)} zoning overlays inside master footprints')


def ConformGroundTile(image, allowed):
  """Extend missing edge colors without stretching the texture or grid shading."""
  pixels = np.array(image)
  for x in range(image.width):
    source_y = np.flatnonzero(pixels[:, x, 3])
    missing_y = np.flatnonzero(allowed[:, x] & (pixels[:, x, 3] == 0))
    if len(missing_y):
      if not len(source_y):
        raise ValueError(f"ground: empty source column {x}")
      nearest = np.abs(missing_y[:, None] - source_y[None, :]).argmin(axis=1)
      pixels[missing_y, x] = pixels[source_y[nearest], x]
  pixels[~allowed] = 0
  pixels[allowed, 3] = 255
  return Image.fromarray(pixels)


def FitGroundTile(source, footprint):
  """Fit a full surface to the master footprint without translucent edge pixels."""
  image = Image.new("RGBA", footprint.size)
  for x in range(image.width):
    target_y = [y for y in range(image.height) if footprint.getpixel((x, y))[3]]
    source_y = [y for y in range(source.height) if source.getpixel((x, y))[3]]
    if target_y and not source_y:
      raise ValueError(f"surface has no source pixels for footprint column {x}")
    for i, y in enumerate(target_y):
      color = source.getpixel((x, source_y[min(len(source_y) - 1,
          i * len(source_y) // len(target_y))]))
      image.putpixel((x, y), (*color[:3], 255))
  return image


def BuildSeaWaterSprite(root, catalog, detail_path):
  """Rebuild the sea from pinned upstream pixels without regenerating other art."""
  source = transparent(Image.open(fetch(root, "graphics/terrain/256/universal_watertile_32bpp.png")))
  tile = Image.new("RGBA", (256, 256))
  tile.alpha_composite(source.crop((4, 4, 260, 131)), (0, 128))
  allowed = ReadGroundBoundaries128()[0]
  footprint = Image.new("RGBA", (128, 128), (255, 255, 255, 0))
  footprint.putalpha(Image.fromarray(allowed.astype(np.uint8) * 255))
  tile = LandscapeDetail128(FitGroundTile(tile, footprint.resize((256, 256), Image.Resampling.NEAREST)))
  validate_footprint(270, tile, allowed, True)
  entries = json.loads(catalog.read_text())
  entry = next(entry for entry in entries if entry['id'] == 270)
  tile.save(catalog.parent / entry['file'])
  detail = json.loads((detail_path / 'manifest.json').read_text())
  entry = next(entry for entry in detail['sprites'] if entry['id'] == 270)
  path = detail_path / entry['file']
  sheet = Image.open(path).convert('RGBA')
  sheet.paste(tile, tuple(entry['rect'][:2]))
  sheet.save(path)
  preview_path = detail_path / 'preview.png'
  preview = Image.open(preview_path).convert('RGBA')
  preview.paste((32, 37, 41, 255), (12, 268, 140, 348))
  preview.alpha_composite(tile.crop((0, 48, 128, 128)), (12, 268))
  preview.convert('RGB').save(preview_path)
  print('Rebuilt sea water with preserved glitter palette pixels')


def LoadGroundSheet(root, name, upstream_tools, resolution=256):
  path = f"graphics/terrain/{resolution}/"
  image = Image.open(fetch(root, path + name + "_32bpp.png")).convert("RGBA")
  grid = Image.open(fetch(root, path + "groundtiles_gridlines.png")).convert("RGBA")
  overlay = image.copy()
  for y in range(0, image.height, 48 * (resolution // 64)):
    overlay.paste(grid, (0, y))
  image = upstream_tools.blendmode_overlay(image, overlay.copy(), 15 / 255, "normal")
  image = upstream_tools.blendmode_overlay(image, overlay, 55 / 255, "overlay")
  return transparent(image, white=True)


def RoughGroundTile(sheet, column, allowed):
  tile = Image.new("RGBA", (256, 256))
  # Same 127 source rows as every other flat tile. A 123-row crop loses the
  # bottom grid edge and triggers column resampling when fitted to the diamond.
  tile.alpha_composite(sheet.crop((column * 4, 4, (column + 64) * 4, 131)), (0, 128))
  return ConformGroundTile(tile, allowed)


def ExportLandscapeDetail(path, records, groups, ground_areas):
  """Keep editable artwork in a few atlases, with exact crops and anchors."""
  path.mkdir(parents=True, exist_ok=True)
  entries = []
  for name, ids in groups.items():
    columns = 7 if name.startswith("trees-") else 14
    width = max(records[ident][0].width for ident in ids)
    height = max(records[ident][0].height for ident in ids)
    sheet = Image.new("RGBA", (columns * width, ((len(ids) + columns - 1) // columns) * height))
    for index, ident in enumerate(ids):
      image, baseline, scale, offset = records[ident]
      x, y = index % columns * width, index // columns * height
      sheet.alpha_composite(image, (x, y))
      entries.append({"id": ident, "file": name + ".png", "rect": [x, y, image.width, image.height],
          "baseline": baseline, "scale_divisor": scale, "offset_x": offset})
    sheet.save(path / (name + ".png"))
  WriteGroundBoundaries128(path / "terrain-masks.png", ground_areas)
  manifest = {"source": f"https://github.com/OpenTTD/OpenGFX2/tree/{REVISION}",
      "tile_width": 128, "mask": "terrain-masks.png", "mask_shapes": len(ground_areas),
      "mask_corners": GROUND_CORNERS, "mask_height_step": 16,
      "mask_rule": "128px pixel centers; half-open map coordinates; nearest-neighbor display sampling",
      "sprites": entries}
  (path / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
  # A small contact sheet at exact asset pixels; never interpolate a preview.
  examples = [
      ("Temperate", NATIVE_GROUND_FIRST), ("Arctic", NATIVE_GROUND_FIRST + 7 * 14),
      ("Tropical", NATIVE_GROUND_FIRST + 14 * 14), ("SimCity", NATIVE_GROUND_FIRST + 21 * 14),
      ("Rough", NATIVE_GROUND_FIRST + 4 * 14), ("Rock", NATIVE_GROUND_FIRST + 6 * 14),
      ("Snow 25%", NATIVE_GROUND_FIRST + 28 * 14), ("Snow 50%", NATIVE_GROUND_FIRST + 29 * 14),
      ("Snow 100%", NATIVE_GROUND_FIRST + 31 * 14), ("Desert 50%", NATIVE_GROUND_FIRST + 33 * 14),
      ("Desert 100%", NATIVE_GROUND_FIRST + 35 * 14), ("Dirt", NATIVE_GROUND_FIRST + 14),
      ("Sea water", 270), ("Fresh water", NATIVE_SLOPED_WATER_FIRST),
      ("Rapids", NATIVE_RAPIDS_FIRST), ("Riverbank", 271),
      ("Coast", NATIVE_COAST_FIRST + 14 + 1), ("Slope", NATIVE_GROUND_FIRST + 3),
      ("Temperate tree", NATIVE_TREE_FIRST + 3), ("Deciduous tree", NATIVE_TREE_FIRST + 6 * 7 + 3),
      ("Arctic tree", NATIVE_TREE_FIRST + 19 * 7 + 3),
      ("Snowy tree", NATIVE_TREE_FIRST + 27 * 7 + 3),
      ("Tropical tree", NATIVE_TREE_FIRST + 37 * 7 + 3),
      ("Desert tree", NATIVE_TREE_FIRST + 44 * 7 + 3),
  ]
  preview = Image.new("RGBA", (6 * 152, 3 * 120 + 256), (32, 37, 41, 255))
  draw = ImageDraw.Draw(preview)
  font = ImageFont.load_default(size=14)
  for index, (label, ident) in enumerate(examples):
    x, y = index % 6 * 152, index // 6 * 120
    draw.text((x + 10, y + 8), label, fill=(230, 235, 239), font=font)
    image = records[ident][0]
    if index < 18:
      image = image.crop((0, image.height - 80, 128, image.height))
    preview.alpha_composite(image, (x + 12, y + 28))
  preview.convert("RGB").save(path / "preview.png")


def validate_footprint(sprite_id, image, allowed, solid=False):
  """Check finished pixels against a declared surface or overlay area."""
  alpha = np.array(image.convert("RGBA").getchannel("A"))
  if alpha.shape != allowed.shape:
    raise ValueError(f"sprite {sprite_id}: footprint dimensions do not match")
  outside = (alpha != 0) & ~allowed
  missing = (alpha == 0) & allowed if solid else np.zeros_like(allowed)
  translucent = (alpha != 0) & (alpha != 255) if solid else np.zeros_like(allowed)
  invalid = outside | missing | translucent
  if invalid.any():
    y, x = np.argwhere(invalid)[0]
    raise ValueError(f"sprite {sprite_id}: invalid footprint at ({x}, {y}): "
        f"{outside.sum()} outside, {missing.sum()} missing, {translucent.sum()} translucent pixels")


def clip_footprint(image, allowed):
  """Crop coverage without moving, fitting or resampling the artwork."""
  pixels = np.array(image)
  pixels[~allowed] = 0
  return Image.fromarray(pixels)


def validate_tile_tiling(allowed, corners=(0, 0, 0, 0)):
  """Flat and constant-grade surfaces must partition pixels at every cache scale."""
  widths = sorted({(8 << zoom) * scale for zoom in range(3) for scale in range(1, 9)})
  for width in widths:
    samples = np.arange(width) * allowed.shape[0] // width
    tile = allowed[np.ix_(samples, samples)]
    coverage = np.zeros((2 * width, 2 * width), dtype=np.uint8)
    for a in range(-4, 5):
      for b in range(-4, 5):
        elevation = a * (corners[3] - corners[0]) + b * (corners[1] - corners[0])
        x = (b - a) * (width // 2)
        y = (a + b) * (width // 4) - elevation * (width // 8)
        left, top = max(0, x), max(0, y)
        right, bottom = min(2 * width, x + width), min(2 * width, y + width)
        if left < right and top < bottom:
          coverage[top:bottom, left:right] += tile[top - y:bottom - y, left - x:right - x]
    center = coverage[width // 2:3 * width // 2, width // 2:3 * width // 2]
    if np.any(center != 1):
      raise ValueError(f"{width}-pixel surface {corners}: {np.count_nonzero(center == 0)} gap pixels, "
          f"{np.count_nonzero(center > 1)} overlapping pixels")
  return len(widths)


def validate_ground_joins(areas):
  """Compatible corner heights must never paint the same shared edge pixel."""
  planes = list(areas)
  # Ignore the cap's vertical front wall when checking surface-to-surface joins.
  planes[13] = np.roll(areas[0], -16, axis=0)
  widths = sorted({(8 << zoom) * scale for zoom in range(3) for scale in range(1, 9)})
  joins = 0
  for first, a_corners in enumerate(GROUND_CORNERS):
    for second, b_corners in enumerate(GROUND_CORNERS):
      for axis in range(2):
        elevation = a_corners[3 if axis == 0 else 1] - b_corners[0]
        if a_corners[2] - b_corners[1 if axis == 0 else 3] != elevation:
          continue
        for width in widths:
          samples = np.arange(width) * GROUND_TILE_WIDTH // width
          a = planes[first][np.ix_(samples, samples)]
          b = planes[second][np.ix_(samples, samples)]
          dx = (-1 if axis == 0 else 1) * width // 2
          dy = width // 4 - elevation * (width // 8)
          left, top = max(0, dx), max(0, dy)
          right, bottom = min(width, dx + width), min(width, dy + width)
          overlap = a[top:bottom, left:right] & b[top - dy:bottom - dy, left - dx:right - dx]
          if overlap.any():
            raise ValueError(f"{width}px ground join {first}/{second}, axis {axis}: "
                f"{overlap.sum()} overlapping pixels")
        joins += 1
  return joins



def simcity_ground(image):
  """Warm soil matching the classic 155/135/71 ground, keeping rocks neutral."""
  image = image.copy()
  image.putdata([(min(255, g * 155 // 85), min(255, g * 135 // 85), min(255, g * 71 // 85), a)
                 if a and g > r and g > b else (r, g, b, a) for r, g, b, a in image.getdata()])
  return image


def underworld_sprites(boundaries):
  from build_underworld_assets import baked_underworld_sprites
  return baked_underworld_sprites(boundaries)


def BuildHydroWaterSprites(catalog=SPRITE_CATALOG):
  """Project existing animated river material onto the four clean dam slopes."""
  entries = {entry['id']: entry for entry in json.loads(catalog.read_text())}
  entry = entries[NATIVE_SLOPED_WATER_FIRST]
  source = Image.open(catalog.parent / entry['file']).convert('RGBA').resize((128, 128), Image.Resampling.NEAREST)
  source = np.asarray(source)
  boundaries = ReadGroundBoundaries128()
  output = catalog.parent.parent / 'civic/198-hydro-power'
  for shape in range(1, 5):
    u, v, _ = GroundProjection128(shape)
    x = np.clip(np.floor(64 + 64 * (v - u)).astype(int), 0, 127)
    y = np.clip(np.floor(64 + 32 * (u + v)).astype(int), 0, 127)
    pixels = source[y, x].copy()
    pixels[pixels[..., 3] == 0] = (*WATER_COLORS[0], 255)
    pixels[..., 3] = boundaries[shape] * 255
    pixels[~boundaries[shape]] = 0
    image = Image.fromarray(pixels)
    validate_footprint(f'hydro water {shape}', image, boundaries[shape], True)
    image.save(output / f'water-slope-{shape}.png')


def BuildRoadPavingSprites():
  """Project shared stone paving onto the same ground boundaries as grass."""
  root = Path(__file__).resolve().parents[1]
  output = root / 'assets/landscape/buildings/ground'
  source = np.array(Image.open(root / 'assets/landscape/civic/211-fire-station/paving-texture.png').convert('RGB'))
  boundaries = ReadGroundBoundaries128()
  height, width = source.shape[:2]
  for shape in range(14):
    u, v, _ = GroundProjection128(shape)
    # Sample the material in map coordinates so adjacent tiles join on slopes.
    x = np.clip(np.floor(v * width).astype(int), 0, width - 1)
    y = np.clip(np.floor(u * height).astype(int), 0, height - 1)
    pixels = np.zeros((128, 128, 4), dtype=np.uint8)
    pixels[..., :3] = source[y, x]
    pixels[..., 3] = boundaries[shape] * 255
    pixels[~boundaries[shape]] = 0
    Image.fromarray(pixels).save(output / f'road-stone-{shape}.png')


def BuildCoastRecoverySprites(root, catalog):
  """Bake exposed dirt and recovering grass beneath the pinned shoreline overlay."""
  entries = {entry['id']: entry for entry in json.loads(catalog.read_text())}
  source = transparent(Image.open(fetch(root, 'graphics/terrain/256/shorelines_overlaynormal.png')), white=True)
  areas = MasterGroundBoundaries128()
  preview = Image.new('RGBA', (14 * 128, 3 * 128), (32, 37, 41, 255))
  for shape in range(14):
    overlay = Image.new('RGBA', (256, 256))
    if shape and shape != 13:
      mask = SLOPE_CORNERS[shape]
      slope = int(f'{mask:04b}'[::-1], 2)
      x, height, dy = GROUND[slope]
      crop = source.crop((x * 4, 4, (x + 64) * 4, (1 + height) * 4 - 1))
      overlay.alpha_composite(crop, (0, (32 + dy) * 4))
      allowed = np.repeat(np.repeat(areas[shape], 2, axis=0), 2, axis=1)
      overlay = clip_footprint(overlay, allowed)
    overlay = LandscapeDetail128(overlay)
    for stage in range(3):
      entry = entries[NATIVE_GROUND_FIRST + (stage + 1) * 14 + shape]
      image = Image.open(catalog.parent / entry['file']).convert('RGBA')
      image.alpha_composite(overlay)
      validate_footprint(f'coast recovery {stage}/{shape}', image, areas[shape], solid=True)
      image.save(catalog.parent / f'sprites/coast-recovery-{stage}-{shape:02}.png', optimize=True)
      preview.alpha_composite(image, (shape * 128, stage * 128))
  preview.save(catalog.parent / 'detail128/coast-recovery-preview.png')
  print('Exported 42 coastal ground recovery sprites')


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source", type=Path, help="upstream download cache (required for a full rebuild)")
  parser.add_argument("--catalog", type=Path, default=SPRITE_CATALOG, help="individual sprite source manifest")
  parser.add_argument("--boundaries-only", action="store_true",
      help="write the shared 128px master masks before rebuilding infrastructure")
  parser.add_argument("--rough-only", action="store_true", help="rebuild only the sixteen alternate rough tiles")
  parser.add_argument("--zones-only", action="store_true", help="rebuild zoning from checked-in flat ground")
  parser.add_argument("--hydro-water-only", action="store_true", help="rebuild only the four clean dam water slopes")
  parser.add_argument("--road-paving-only", action="store_true", help="rebuild only the fourteen stone road surfaces")
  parser.add_argument("--coast-recovery-only", action="store_true", help="rebuild coastal ground recovery")
  parser.add_argument("--sea-water-only", action="store_true", help="rebuild only the sea water sprite")
  parser.add_argument("--detail-output", type=Path, default=Path("assets/landscape/opengfx2/detail128"),
      help="editable 128px landscape atlases and crop manifest")
  args = parser.parse_args()
  if args.road_paving_only:
    BuildRoadPavingSprites()
    print('Exported fourteen stone road surfaces')
    return
  if args.zones_only:
    BuildZoneGroundSprites(args.catalog, args.detail_output)
    return
  if args.hydro_water_only:
    BuildHydroWaterSprites(args.catalog)
    print('Exported four clean hydro water slopes')
    return
  if args.source is None:
    parser.error('--source is required for a full rebuild')
  root = args.source
  if args.coast_recovery_only:
    BuildCoastRecoverySprites(root, args.catalog)
    return
  if args.sea_water_only:
    BuildSeaWaterSprite(root, args.catalog, args.detail_output)
    return
  master_areas = MasterGroundBoundaries128()
  for shape in range(5):
    widths = validate_tile_tiling(master_areas[shape], GROUND_CORNERS[shape])
  joins = validate_ground_joins(master_areas)
  WriteGroundBoundaries128(args.detail_output / "terrain-masks.png", master_areas)
  if args.boundaries_only:
    print(f"Defined {len(master_areas)} master ground boundaries at 128px; "
        f"flat and four inclines at {widths} tile widths without gaps or overlap; {joins} compatible slope joins")
    return
  records = {}
  footprints = {}

  def add(id, image, baseline, scale, offset_x=0, *, allowed=None, solid=False):
    if UNDERGROUND_FIRST <= id <= UNDERGROUND_LAST:
      raise ValueError(f"sprite {id}: landscape artwork cannot replace reserved underground sprites")
    records[id] = (image, baseline, scale, offset_x)
    if allowed is not None:
      footprints[id] = (allowed, solid)

  # Import original extra-zoom colors with upstream grid shading. The 128px
  # master boundaries own coverage independently of the source artwork detail.
  fetch(root, "graphics/tools.py", False)
  sys.path.insert(0, str(root / "graphics"))
  upstream_tools = importlib.import_module("tools")
  if args.rough_only:
    allowed = np.repeat(np.repeat(master_areas[0], 2, axis=0), 2, axis=1)
    for biome, name in enumerate(("temperate", "arctic", "tropical", "temperate")):
      sheet = LoadGroundSheet(root, name + "_groundtiles_rough", upstream_tools)
      for variant, column in enumerate((1502, 1567, 1632, 1697)):
        tile = RoughGroundTile(sheet, column, allowed)
        if biome == 3:
          tile = simcity_ground(tile)
        tile = LandscapeDetail128(tile)
        ident = NATIVE_ROUGH_FIRST + biome * 4 + variant
        validate_footprint(ident, tile, master_areas[0], solid=True)
        records[ident] = (tile, 128, 4, 0)
    retained = [entry for entry in json.loads(args.catalog.read_text()) if entry["id"] not in records]
    ExportSpriteSources(args.catalog, records, retained=retained)
    detail = json.loads((args.detail_output / "manifest.json").read_text())
    sheets = {}
    for entry in detail["sprites"]:
      if entry["id"] not in records:
        continue
      path = args.detail_output / entry["file"]
      if path not in sheets:
        sheets[path] = Image.open(path).convert("RGBA")
      sheets[path].paste(records[entry["id"]][0], tuple(entry["rect"][:2]))
    for path, image in sheets.items():
      image.save(path)
    print(f"Rebuilt {len(records)} alternate rough tiles with opaque master footprints")
    return
  # SPR_SELECT_TILE (752..770): use the original normal/extra-zoom artwork,
  # template_selectors_tile anchors. Palette 772 is applied at draw time.
  # Slots 500+ are native-only assets, outside the original sprite catalog.
  palette = upstream_tools.openttd_palette
  colors = list(zip(palette["r"], palette["g"], palette["b"]))
  for detail, resolution in enumerate((64, 128, 256)):
    z = resolution // 64
    sheet = Image.open(fetch(root, f"graphics/selectors/{resolution}/selector_tile_32bpp.png"))
    source_palette = sheet.getpalette()
    indices = []
    for i in range(len(source_palette) // 3):
      color = tuple(source_palette[3 * i:3 * i + 3])
      indices.append(min(range(256), key=lambda j: sum((a - b) ** 2 for a, b in zip(color, colors[j]))))
    for shape in range(14):
      corners = SLOPE_CORNERS[shape]
      slope = int(f"{corners:04b}"[::-1], 2)
      x, h, dy = GROUND[0 if slope == 15 else slope]
      crop = sheet.crop((x * z, z, (x + 64) * z, (1 + h) * z - 1))
      image = Image.new("P", crop.size)
      image.putdata([indices[value] for value in crop.getdata()])
      tile = Image.new("P", (resolution, resolution))
      tile.info["transparency"] = 0
      tile.paste(image, (0, (32 + dy - (8 if slope == 15 else 0)) * z))
      add(500 + detail * 14 + shape, tile, resolution, 2 * z)
  # SPR_DOT (774): use the normal and zoom-out variants, retaining their anchors.
  for zoom, resolution in enumerate((16, 32, 64)):
    sheet = Image.open(fetch(root, f"graphics/selectors/{resolution}/selector_point_32bpp.png"))
    source_palette = sheet.getpalette()
    size, baseline, offset = (5, 3, -2) if resolution == 16 else (7, 4, -3)
    point = Image.new("P", (size, size))
    point.info["transparency"] = 0
    point.putdata([min(range(256), key=lambda j: sum((a - b) ** 2 for a, b in
        zip(source_palette[3 * value:3 * value + 3], colors[j])))
        for value in sheet.crop((1, 1, 1 + size, 1 + size)).getdata()])
    add(NATIVE_POINT + zoom, point, baseline, 2, offset)
  terrain = "graphics/terrain/256/"
  ground_sheets = {}

  def ground_sheet(name, resolution=256):
    key = name, resolution
    if key not in ground_sheets:
      ground_sheets[key] = LoadGroundSheet(root, name, upstream_tools, resolution)
    return ground_sheets[key]

  grass = ground_sheet("temperate_groundtiles")
  rocks = ground_sheet("temperate_groundtiles_rocks_alt")
  # The 256px working canvas carries color detail only. Its coverage is a
  # nearest-neighbor enlargement of the authoritative 128px masks.
  ground_areas = [np.repeat(np.repeat(area, 2, axis=0), 2, axis=1) for area in master_areas[:14]]

  def ground(sheet, shape, row=0, conform=True):
    mask = SLOPE_CORNERS[shape]
    slope = int(f"{mask:04b}"[::-1], 2)
    if slope == 15:
      slope = 0
    x, h, dy = GROUND[slope]
    crop = sheet.crop((x * 4, (1 + row) * 4, (x + 64) * 4, (1 + row + h) * 4 - 1))
    tile = Image.new("RGBA", (256, 256))
    if mask == 15:
      # This SC2 corner case is a raised flat cap. Its exposed front faces
      # use the rock artwork; do not leave transparent cracks below the cap.
      rock = rocks.crop((4, 4, 260, 131))
      for px in range(256):
        south_edge = 192 + (128 - abs(px - 128)) // 2
        for py in range(south_edge - 32, min(256, south_edge)):
          color = rock.getpixel((px, max(0, min(126, py - 128))))
          if color[3] == 0:
            color = (104, 100, 88, 255)
          tile.putpixel((px, py), color)
    tile.alpha_composite(crop, (0, (32 + dy - (8 if mask == 15 else 0)) * 4))
    return ConformGroundTile(tile, ground_areas[shape]) if conform else tile

  ground_footprints = [ground(grass, shape) for shape in range(14)]
  flat_ground = ground(grass, 0)

  def add_surface(id, image, shape=0):
    # Normalize source edge rasters before declaring the final tile footprint.
    # Bank pieces are clipped separately; this must never squeeze their art.
    alpha = np.array(image.getchannel("A"))
    if not np.array_equal(alpha, ground_areas[shape] * 255):
      image = FitGroundTile(image, ground_footprints[shape])
    add(id, image, 256, 8, allowed=ground_areas[shape], solid=True)

  for ident, (image, allowed, solid) in underworld_sprites(master_areas).items():
    add(ident, image, image.width, image.width // 32, allowed=allowed, solid=solid)

  # OpenTTD DrawFoundation: fourteen leveled and eight inclined forms, with
  # four blocks controlling the two rear walls. Keep these separate from the
  # ground and road surfaces. All crops/geometry are prepared offline.
  foundations = transparent(Image.open(fetch(root,
      "graphics/foundations/256/foundations_temperate_32bpp.png")), white=True)
  templates = fetch(root, "templates/zoom-sensitive.pnml", False).read_text()
  def foundation_templates(name):
    body = templates.split("template " + name + "(", 1)[1].split("}", 1)[0]
    return [tuple(map(int, entry)) for entry in re.findall(
        r"\[\s*(\d+)\*z\+x\*z,\s*(\d+)\*z\+y\*z,\s*64\*z,\s*(\d+)\*z-1,"
        r"\s*(-?\d+)\*z,\s*(-?\d+)\*z", body)]
  base_foundations = foundation_templates("template_foundations_base")
  extra_foundations = foundation_templates("template_foundations_slope_halftile")[:74]
  if len(base_foundations) != 14 or len(extra_foundations) != 74:
    raise ValueError("unexpected OpenGFX2 foundation templates")

  def foundation(entry, row_offset=0):
    x, y, height, dx, dy = entry
    crop = foundations.crop((x * 4, (y + row_offset) * 4, (x + 64) * 4,
        (y + row_offset + height - 1) * 4))
    image = Image.new("RGBA", (256, 256))
    image.alpha_composite(crop, ((dx + 31) * 4, (32 + dy) * 4))
    return image

  for block in range(4):
    for slope in range(14):
      image = foundation(base_foundations[slope]) if not block else \
          foundation(extra_foundations[8 + (block - 1) * 22 + slope], 82)
      add(NATIVE_LEVEL_FOUNDATION_FIRST + block * 14 + slope, image, 256, 8)
    for incline in range(8):
      add(NATIVE_INCLINED_FOUNDATION_FIRST + block * 8 + incline,
          foundation(extra_foundations[block * 22 + incline], 82), 256, 8)

  # SC2 stores a base height and a shape per cell, rather than shared corner
  # heights. Its original slope sprites close the front down to that base.
  # Prepare the same solid boundary for infrastructure using the upstream
  # wall texture. Adjacent ground covers the face on a continuous hillside.
  wall = LandscapeDetail128(foundation(base_foundations[7]))
  for shape in range(14):
    face, allowed = TerrainFace128(wall, shape)
    allowed = np.repeat(np.repeat(allowed, 2, axis=0), 2, axis=1)
    add(NATIVE_TERRAIN_FACE_FIRST + shape, face.resize((256, 256), Image.Resampling.NEAREST),
        256, 8, allowed=allowed, solid=True)

  # Seven ground sets per biome: grass, dirt, 1/3, 2/3, rough, rocks,
  # alternate rocks. Keep these independent of tree species and growth.
  # The fourth ground bank is SimCity-colored temperate land; Toyland is not exposed.
  biome_sheets = []
  for biome, name in enumerate(("temperate", "arctic", "tropical", "temperate")):
    sheets = [ground_sheet(name + "_groundtiles"),
              ground_sheet(name + "_groundtiles_rough"),
              ground_sheet(name + "_groundtiles_rocks")]
    alternate = sheets[2] if name == "toyland" else ground_sheet(name + "_groundtiles_rocks_alt")
    variants = ((sheets[0], 0), (sheets[0], 144), (sheets[0], 96), (sheets[0], 48),
                (sheets[1], 0), (sheets[2], 0), (alternate, 0))
    biome_sheets.append(variants)
    for variant, (sheet, row) in enumerate(variants):
      for shape in range(14):
        tile = ground(sheet, shape, row)
        add_surface(NATIVE_GROUND_FIRST + (7 * biome + variant) * 14 + shape,
            simcity_ground(tile) if biome == 3 else tile, shape)
    for variant, column in enumerate((1502, 1567, 1632, 1697)):
      tile = RoughGroundTile(sheets[1], column, ground_areas[0])
      add_surface(NATIVE_ROUGH_FIRST + biome * 4 + variant, simcity_ground(tile) if biome == 3 else tile)
  snow_transition = ground_sheet("arctic_groundtiles_snowtransition")
  snow = ground_sheet("arctic_groundtiles_snow")
  desert_transition = ground_sheet("tropical_groundtiles_deserttransition")
  desert = ground_sheet("tropical_groundtiles_desert")
  tropical_grass = ground_sheet("tropical_groundtiles")
  grass_blends = []
  for blend, (left, right) in enumerate(((0, 1), (0, 2), (1, 2))):
    a, b = biome_sheets[left][0][0], biome_sheets[right][0][0]
    box = (0, 0, min(a.width, b.width), min(a.height, b.height))
    grass_blends.append(Image.blend(a.crop(box), b.crop(box), 0.5))
    for variant in range(7):
      a, arow = biome_sheets[left][variant]
      b, brow = biome_sheets[right][variant]
      for shape in range(14):
        footprint = ground(grass, shape)
        add_surface(NATIVE_GRASS_BLEND_FIRST + (blend * 7 + variant) * 14 + shape,
            Image.blend(FitGroundTile(ground(a, shape, arow), footprint),
                FitGroundTile(ground(b, shape, brow), footprint), 0.5), shape)

  def snow_grass(shape, amount, blend):
    source = np.array(ground(snow_transition, shape, amount * 48), dtype=np.float32)
    green = np.array(ground(biome_sheets[1][0][0], shape), dtype=np.float32)
    white = np.array(ground(snow, shape), dtype=np.float32)
    target = np.array(ground(grass_blends[blend], shape), dtype=np.float32)
    delta = white[:, :, :3] - green[:, :, :3]
    # Preserve snow and its shading; replace only the exposed grass component.
    coverage = np.clip(np.sum((source[:, :, :3] - green[:, :, :3]) * delta, axis=2) /
        np.maximum(1, np.sum(delta * delta, axis=2)), 0, 1)
    source[:, :, :3] += (target[:, :, :3] - green[:, :, :3]) * (1 - coverage[:, :, None])
    return Image.fromarray(np.clip(np.rint(source), 0, 255).astype(np.uint8))

  # Arctic-containing blends retain their own grass beneath the existing snow
  # patterns. Quarter snow is used only by demolition recovery, not elevation.
  for family, blend in enumerate((0, 2)):
    for amount in range(2):
      for shape in range(14):
        add_surface(NATIVE_SNOW_GRASS_FIRST + (family * 2 + amount) * 14 + shape,
            snow_grass(shape, amount, blend), shape)
  for shape in range(14):
    for amount in range(4):
      add_surface(NATIVE_GROUND_FIRST + (28 + amount) * 14 + shape,
          ground(snow_transition, shape, amount * 48) if amount < 3 else ground(snow, shape), shape)
    # OpenGFX2 supplies half-desert and full-desert. The native quarter levels
    # use a fixed normal-detail dither between those immutable source rasters.
    green = ground(tropical_grass, shape)
    half = ground(desert_transition, shape)
    full = ground(desert, shape)
    for amount in range(4):
      tile = (half if amount < 2 else full).copy()
      if amount in (0, 2):
        other = green if amount == 0 else half
        pixels, alternatives = list(tile.getdata()), list(other.getdata())
        tile.putdata([a if ((i % 256) // 4 + (i // 256) // 4) & 1 else b
                      for i, (a, b) in enumerate(zip(pixels, alternatives))])
      add_surface(NATIVE_GROUND_FIRST + (32 + amount) * 14 + shape, tile, shape)

  water = transparent(Image.open(fetch(root, terrain + "universal_watertile_32bpp.png")))
  tile = Image.new("RGBA", (256, 256))
  tile.alpha_composite(water.crop((4, 4, 260, 131)), (0, 128))
  tile = FitGroundTile(tile, flat_ground)
  add_surface(270, tile)

  # SC2's bank shape encodes wet neighbours, not an exposed ground slope.
  # Use OpenTTD's flat river-bank pieces over water, following DrawWaterEdges:
  # four sides, four outside corners, then four inside corners. The templates
  # are template_waterfeature_flat at the pinned revision, in 64-pixel units.
  bank_templates = ((27, 41, 38, 19, -5, 0), (27, 14, 38, 19, -5, 13),
      (1, 54, 38, 19, -31, 13), (1, 1, 38, 19, -31, 0),
      (41, 91, 24, 12, 9, 10), (21, 101, 24, 12, -11, 20),
      (1, 91, 24, 12, -31, 10), (21, 81, 24, 12, -11, 0),
      (49, 133, 16, 8, 17, 12), (25, 145, 16, 8, -7, 24),
      (1, 133, 16, 8, -31, 12), (25, 121, 16, 8, -7, 0))
  infrastructure = "graphics/infrastructure/256/river_"
  def bank_overlay(name):
    return Image.open(fetch(root, infrastructure + name + ".png")).convert("RGBA")
  bank_alpha = bank_overlay("overlayalpha")
  bank_normal = bank_overlay("overlaynormal")
  bank_shading = bank_overlay("overlayshading")
  bank_pieces = {}

  def bank_piece(template, background, shape=0):
    x, y, w, h, dx, dy = template
    box = (x * 4, y * 4, (x + w) * 4, (y + h - 1) * 4)
    dest_x, dest_y = (31 + dx) * 4, (32 + dy) * 4
    piece = background.crop((dest_x, dest_y, dest_x + w * 4, dest_y + (h - 1) * 4))
    # Upstream's shading pass expects blue-keyed terrain, not transparent black.
    # It makes the background opaque, so leaving black here creates a dark rim.
    piece.putdata([pixel if pixel[3] else (0, 0, 255, 255) for pixel in piece.getdata()])
    piece.alpha_composite(bank_alpha.crop(box))
    piece.alpha_composite(bank_normal.crop(box))
    piece = upstream_tools.blend_overlay(piece, bank_shading.crop(box), 192 / 255)
    allowed = ground_areas[shape][dest_y:dest_y + piece.height, dest_x:dest_x + piece.width]
    return clip_footprint(transparent(piece), allowed), dest_x, dest_y

  def bank_ground(shape, sheet=grass, row=0, source=None):
    key = (id(sheet if source is None else source), row)
    if key not in bank_pieces:
      background = ground(sheet, 0, row) if source is None else source
      pieces = []
      for template in bank_templates:
        pieces.append(bank_piece(template, background))
      bank_pieces[key] = (sheet, source, pieces)
    cardinal_shapes = (13, 21, 18, 8, 19, 16, 5, 1, 20, 7, 17, 4, 6, 3, 2, 0)
    connections = cardinal_shapes.index(shape) if shape in cardinal_shapes else 15
    bank = tile.copy()
    draw = [edge for edge in range(4) if not connections & (1 << edge)]
    # NW/SW/SE/NE missing diagonal, for the four concave bank forms.
    inner = {9: 3, 10: 0, 11: 1, 12: 2}.get(shape, -1)
    for corner, mask in enumerate((3, 6, 12, 9)):
      if not connections & mask:
        draw.append(4 + corner)
      elif connections & mask == mask and inner == corner:
        draw.append(8 + corner)
    for index in draw:
      piece, x, y = bank_pieces[key][2][index]
      bank.alpha_composite(piece, (x, y))
    # Pieces are clipped to this tile, preserving their original raster.
    # Fitting a finished bank would squeeze the shoreline into the water.
    return bank

  # OpenGFX2's four planar river slopes include the rocky whitewater artwork.
  # Fit their edge raster to the shared 128px master, as for flat water.
  rapids = transparent(Image.open(fetch(root, terrain + "universal_rivertiles_32bpp.png")))
  # template_waterslopes(80, 0, 4) starts with the distinct inland river tile
  # at x=402. Keep its colors and anchor, but use the same opaque footprint as
  # sea and ground: the extra-zoom river's edge raster otherwise leaves seams.
  river = Image.new("RGBA", (256, 256))
  river.alpha_composite(rapids.crop((402 * 4, 4, 466 * 4, 32 * 4 + 3)), (0, 128))
  river = FitGroundTile(river, flat_ground)
  for shape, (x, h, dy) in enumerate(((241, 40, -8), (321, 40, -8), (161, 24, 0), (81, 24, 0)), 1):
    image = Image.new("RGBA", (256, 256))
    image.alpha_composite(rapids.crop((x * 4, 4, (x + 64) * 4, (1 + h) * 4 - 1)), (0, (32 + dy) * 4))
    add_surface(NATIVE_RAPIDS_FIRST + shape - 1, FitGroundTile(image, ground(grass, shape)), shape)

  # Every water slope is prepared offline, including non-planar SC2 river forms.
  for shape in range(14):
    image = river if shape == 0 else records[NATIVE_RAPIDS_FIRST + shape - 1][0] if 1 <= shape <= 4 else \
        FitGroundTile(tile, ground(grass, shape))
    add_surface(NATIVE_SLOPED_WATER_FIRST + shape, image, shape)

  # Side banks follow the hillside, using template_waterfeature_slopes rather
  # than a flat bank drawn across a sloping river. Keep the two sides separate
  # so adjacent river tiles can omit their shared bank without extra composites.
  slope_sides = (((287, 41, 38, 27, -5, -8), (261, 54, 38, 27, -31, 5)),
      ((92, 14, 38, 27, -5, 5), (66, 1, 38, 27, -31, -8)),
      ((223, 41, 37, 18, -4, 0), (196, 47, 37, 18, -31, 6)),
      ((158, 7, 37, 18, -4, 6), (131, 1, 37, 18, -31, 0)))
  def climate_ground(climate, shape):
    if climate <= 2:
      background = ground(biome_sheets[1 if climate == 2 else 0][0][0], shape)
      if climate == 0: background = simcity_ground(background)
    elif climate <= 5:
      background = ground(snow_transition, shape, (climate - 3) * 48)
    elif climate == 6:
      background = ground(snow, shape)
    elif climate <= 8:
      background = ground(tropical_grass, shape)
    elif climate <= 10:
      background = ground(desert_transition if climate == 9 else desert, shape)
    elif climate <= 13:
      background = ground(grass_blends[climate - 11], shape)
    else:
      background = snow_grass(shape, 1, 0 if climate == 14 else 2)
    return background

  for climate in range(16):
    for shape in range(5):
      background = climate_ground(climate, shape)
      if shape == 0:
        # Keep every flat bank piece independent: the SC2 shape table cannot
        # represent all combinations of straight sides and diagonal corners.
        for index, template in enumerate(bank_templates):
          piece, x, y = bank_piece(template, background)
          bank = Image.new("RGBA", (256, 256))
          bank.alpha_composite(piece, (x, y))
          add(NATIVE_RIVER_BANK_FIRST + climate * 12 + index, bank, 256, 8, allowed=ground_areas[0])
        continue
      for side, template in enumerate(slope_sides[shape - 1]):
        piece, x, y = bank_piece(template, background, shape)
        bank = Image.new("RGBA", (256, 256))
        bank.alpha_composite(piece, (x, y))
        add(NATIVE_RIVER_SIDE_FIRST + (climate * 4 + shape - 1) * 2 + side, bank, 256, 8,
            allowed=ground_areas[shape])

  # Sea shores retain the ground slope, with low corners at water level and
  # high corners one level above. Match the shared master boundaries without
  # flattening the coastal art into the river-bank diamond.
  shoreline = transparent(Image.open(fetch(root, terrain + "shorelines_overlaynormal.png")), white=True)
  for climate in range(16):
    for shape in range(14):
      coast = climate_ground(climate, shape)
      if shape and shape != 13:
        overlay = clip_footprint(ground(shoreline, shape, conform=False), ground_areas[shape])
        coast.alpha_composite(overlay)
      add_surface(NATIVE_COAST_FIRST + climate * 14 + shape, coast, shape)

  for shape in range(15):
    if shape < 14:
      add_surface(256 + shape, ground(grass, shape), shape)
    if shape:
      add_surface(270 + shape, bank_ground(shape))
  # Share the same zoning raster with the focused checked-in-asset rebuild.
  for ident, image in ZoneGroundSprites(LandscapeDetail128(ground(grass, 0))).items():
    image = image.resize((256, 256), Image.Resampling.NEAREST)
    add(ident, image, 256, 8, allowed=ground_areas[0], solid=ident == 303)
  # These are additional shoreline combinations produced by regrading water,
  # not waterfall tiles. Assemble their exposed land corners over flat water.
  for special in range(6):
    add_surface(285 + special, bank_ground(16 + special))

  # Every coastline uses the same biome/coverage artwork as dry ground.
  # Keep the animated water and shoreline overlay unchanged.
  shore_sheets = ((ground_sheet("arctic_groundtiles"), 0),
      (snow_transition, 0), (snow_transition, 48), (snow_transition, 96), (snow, 0),
      (tropical_grass, 0), (desert_transition, 0), (desert, 0))
  def add_shores(first, make_bank):
    shores = [make_bank(shape) for shape in range(1, 15)]
    shores.extend(make_bank(16 + special) for special in range(6))
    for shape, shore in enumerate(shores):
      add_surface(first + shape, shore)

  for bank, (sheet, row) in enumerate(shore_sheets):
    add_shores(NATIVE_SHORE_FIRST + bank * 20, lambda shape: bank_ground(shape, sheet, row))
  for blend, sheet in enumerate(grass_blends):
    add_shores(NATIVE_GRASS_BLEND_SHORE_FIRST + blend * 20, lambda shape: bank_ground(shape, sheet))
  for family, blend in enumerate((0, 2)):
    background = snow_grass(0, 1, blend)
    add_shores(NATIVE_SNOW_GRASS_SHORE_FIRST + family * 20,
        lambda shape: bank_ground(shape, source=background))

  # Quarter-desert artwork is never selected (only 0/50/100% is supported).
  # Reuse those existing slots for matching brown shores without growing the atlas.
  for sprite in range(271, 291):
    image, baseline, scale, offset_x = records[sprite]
    destination = NATIVE_SIMCITY_SHORE_FIRST + sprite - 271 if sprite < 285 else \
        NATIVE_SIMCITY_SHORE_EXTRA + sprite - 285
    add(destination, simcity_ground(image), baseline, scale, offset_x, allowed=ground_areas[0], solid=True)

  bridge_path = "graphics/bridges/256/"
  bridge_mask = Image.open(fetch(root, bridge_path + "bridge_deckmask.png")).convert("RGB")
  bridge_ground = transparent(Image.open(fetch(root, terrain + "general_bridgetiles_32bpp.png")))
  decks = {}
  for transport in ("road", "rail"):
    overlay = transparent(Image.open(fetch(root, "graphics/infrastructure/256/" + transport + "_overlayalpha.png")))
    decks[transport] = []
    for axis in range(2):
      deck = bridge_ground.crop((4, 4, 260, 132))
      x = (1 + 65 * axis) * 4
      deck.alpha_composite(overlay.crop((x, 4, x + 256, 132)))
      decks[transport].append(deck)

  def bridge(id, style, column=0, transport="road"):
    sheet = transparent(Image.open(fetch(root, bridge_path + "bridge_" + style + "_32bpp.png")))
    axis = (column // 65) & 1
    body = sheet.crop(((column + 1) * 4, 4, (column + 65) * 4, 512))
    deck = Image.new("RGBA", body.size)
    deck.alpha_composite(decks[transport][axis], (0, 81 * 4))
    mask_x = (1 + 65 * axis) * 4
    mask = bridge_mask.crop((mask_x, 4, mask_x + 256, 512))
    pixels, masks = list(deck.getdata()), list(mask.getdata())
    deck.putdata([pixel if color not in ((0, 0, 255), (255, 255, 255)) else (0, 0, 0, 0)
                  for pixel, color in zip(pixels, masks)])
    body.alpha_composite(deck)
    canvas = Image.new("RGBA", (512, 768))
    # OpenTTD advances the front's world position by twelve units. Its screen
    # displacement cancels the front/rear template anchor difference, so the
    # sheet crops share an origin when composed into one finished sprite.
    canvas.alpha_composite(body, (128, (192 - 36 - 84) * 4))
    for row in (259, 129):
      if sheet.height <= row * 4:
        continue
      front = sheet.crop(((column + 1) * 4, (row + 1) * 4, (column + 65) * 4, (row + 128) * 4))
      canvas.alpha_composite(front, (128, (192 - 36 - 84) * 4))
    # SC2's unrotated bridge follows its east-west road/rail layout, which is
    # the opposite screen diagonal to the upstream even-numbered columns.
    add(id, canvas.transpose(Image.Transpose.FLIP_LEFT_RIGHT), 768, 8, -128)

  for sprite_id, column in zip(range(81, 86), (520, 0, 130, 260, 390)):
    bridge(sprite_id, "suspension", column)
  bridge(86, "girder")
  bridge(87, "deckonly")
  # The authored bascule pair is installed with the native road manifest below.
  bridge(90, "girder", transport="rail")
  bridge(91, "girder", column=130, transport="rail")
  bridge(106, "girder")
  bridge(107, "girder", column=130)

  trees = "graphics/trees/256/"
  fetch(root, trees + "temperate_leaf_shape.png")
  for name in ("shapeproc.py", "tools.py"):
    fetch(root, "graphics/trees/" + name if name == "shapeproc.py" else "graphics/" + name, False)
  generated = root / trees / "pygen/temperate_32bpp.png"
  if not generated.exists():
    sys.path[:0] = [str(root / "graphics"), str(root / "graphics/trees")]
    importlib.import_module("shapeproc").tree_shapeproc(str(root / trees), 4, False, False)
  tree_sheet = transparent(Image.open(generated))
  # XBLD 6..12 represents increasing tree cover, not a separate simulation species.
  # Mix upstream species without consuming the deterministic City RNG.
  species = (0, 2, 4, 7, 9, 12, 16)
  for stage in range(7):
    tile = Image.new("RGBA", (256, 384))
    count = 1 if stage < 3 else 2 if stage < 5 else 3 if stage == 5 else 4
    placements = ((0, 0),) if count == 1 else ((0, -8), (-13, 0), (13, 0), (0, 6))[:count]
    for index, (dx, dy) in enumerate(placements):
      kind = species[(stage + index * 2) % len(species)]
      growth = min(stage, 3)
      x, y = (1 + 46 * growth) * 4, (1 + 81 * kind) * 4
      sprite = tree_sheet.crop((x, y, x + 180, y + 320))
      tile.alpha_composite(sprite, ((8 + dx) * 4, (11 + dy) * 4))
    add(6 + stage, tile, 384, 8)

  tree_detail = "graphics/trees/256/"
  for name in ("temperate_leaf", "arctic_leaf", "tropical_leaf", "tropical_nonleaf", "toyland_leaf"):
    fetch(root, tree_detail + name + "_shape.png")
  sys.path[:0] = [str(root / "graphics"), str(root / "graphics/trees")]
  processor = importlib.import_module("shapeproc")
  processor.tree_shapeproc(str(root / tree_detail), 4, False, False)
  # Only the arctic snowy family is used. Isolate that input so upstream does
  # not generate unused snowy tropical, temperate and Toyland art as well.
  snow_root = root / tree_detail / "snow"
  snow_root.mkdir(exist_ok=True)
  snow_shape = snow_root / "arctic_leaf_shape.png"
  if not snow_shape.exists():
    snow_shape.write_bytes((root / tree_detail / "arctic_leaf_shape.png").read_bytes())
  processor.tree_shapeproc(str(snow_root), 4, True, False)
  sheets = {name: transparent(Image.open(root / tree_detail / "pygen" / (name + "_32bpp.png")))
            for name in ("temperate", "arctic", "tropical", "tropical_nonleaf", "toyland")}
  sheets["arctic_snow"] = transparent(Image.open(snow_root / "pygen/arctic_snow_32bpp.png"))
  art = [("temperate", row) for row in (0, 1, 3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 11, 13, 14, 15, 16, 17, 18)]
  art += [(name, row) for name in ("arctic", "arctic_snow") for row in (1, 3, 4, 5, 0, 2, 6, 7)]
  art += [("tropical_nonleaf", 0), ("tropical_nonleaf", 1), ("tropical", 0), ("tropical", 1),
          ("tropical_nonleaf", 2), ("tropical_nonleaf", 3), ("tropical", 2), ("tropical", 3),
          ("tropical", 4)]
  art += [("tropical_nonleaf", row) for row in range(4, 11)] + [("tropical", 5), ("tropical", 6)]
  art += [("toyland", row) for row in range(9)]
  assert len(art) == 62
  for kind, (name, row) in enumerate(art):
    for growth in range(7):
      sprite = sheets[name].crop(((1 + 46 * growth) * 4, (1 + 81 * row) * 4,
          (46 + 46 * growth) * 4, (81 + 81 * row) * 4))
      # Tree template: (-23,-69), projected from the tile's centre.
      canvas = Image.new("RGBA", (256, 448))
      canvas.alpha_composite(sprite, (36, 108))
      add(NATIVE_TREE_FIRST + kind * 7 + growth, canvas, 384, 8)

  # Store every landscape family at 128px tile pitch, including water. Use
  # original extra-zoom source colors rather than enlarging normal-detail art.
  brown_shores = set(range(NATIVE_SIMCITY_SHORE_FIRST, NATIVE_SIMCITY_SHORE_FIRST + 14))
  brown_shores.update(range(NATIVE_SIMCITY_SHORE_EXTRA, NATIVE_SIMCITY_SHORE_EXTRA + 6))
  groups = {
      "terrain": [ident for ident in range(NATIVE_GROUND_FIRST, NATIVE_ROUGH_FIRST + 16)
          if ident not in brown_shores] + list(range(256, 270)) + list(range(291, 305)),
      "grass-blends": list(range(NATIVE_GRASS_BLEND_FIRST, NATIVE_GRASS_BLEND_SHORE_FIRST)),
      "snow-blends": list(range(NATIVE_SNOW_GRASS_FIRST, NATIVE_SNOW_GRASS_SHORE_FIRST)),
      "riverbanks": list(range(NATIVE_RIVER_SIDE_FIRST, NATIVE_COAST_FIRST)),
      "foundations": list(range(NATIVE_TERRAIN_FACE_FIRST, NATIVE_UNDERWORLD_GROUND_FIRST)),
      "water-coasts": list(range(270, 291)) + sorted(brown_shores) +
          list(range(NATIVE_SHORE_FIRST, NATIVE_GRASS_BLEND_FIRST)) +
          list(range(NATIVE_GRASS_BLEND_SHORE_FIRST, NATIVE_SNOW_GRASS_FIRST)) +
          list(range(NATIVE_SNOW_GRASS_SHORE_FIRST, NATIVE_TREE_FIRST)) +
          list(range(NATIVE_RAPIDS_FIRST, NATIVE_RIVER_SIDE_FIRST)) +
          list(range(NATIVE_COAST_FIRST, NATIVE_TERRAIN_FACE_FIRST)),
      "trees-legacy": list(range(6, 13)),
  }
  for kind, (name, row) in enumerate(art):
    groups.setdefault("trees-" + name, []).extend(range(NATIVE_TREE_FIRST + kind * 7,
        NATIVE_TREE_FIRST + (kind + 1) * 7))
  for ids in groups.values():
    for ident in ids:
      image, baseline, scale, offset = records[ident]
      if image.width != 256 or scale != 8 or baseline % 2 or offset % 2:
        raise ValueError(f"sprite {ident}: unexpected extra-zoom landscape template")
      records[ident] = (LandscapeDetail128(image), baseline // 2, scale // 2, offset // 2)
      if ident in footprints:
        allowed, solid = footprints[ident]
        footprints[ident] = allowed[::2, ::2], solid
  for sprite_id, (allowed, solid) in footprints.items():
    validate_footprint(sprite_id, records[sprite_id][0], allowed, solid)
  widths = validate_tile_tiling(ground_areas[0])
  validate_tile_tiling(ground_areas[0][::2, ::2])
  print(f"Verified {len(footprints)} surface and overlay footprints; "
      f"{widths} tile widths without gaps or overlap; {joins} compatible slope joins")
  ExportLandscapeDetail(args.detail_output, records, groups, master_areas)

  # Finished native roads replace only their original artwork IDs. Curves,
  # junctions and inclines are generated offline at 128px tile detail; bridge
  # and crossing structures retain their existing anchors and front occlusion.
  source_files = {}
  road_root = Path(__file__).resolve().parents[1] / "assets/landscape/roads"
  for entry in json.loads((road_root / "manifest.json").read_text()):
    # Buried tunnel spans use the baked underground catalogue, not these retired IDs.
    if NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST <= entry["id"] < NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST + 2:
      continue
    source_files[entry["id"]] = road_root / entry["file"]
    image = Image.open(road_root / entry["file"]).convert("RGBA")
    allowed = None
    if 29 <= entry["id"] <= 43 or NATIVE_DIAGONAL_ROAD_FIRST <= entry["id"] < NATIVE_DIAGONAL_RAIL_FIRST:
      shape = entry["id"] - 30 if 31 <= entry["id"] <= 34 else 0
      allowed = ground_areas[shape][::2, ::2][-image.height:]
      validate_footprint(entry["id"], image, allowed)
    add(entry["id"], image, entry["baseline"], entry["scale_divisor"], entry["offset_x"], allowed=allowed)

  # Rail composites supersede the earlier road/rail and highway/rail crossings.
  rail_root = Path(__file__).resolve().parents[1] / "assets/landscape/rails"
  for entry in json.loads((rail_root / "manifest.json").read_text()):
    source_files[entry["id"]] = rail_root / entry["file"]
    image = Image.open(rail_root / entry["file"]).convert("RGBA")
    allowed = None
    if 44 <= entry["id"] <= 62 or entry["id"] >= NATIVE_DIAGONAL_RAIL_FIRST:
      shape = entry["id"] - 45 if 46 <= entry["id"] <= 49 else 0
      allowed = ground_areas[shape][::2, ::2][-image.height:]
      validate_footprint(entry["id"], image, allowed)
    add(entry["id"], image, entry["baseline"], entry["scale_divisor"], entry["offset_x"], allowed=allowed)

  bridge_root = Path(__file__).resolve().parents[1] / "assets/landscape/bridges"
  for entry in json.loads((bridge_root / "manifest.json").read_text()):
    source_files[entry["id"]] = bridge_root / entry["file"]
    image = Image.open(bridge_root / entry["file"]).convert("RGBA")
    add(entry["id"], image, entry["baseline"], entry["scale_divisor"], entry["offset_x"])

  from build_bridge_assets import BridgeCatalogMetadata
  from build_power_assets import BuildPowerSprites
  power = BuildPowerSprites(records)
  records.update(power)
  source_files = {ident: path for ident, path in source_files.items() if ident not in power}
  ExportSpriteSources(args.catalog, records, source_files, metadata=BridgeCatalogMetadata(args.catalog))
  BuildHydroWaterSprites(args.catalog)
  BuildRoadPavingSprites()
  BuildCoastRecoverySprites(root, args.catalog)
  print(f"Exported {len(records)} individual sprite sources: {args.catalog}")


if __name__ == "__main__":
  main()
