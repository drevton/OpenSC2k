#!/usr/bin/env python3
"""Build the native road set offline from one painted asphalt material.

Pillow/numpy are build-time dependencies only. Runtime uses finished landscape
runs and the existing sprite IDs, anchors, zoom cache and foundation selection.
"""
import argparse
import json
import math
from pathlib import Path
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from bake_road_car_paths import DiagonalCoefficients

from build_highway_assets import (BuildHighways, NAMES as HIGHWAYS, MERGE_NAMES, SLOPE_NAMES,
    DIAGONAL_NAMES as HIGHWAY_DIAGONALS, WriteHighwayPreview)
from build_landscape_assets import NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST, NATIVE_DIAGONAL_ROAD_FIRST
from build_landscape_assets import NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST
from build_landscape_assets import (NATIVE_DIAGONAL_ROAD_SHARED_FIRST, NATIVE_HIGHWAY_MERGE_FIRST,
    GROUND_BOUNDARIES, ReadGroundBoundaries128)
from pack_landscape_assets import ExportSpriteSources, SPRITE_CATALOG

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/landscape/roads"
DETAIL = 4
HALF_WIDTH = .375
DIAGONAL_HALF_WIDTH = .5
SLOPES = ((0, 0, 0), (1, -1, 0), (1, 0, -1), (0, 1, 0), (0, 0, 1))
LAYOUTS = ((29, "straight-a", "straight", 0, 0), (30, "straight-b", "straight", 1, 0),
    (31, "slope-1", "straight", 1, 1), (32, "slope-2", "straight", 0, 2),
    (33, "slope-3", "straight", 1, 3), (34, "slope-4", "straight", 0, 4),
    (35, "curve-1", "curve", 0, 0), (36, "curve-2", "curve", 1, 0),
    (37, "curve-3", "curve", 2, 0), (38, "curve-4", "curve", 3, 0),
    (39, "junction-1", "junction", 0, 0), (40, "junction-2", "junction", 1, 0),
    (41, "junction-3", "junction", 2, 0), (42, "junction-4", "junction", 3, 0),
    (43, "crossroads", "cross", 0, 0))
DIAGONALS = tuple((NATIVE_DIAGONAL_ROAD_FIRST + rotation * 3 + mask - 1,
    f"diagonal-road-{rotation + 1}-{mask}", f"diagonal-{mask}", rotation, 0)
    for rotation in range(4) for mask in range(1, 4))
TRANSITIONS = tuple((NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST + rotation * 4 + (mask - 1) * 2 + part,
    f"diagonal-{name}-{rotation + 1}-{mask}", f"diagonal-{name}-{mask}", rotation, 0)
    for rotation in range(4) for mask in range(1, 3) for part, name in enumerate(("transition", "approach")))
SHARED_APPROACHES = {NATIVE_DIAGONAL_ROAD_SHARED_FIRST + axis * 4 + first * 2 + second:
    f"diagonal-shared-{axis + 1}-{first + 1}-{second + 1}"
    for axis in range(2) for first in range(2) for second in range(2)}
STRUCTURES = {63: "tunnel-1", 64: "tunnel-2", 65: "tunnel-3", 66: "tunnel-4",
    67: "power-crossing-a", 68: "power-crossing-b", 69: "rail-crossing-a", 70: "rail-crossing-b",
    75: "highway-crossing-a", 76: "highway-crossing-b",
    81: "suspension-1", 82: "suspension-2", 83: "suspension-3", 84: "suspension-4", 85: "suspension-5",
    86: "girder-bridge", 87: "bridge-deck", 88: "drawbridge-closed", 89: "drawbridge-open"}
UNDERWORLD_TUNNELS = {NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST + axis: f"underworld-road-tunnel-{axis + 1}"
    for axis in range(2)}


def ReadTerrainFootprints(path=GROUND_BOUNDARIES):
  """Roads, rail and highways borrow the same 128px master boundary sheet."""
  areas = ReadGroundBoundaries128(path)
  return {shape: areas[shape][-80 if shape else -64:] for shape in range(len(SLOPES))}


def BuildTunnels(records, source=SOURCE):
  """A flared entrance tapering into the hill, above a level road floor."""
  material = np.asarray(Image.open(source / "tunnel-material.png").convert("RGB"), dtype=float)
  # The mouth meets the low edge of the slope, not its uphill end.
  # The road floor is z=0, matching the adjacent flat tile exactly.
  back, front, spring = 0, 1, 10
  roof_height, inner_height = 28, 16
  entrance_lean = math.tan(math.radians(20)) / 64
  material_mean = material.mean()
  for rotation in range(4):
    image = records[30 if rotation % 2 == 0 else 29][0]
    canvas = Image.new("RGBA", (128, 96))
    canvas.alpha_composite(image, (0, 32))
    pixels = np.asarray(canvas.resize((128 * DETAIL, 96 * DETAIL), Image.Resampling.NEAREST)).copy()
    depth = np.full(pixels.shape[:2], -1e10)
    y, x = np.mgrid[:96 * DETAIL, :128 * DETAIL]
    u, v = Unproject((x + .5) / DETAIL, (y + .5) / DETAIL - 32, 0)
    u, v = Canonical(u, v, "curve", rotation)
    # The road remains level underneath. Rear views hide only the floor pixels
    # actually covered by the projected hillside, rather than dropping it all.
    inside = (u < front) & (u >= back) & (v > .105) & (v < .895)
    light = np.clip((u - back) / (front - back), 0, 1)
    light = .20 + .80 * light * light * (3 - 2 * light)
    pixels[..., :3] = np.rint(pixels[..., :3] * np.where(inside, light, 1)[..., None]).astype(np.uint8)

    def vertex(point, terrain=False):
      u, v, z = point
      # Keep the floor and uphill join fixed while leaning the mouth inward.
      if not terrain:
        u -= (u - back) / (front - back) * z * entrance_lean
      for _ in range(rotation):
        u, v = 1 - v, u
      return np.array((64 + 64 * (u - v), 32 + 32 * (u + v) - z, u + v + z / 32))

    # Interior surfaces are visible only through the mouth. Their projected
    # buried portions must not paint dark wedges onto the surrounding incline.
    mouth_start, mouth_end = vertex((front, 0, 0)), vertex((front, 1, 0))
    mouth_across = mouth_end - mouth_start
    mouth_up = vertex((front, 0, 1)) - mouth_start
    dx, dy = (x + .5) / DETAIL - mouth_start[0], (y + .5) / DETAIL - mouth_start[1]
    determinant = mouth_across[0] * mouth_up[1] - mouth_across[1] * mouth_up[0]
    mouth_v = (dx * mouth_up[1] - dy * mouth_up[0]) / determinant
    mouth_z = (mouth_across[0] * dy - mouth_across[1] * dx) / determinant
    mouth_top = spring + inner_height * np.sqrt(np.maximum(0, 1 - ((mouth_v - .5) / .395) ** 2))
    opening = (mouth_v >= .105) & (mouth_v <= .895) & (mouth_z >= 0) & (mouth_z <= mouth_top)
    if rotation < 2:
      pixels[..., 3][~opening] = 0

    def triangle(points, shade, cavity=False, interior=False, ground_join=False, buried_wall=False, terrain=False):
      a, b, c = [vertex(point, terrain) for point in points]
      left = max(0, math.floor(min(a[0], b[0], c[0]) * DETAIL))
      right = min(128 * DETAIL, math.ceil(max(a[0], b[0], c[0]) * DETAIL))
      top = max(0, math.floor(min(a[1], b[1], c[1]) * DETAIL))
      bottom = min(96 * DETAIL, math.ceil(max(a[1], b[1], c[1]) * DETAIL))
      if right <= left or bottom <= top:
        return
      yy, xx = np.mgrid[top:bottom, left:right]
      xx, yy = (xx + .5) / DETAIL, (yy + .5) / DETAIL
      denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
      if abs(denominator) < .00001:
        return
      wa = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / denominator
      wb = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / denominator
      wc = 1 - wa - wb
      z = wa * a[2] + wb * b[2] + wc * c[2]
      visible = (wa >= -1e-7) & (wb >= -1e-7) & (wc >= -1e-7) & (z > depth[top:bottom, left:right])
      if terrain:
        # Seed the existing offline depth buffer with the actual inclined ground.
        # Runtime terrain supplies its pixels; this pass only hides buried concrete.
        depth[top:bottom, left:right][visible] = z[visible]
        return
      if cavity or interior:
        visible &= opening[top:bottom, left:right]
      pu, pv, pz = (sum(weight * point[axis] for weight, point in zip((wa, wb, wc), points))
          for axis in range(3))
      land_height = 16 * (1 - pu + pu * pz * entrance_lean)
      if ground_join or buried_wall:
        # Solid exposed concrete; hide only the geometry actually below soil.
        visible &= pz >= land_height
      depth[top:bottom, left:right][visible] = z[visible]
      tx = ((pu + pv) * 121) % 127
      ty = (pz * 4 + pu * 63) % 127
      ix, iy = tx.astype(int), ty.astype(int)
      fx, fy = (tx - ix)[..., None], (ty - iy)[..., None]
      sample = (material[iy, ix] * (1 - fx) + material[iy, ix + 1] * fx) * (1 - fy)
      sample += (material[iy + 1, ix] * (1 - fx) + material[iy + 1, ix + 1] * fx) * fy
      color = np.full(sample.shape, 17, dtype=np.uint8) if cavity else \
          np.clip(np.rint(sample * shade), 0, 255).astype(np.uint8)
      alpha = np.full(wa.shape, 255, dtype=np.uint8)
      if ground_join:
        # A little soil on the roof's buried rim, never translucent sidewalls.
        # Neutral earth also works beside desert and snow without baked-in grass.
        edge = np.clip((sample.mean(axis=-1) - material_mean) / 24, -1, 1)
        # Measure the rim in projected pixels, not height: a shallow roof
        # otherwise turns a small vertical band into a wide brown blur.
        dx_weights = ((b[1] - c[1]) / denominator, (c[1] - a[1]) / denominator,
            (a[1] - b[1]) / denominator)
        dy_weights = ((c[0] - b[0]) / denominator, (a[0] - c[0]) / denominator,
            (b[0] - a[0]) / denominator)
        dx_point = sum(weight * np.asarray(point) for weight, point in zip(dx_weights, points))
        dy_point = sum(weight * np.asarray(point) for weight, point in zip(dy_weights, points))
        height_dx = (1 - 16 * pu * entrance_lean) * dx_point[2] + \
            16 * (1 - pz * entrance_lean) * dx_point[0]
        height_dy = (1 - 16 * pu * entrance_lean) * dy_point[2] + \
            16 * (1 - pz * entrance_lean) * dy_point[0]
        rear_distance = pu / max(math.hypot(dx_point[0], dy_point[0]), .000001)
        side_distance = (pz - land_height) / np.maximum(np.hypot(height_dx, height_dy), .000001)
        soil = ((rear_distance < .7 + .3 * edge) | (side_distance < .65 + .3 * edge)) & (pu < .82)
        earth = np.array((105, 88, 60)) * shade * (1 + .12 * edge[..., None])
        color[soil] = np.clip(np.rint(earth[soil]), 0, 255).astype(np.uint8)
      region = pixels[top:bottom, left:right]
      region[..., :3][visible] = color[visible]
      region[..., 3][visible] = alpha[visible]

    def quad(points, shade, cavity=False, interior=False, ground_join=False, buried_wall=False, terrain=False):
      triangle((points[0], points[1], points[2]), shade, cavity, interior, ground_join, buried_wall, terrain)
      triangle((points[0], points[2], points[3]), shade, cavity, interior, ground_join, buried_wall, terrain)

    if rotation >= 2:
      quad(((0, 0, 16), (1, 0, 0), (1, 1, 0), (0, 1, 16)), 0, terrain=True)
      floor_depth = ((y + .5) / DETAIL - 32) / 32
      pixels[..., 3][depth > floor_depth] = 0

    def flare(at):
      t = (at - back) / (front - back)
      return t * t * (3 - 2 * t)

    def arch(at, angle):
      t = flare(at)
      sill = 6 + (spring - 6) * t
      height = 8 + (inner_height - 8) * t
      return (at, .5 + .395 * math.cos(angle), sill + height * math.sin(angle))

    def roof(at, angle):
      t = flare(at)
      front_height = spring + (roof_height - spring) * math.sin(angle)
      return (at, .5 + (.42 + .06 * t) * math.cos(angle), 16 + (front_height - 16) * t)

    front_shade = (.73, .98, .88, .77)[rotation]
    # The rounded mouth narrows and blends smoothly into the upper ground level.
    # There is no exposed rear wall: the roof's uphill edge is flush with soil.
    sections = np.linspace(back, front, 17)
    for start, end in zip(sections, sections[1:]):
      for n in range(24):
        a, b = n * math.pi / 24, (n + 1) * math.pi / 24
        quad((roof(start, a), roof(end, a), roof(end, b), roof(start, b)),
            .86 + .17 * math.sin((a + b) / 2), ground_join=True)
      for angle in (0, math.pi):
        a, b = roof(start, angle), roof(end, angle)
        quad(((a[0], a[1], 16 * (1 - start)), (b[0], b[1], 16 * (1 - end)), b, a),
            .78, buried_wall=True)
      if rotation < 2:
        for across in (.105, .895):
          quad(((start, across, 0), (end, across, 0),
              (end, across, 6 + (spring - 6) * flare(end)),
              (start, across, 6 + (spring - 6) * flare(start))), .28, interior=True)
    if rotation < 2:
      sill = 6
      quad(((back, .105, 0), (back, .895, 0), (back, .895, sill), (back, .105, sill)), 0, cavity=True)
    for n in range(24):
      a, b = n * math.pi / 24, (n + 1) * math.pi / 24
      # The vault tapers with the roof; its road floor stays at z=0 throughout.
      if rotation < 2:
        triangle(((back, .5, sill), arch(back, a), arch(back, b)), 0, cavity=True)
        for start, end in zip(sections, sections[1:]):
          quad((arch(start, a), arch(end, a), arch(end, b), arch(start, b)), .25, interior=True)
        quad([arch(front, a), roof(front, a), roof(front, b), arch(front, b)], front_shade * .68)
        quad([arch(front, a + .004), roof(front, a + .004),
            roof(front, b - .004), arch(front, b - .004)], front_shade)
    for at, shade in ((front, front_shade),):
      for a, b in ((.02, .105), (.895, .98)):
        quad([(at, a, 0), (at, b, 0), (at, b, spring), (at, a, spring)], shade)
    # BOX coverage preserves the sharp material boundary without a dark halo.
    canvas = Image.fromarray(pixels).resize((128, 96), Image.Resampling.BOX)
    records[63 + rotation] = (canvas, 96, 4, 0)


def BuildUnderworldTunnels(records, terrain):
  """Enclosed concrete boxes distinguish buried tunnel spans from roads."""
  from build_highway_assets import HighwayMesh
  texture = np.asarray(Image.open(SOURCE / "asphalt-material.png").convert("RGB"), dtype=float)
  material = np.asarray(Image.open(SOURCE / "tunnel-material.png").convert("RGB"), dtype=float)
  profile = json.loads((SOURCE / "lighting-profile.json").read_text())["samples"]
  footprint = ReadTerrainFootprints(terrain)[0]
  for axis in range(2):
    mesh = HighwayMesh(1, texture, profile, material, footprint)

    def point(at, across, z):
      u, v = (.5 + across, at) if axis == 0 else (at, .5 + across)
      return (u, v, z, at * 128 / 127, (across + .5 + z / 32) * 128 / 127)

    # A rounded 20px vault above short walls. Its narrower cross-section leaves
    # room for both the roof and the projected wall inside the cutaway grid.
    for side in (-.32, .32):
      mesh.concrete = material * (.86 if side < 0 else 1)
      mesh.Quad([point(-1, side, 0), point(2, side, 0), point(2, side, 10), point(-1, side, 10)],
          "concrete" if side < 0 else "shadow")
    angles = np.linspace(math.pi, 0, 33)
    for start, end in zip(angles, angles[1:]):
      a, az = .32 * math.cos(start), 10 + 10 * math.sin(start)
      b, bz = .32 * math.cos(end), 10 + 10 * math.sin(end)
      angle = (start + end) * .5
      mesh.concrete = material * (.76 + .24 * math.sin(angle) - .10 * math.cos(angle))
      mesh.Quad([point(-1, a, az), point(2, a, az), point(2, b, bz), point(-1, b, bz)], "concrete")
    # Center the complete silhouette (roof plus wall) on the utility plane.
    # Draw beyond the travel ends, then give each grid tile its exact pixels.
    image = Image.new("RGBA", (128, 128))
    image.alpha_composite(mesh.Finish()[0], (0, 6))
    pixels = np.array(image.resize((256, 256), Image.Resampling.NEAREST))
    # The procedural Underworld uses its own canonical grid, rather than the
    # OpenGFX2 surface-ground staircase. Match the utility texture coverage.
    y, x = np.mgrid[:256, :256]
    u = (y + .5 - 128) / 128 - (x + .5 - 128) / 256
    v = (y + .5 - 128) / 128 + (x + .5 - 128) / 256
    pixels[(u < 0) | (u >= 1) | (v < 0) | (v >= 1)] = 0
    image = Image.fromarray(pixels)
    records[NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST + axis] = (image, 256, 8, 0)


def BuildStructures(records, upstream):
  from build_landscape_assets import fetch, transparent
  # Finished native rail and highway composites own the crossings. A power
  # crossing borrows its road surface and gets its overhead components at draw time.
  for ident, axis in ((67, 29), (68, 30), (69, 29), (70, 30), (75, 29), (76, 30)):
    records[ident] = records[axis]
  rail_root = ROOT / 'assets/landscape/rails'
  for entry in json.loads((rail_root / 'manifest.json').read_text()):
    if entry['id'] in (69, 70):
      records[entry['id']] = (Image.open(rail_root / entry['file']).convert('RGBA'),
          entry['baseline'], entry['scale_divisor'], entry['offset_x'])

  bridge_path = "graphics/bridges/256/"
  mask_sheet = Image.open(fetch(upstream, bridge_path + "bridge_deckmask.png")).convert("RGB")
  terrain = transparent(Image.open(fetch(upstream, "graphics/terrain/256/general_bridgetiles_32bpp.png")))
  for ident, style, column in ((81, "suspension", 520), (82, "suspension", 0), (83, "suspension", 130),
      (84, "suspension", 260), (85, "suspension", 390), (86, "girder", 0), (87, "deckonly", 0)):
    sheet = transparent(Image.open(fetch(upstream, bridge_path + "bridge_" + style + "_32bpp.png")))
    axis = (column // 65) & 1
    body = sheet.crop(((column + 1) * 4, 4, (column + 65) * 4, 512))
    deck = Image.new("RGBA", body.size)
    ground = terrain.crop((4, 4, 260, 132))
    # The upstream deck mask's even column runs NW-SE. Native road 30 has
    # that diagonal; the finished bridge is mirrored to SC2's axis below.
    ground.alpha_composite(records[30 - axis][0].resize((256, 128), Image.Resampling.NEAREST))
    deck.alpha_composite(ground, (0, 81 * 4))
    mask_x = (1 + 65 * axis) * 4
    mask = np.asarray(mask_sheet.crop((mask_x, 4, mask_x + 256, 512)))
    pixels = np.array(deck)
    pixels[np.all(mask == (0, 0, 255), axis=-1) | np.all(mask == (255, 255, 255), axis=-1)] = 0
    body.alpha_composite(Image.fromarray(pixels))
    canvas = Image.new("RGBA", (512, 768))
    canvas.alpha_composite(body, (128, 72 * 4))
    for row in (259, 129):
      if sheet.height <= row * 4:
        continue
      front = sheet.crop(((column + 1) * 4, (row + 1) * 4, (column + 65) * 4, (row + 128) * 4))
      canvas.alpha_composite(front, (128, 72 * 4))
    # Finished structures have a 128px tile too; reduce padding/body together.
    canvas = canvas.transpose(Image.Transpose.FLIP_LEFT_RIGHT).resize((256, 384), Image.Resampling.BOX)
    records[ident] = (canvas, 384, 4, -64)

  world = ROOT / 'assets/landscape/world'
  for entry in json.loads((world / 'catalog.json').read_text()):
    if entry['id'] in (88, 89):
      records[entry['id']] = (Image.open(SPRITE_CATALOG.parent / entry['file']).convert('RGBA'),
          entry['baseline'], entry['scale_divisor'], entry['offset_x'])


def Unproject(x, y, slope):
  a, b, c = SLOPES[slope]
  diagonal = (x - 64) / 64
  v = (y - (16 if slope else 0) + 16 * a - (32 - 16 * b) * diagonal) / (64 - 16 * (b + c))
  return v + diagonal, v


def Canonical(u, v, kind, rotation):
  if kind == "straight":
    return (v, u) if rotation else (u, v)
  for _ in range(rotation):
    u, v = v, 1 - u
  return u, v


def Project(u, v, kind, rotation, slope):
  if kind == "straight":
    if rotation:
      u, v = v, u
  else:
    for _ in range(rotation):
      u, v = 1 - v, u
  a, b, c = SLOPES[slope]
  return 64 + 64 * (u - v), (16 if slope else 0) + 32 * (u + v) - 16 * (a + b * u + c * v)


def DiagonalPath(t, mask, extended=False):
  # The same centreline drives road pixels and baked car paths. Wide ends use
  # a rounded cubic with a monotone heading, shared across the bend and approach.
  origin, start, q, a, b, c = (np.asarray(value) for value in DiagonalCoefficients(mask, extended))
  t = np.asarray(t)[..., None]
  p = origin + start * t + q * t ** 2 + a * t ** 3 + b * t ** 4 + c * t ** 5
  d = start + 2 * q * t + 3 * a * t ** 2 + 4 * b * t ** 3 + 5 * c * t ** 4
  dd = 2 * q + 6 * a * t + 12 * b * t ** 2 + 20 * c * t ** 3
  return p, d, dd


def DiagonalCoordinates(u, v, kind):
  mask = int(kind[-1])
  extended = kind.startswith(("diagonal-transition-", "diagonal-approach-"))
  if kind.startswith("diagonal-approach-"):
    if mask == 1: u = u + 1
    else: v = v - 1
  # A coarse closest-point bracket keeps Newton iteration on the right branch,
  # including pixels far from the pavement that otherwise acquired stray alpha.
  values = np.linspace(0, 1, 17)
  points, _, _ = DiagonalPath(values, mask, extended)
  best = np.full(u.shape, np.inf)
  t = np.zeros(u.shape)
  for value, point in zip(values, points):
    distance = (u - point[0]) ** 2 + (v - point[1]) ** 2
    nearer = distance < best
    t = np.where(nearer, value, t)
    best = np.minimum(best, distance)
  low, high = np.maximum(0, t - 1 / 16), np.minimum(1, t + 1 / 16)
  # Closest point on the transition; extend its end tangents through the tile edge.
  for _ in range(6):
    p, d, dd = DiagonalPath(t, mask, extended)
    delta = p - np.stack((u, v), axis=-1)
    denominator = np.sum(d * d + delta * dd, axis=-1)
    t = np.clip(t - np.sum(delta * d, axis=-1) / np.maximum(denominator, .01), low, high)
  p, d, _ = DiagonalPath(t, mask, extended)
  norm = np.hypot(d[..., 0], d[..., 1])
  lateral = ((u - p[..., 0]) * d[..., 1] - (v - p[..., 1]) * d[..., 0]) / norm
  extension = ((u - p[..., 0]) * d[..., 0] + (v - p[..., 1]) * d[..., 1]) / norm ** 2
  travel = np.where((t == 0) | (t == 1), t + extension, t)
  return lateral, travel, t


def DiagonalGeometry(u, v, kind):
  lateral, travel, t = DiagonalCoordinates(u, v, kind)
  mask = int(kind[-1])
  # Wider diagonal connections use 100% of their edge; ordinary ends retain
  # 75%. Both halves choose the same width and the tile mask owns each pixel.
  blend = t ** 3 * (10 - 15 * t + 6 * t ** 2)
  widening = (1 - blend) * bool(mask & 1) + blend * bool(mask & 2)
  # Interpolate the actual perpendicular endpoint widths. Applying an angle
  # correction to an interpolated edge width pinched the middle of the bend.
  width = HALF_WIDTH + (DIAGONAL_HALF_WIDTH / math.sqrt(2) - HALF_WIDTH) * widening
  # The outline extends the endpoint tangents, so material coordinates must
  # continue along them too. A clamped t smeared one texture row into streaks.
  return np.abs(lateral) - width, lateral * HALF_WIDTH / width, travel


def Geometry(u, v, kind):
  if kind.startswith("diagonal-"):
    return DiagonalGeometry(u, v, kind)
  if kind == "curve":
    lateral = np.hypot(1 - u, v) - .5
    return np.abs(lateral) - HALF_WIDTH, lateral, np.arctan2(v, 1 - u) / (math.pi / 2)
  if kind == "straight":
    return np.abs(u - .5) - HALF_WIDTH, u - .5, v
  main = np.abs(v - .5) - HALF_WIDTH
  branch = np.abs(u - .5) - HALF_WIDTH
  if kind == "junction":
    branch = np.maximum(branch, v - .5)
  # Small fillets on the inside corners, without changing the shared ends.
  blend = np.maximum(.035 - np.abs(main - branch), 0) / .035
  distance = np.minimum(main, branch) - blend * blend * .035 / 4
  choose_branch = branch < main
  lateral = np.where(choose_branch, u - .5, v - .5)
  travel = np.where(choose_branch, v, u)
  return distance, lateral, travel


def PaintMask(kind, rotation, slope, height):
  mask = Image.new("L", (128 * DETAIL, height * DETAIL))
  draw = ImageDraw.Draw(mask)

  def polygon(points):
    draw.polygon([(round(x * DETAIL), round(y * DETAIL)) for x, y in points], fill=255)

  if kind.startswith("diagonal-"):
    connections = int(kind[-1])
    extended = kind.startswith(("diagonal-transition-", "diagonal-approach-"))
    values = np.linspace(0, 1, 257)
    points, _, _ = DiagonalPath(values, connections, extended)
    lengths = np.concatenate(([0], np.cumsum(np.hypot(*np.diff(points, axis=0).T))))
    length = lengths[-1]
    for center in ((1 / 6, .5, 5 / 6) if extended else (.25, .75)):
      t = np.interp(np.linspace(center * length - .10, center * length + .10, 65), lengths, values)
      p, d, _ = DiagonalPath(t, connections, extended)
      normal = np.stack((d[:, 1], -d[:, 0]), axis=-1) / np.hypot(*d.T)[:, None]
      outline = np.concatenate((p + .0225 * normal, (p - .0225 * normal)[::-1]))
      if kind.startswith("diagonal-approach-"):
        outline -= (1, 0) if connections == 1 else (0, -1)
      polygon([Project(u, v, kind, rotation, slope) for u, v in outline])
    if kind.startswith("diagonal-approach-"):
      tail = ((.65, .4775), (.85, .4775), (.85, .5225), (.65, .5225)) if connections == 1 else \
          ((.4775, .15), (.5225, .15), (.5225, .35), (.4775, .35))
      polygon([Project(u, v, kind, rotation, slope) for u, v in tail])
    return mask

  length = math.pi / 4 if kind == "curve" else 1
  centers = [.25, .75]
  if kind == "curve" and rotation in (0, 2):
    first = (length - .20 / 2 - .15) / 3 / length
    centers = [first, 1 - first]
  if kind in ("straight", "curve"):
    for center in centers:
      a, b = center - .1 / length, center + .1 / length
      if kind == "curve":
        points = []
        for side, values in ((1, np.linspace(a, b, 65)), (-1, np.linspace(b, a, 65))):
          for value in values:
            angle, radius = value * math.pi / 2, .5 + side * .0225
            points.append(Project(1 - radius * math.cos(angle), radius * math.sin(angle), kind, rotation, slope))
      else:
        points = [Project(u, v, kind, rotation, slope)
            for u, v in ((.4775, a), (.5225, a), (.5225, b), (.4775, b))]
      polygon(points)
  else:
    # Continue the centre paint on each connected approach, inside the road.
    # Leave the intersection itself clear instead of tracing its outer edges.
    approaches = [((.15, .4775), (.35, .4775), (.35, .5225), (.15, .5225)),
        ((.65, .4775), (.85, .4775), (.85, .5225), (.65, .5225)),
        ((.4775, .15), (.5225, .15), (.5225, .35), (.4775, .35))]
    if kind == "cross":
      approaches.append(((.4775, .65), (.5225, .65), (.5225, .85), (.4775, .85)))
    for points in approaches:
      polygon([Project(u, v, kind, rotation, slope) for u, v in points])
  return mask


def SampleMaterial(s, t, texture):
  t = np.clip(t, 0, 1)
  tx = (.5 - .5 * np.cos(2 * math.pi * t)) * 126
  # Wrap the bilinear footprint itself. Fading towards one fixed row over
  # the tile seam stretched that row into a visible band of vertical pixels.
  sy = (s % 1) * 128
  ix, iy = tx.astype(int), sy.astype(int)
  fx, fy = (tx - ix)[..., None], (sy - iy)[..., None]
  iy %= 128
  next_y = (iy + 1) % 128
  a = texture[iy, ix] * (1 - fx) + texture[iy, np.minimum(ix + 1, 127)] * fx
  b = texture[next_y, ix] * (1 - fx) + texture[next_y,
      np.minimum(ix + 1, 127)] * fx
  return a * (1 - fy) + b * fy


def BuildSurface(kind, rotation, slope, texture, profile, owned, markings=True):
  height = 80 if slope else 64
  y, x = np.mgrid[:height * DETAIL, :128 * DETAIL]
  u, v = Unproject((x + .5) / DETAIL, (y + .5) / DETAIL, slope)
  u, v = Canonical(u, v, kind, rotation)
  distance, lateral, travel = Geometry(u, v, kind)
  t = np.clip(.5 + lateral / .75, 0, 1)
  lighting_t = t if kind in ("straight", "curve") or kind.startswith("diagonal-") else \
      np.clip(1 + distance / .75, .5, 1)
  # A short flat center avoids a pale stripe between the separate white dashes.
  # Both shoulders retain a gradual OpenGFX2-style falloff across the width.
  base = np.interp(lighting_t * (len(profile) - 1), np.arange(len(profile)), profile)

  sample = SampleMaterial(travel, t, texture)
  if kind.startswith("diagonal-approach-"):
    connections = int(kind[-1])
    axis = (rotation & 1) ^ (connections == 1)
    nu, nv = Canonical(*Unproject((x + .5) / DETAIL, (y + .5) / DETAIL, slope), "straight", axis)
    ordinary = SampleMaterial(nv, .5 + (nu - .5) / .75, texture)
    blend = np.clip((u - .2) / .3 if connections == 1 else (.8 - v) / .3, 0, 1)
    blend = (blend * blend * (3 - 2 * blend))[..., None]
    sample = sample * (1 - blend) + ordinary * blend
  if kind in ("junction", "cross"):
    # One planar interior replaces the hard nearest-arm texture switch, which
    # made a visible diagonal color seam. Smooth weights retain precisely the
    # connecting arm's material at its edge without partitioning the middle.
    tx = np.clip(u * 127, 0, 127)
    ty = np.clip(v * 127, 0, 127)
    ix, iy = tx.astype(int), ty.astype(int)
    fx, fy = (tx - ix)[..., None], (ty - iy)[..., None]
    a = texture[iy, ix] * (1 - fx) + texture[iy, np.minimum(ix + 1, 127)] * fx
    b = texture[np.minimum(iy + 1, 127), ix] * (1 - fx) + texture[np.minimum(iy + 1, 127),
        np.minimum(ix + 1, 127)] * fx
    sample = a * (1 - fy) + b * fy
    total = np.ones(u.shape)
    for distance, s, across in ((u, u, v), (1 - u, u, v), (v, v, u)) + \
        (((1 - v, v, u),) if kind == "cross" else ()):
      weight = (.16 / np.maximum(distance, .00001)) ** 4
      sample += SampleMaterial(s, .5 + (across - .5) / .75, texture) * weight[..., None]
      total += weight
    sample /= total[..., None]
  grain = (sample - texture.mean(axis=(0, 1))) * 2.15
  asphalt = np.clip(np.rint(base[..., None] + grain), 0, 255).astype(np.uint8)

  # Resample asphalt separately. BOX coverage for paint has finite support:
  # unlike Lanczos on a combined image it cannot leave pale/dark ghost dashes.
  paint = PaintMask(kind, rotation, slope, height) if markings else Image.new("L", (128 * DETAIL, height * DETAIL))
  surface = Image.fromarray(asphalt).resize((128, height), Image.Resampling.LANCZOS)
  paint = paint.resize((128, height), Image.Resampling.BOX)
  rgb = np.array(surface)
  shade = np.clip(245 + (rgb[..., 0].astype(int) - 90) // 6, 240, 249).astype(np.uint8)
  pigment = Image.fromarray(np.stack((shade, shade, shade - 2), axis=-1))
  surface = Image.composite(pigment, surface, paint).convert("RGBA")

  y, x = np.mgrid[:height, :128]
  u, v = Unproject(x + .5, y + .5, slope)
  cu, cv = Canonical(u, v, kind, rotation)
  distance = Geometry(cu, cv, kind)[0]
  gradients = []
  for dx, dy in ((.5, 0), (0, .5)):
    pu, pv = Canonical(*Unproject(x + .5 + dx, y + .5 + dy, slope), kind, rotation)
    mu, mv = Canonical(*Unproject(x + .5 - dx, y + .5 - dy, slope), kind, rotation)
    gradients.append(Geometry(pu, pv, kind)[0] - Geometry(mu, mv, kind)[0])
  gradient = np.hypot(*gradients)
  fringe = distance / np.maximum(gradient, .000001)
  alpha = np.where(owned, np.rint(255 * np.clip(1 - fringe / 1.25, 0, 1)), 0).astype(np.uint8)
  surface.putalpha(Image.fromarray(alpha))
  pixels = np.array(surface)
  pixels[alpha == 0] = 0
  surface = Image.fromarray(pixels)
  return surface, paint, owned, owned & (distance <= 0)


def BuildRoadSprites(source=SOURCE, terrain=GROUND_BOUNDARIES, layouts=LAYOUTS + DIAGONALS + TRANSITIONS):
  texture = np.asarray(Image.open(source / "asphalt-material.png").convert("RGB"), dtype=float)
  profile = json.loads((source / "lighting-profile.json").read_text())["samples"]
  footprints = ReadTerrainFootprints(terrain)
  records = {}
  for ident, name, kind, rotation, slope in layouts:
    image, _, _, _ = BuildSurface(kind, rotation, slope, texture, profile, footprints[slope])
    records[ident] = (image, image.height, 4, 0)
  return records


def AddRoadVerges(records, terrain, layouts=LAYOUTS + DIAGONALS + TRANSITIONS):
  """Light the underlying terrain through a textured, translucent roadside band."""
  footprints = ReadTerrainFootprints(terrain)
  for ident, _, kind, rotation, slope in layouts:
    image, baseline, scale, offset = records[ident]
    y, x = np.mgrid[:image.height, :image.width]
    u, v = Unproject(x + .5, y + .5, slope)
    cu, cv = Canonical(u, v, kind, rotation)
    distance = Geometry(cu, cv, kind)[0]
    gradients = []
    for dx, dy in ((.5, 0), (0, .5)):
      pu, pv = Canonical(*Unproject(x + .5 + dx, y + .5 + dy, slope), kind, rotation)
      mu, mv = Canonical(*Unproject(x + .5 - dx, y + .5 - dy, slope), kind, rotation)
      gradients.append(Geometry(pu, pv, kind)[0] - Geometry(mu, mv, kind)[0])
    gradient = np.maximum(np.hypot(*gradients), .000001)
    outside = distance / gradient
    band = np.clip((outside - .65) / .65, 0, 1) * np.clip((3.4 - outside) / 1.6, 0, 1)
    # Top-left light follows the actual outline, including curved/junction edges.
    light = np.clip((-gradients[0] * .6 - gradients[1] * .8) / gradient, 0, 1)
    # Tile-periodic grain meets adjacent tiles without a seam. Vary opacity,
    # retaining the ground's own colour and texture instead of painting grass.
    grain = .55 + .25 * np.sin(2 * math.pi * (7 * u + 11 * v)) + \
        .20 * np.cos(2 * math.pi * (13 * u - 5 * v))
    alpha = np.rint(255 * .34 * band * (.35 + .65 * light) * grain).astype(np.uint8)
    alpha[~footprints[slope]] = 0
    verge = Image.new("RGBA", image.size, (255, 255, 248, 0))
    verge.putalpha(Image.fromarray(alpha))
    verge.alpha_composite(image)
    pixels = np.array(verge)
    pixels[pixels[..., 3] == 0] = 0
    records[ident] = (Image.fromarray(pixels), baseline, scale, offset)


def BuildSharedDiagonalApproaches(records, transition_first=NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST,
    shared_first=NATIVE_DIAGONAL_ROAD_SHARED_FIRST):
  # Each bend finishes tangent to the straight tile at its midpoint.
  # Keep its own half; both halves match the ordinary asphalt there.
  approaches = ((((1, 1), (2, 2)), ((0, 2), (3, 1))),
      (((0, 1), (1, 2)), ((2, 1), (3, 2))))
  y, x = np.mgrid[:64, :128]
  u, v = Unproject(x + .5, y + .5, 0)
  for axis in range(2):
    first_half = (v if axis == 0 else u) < .5
    for first in range(2):
      for second in range(2):
        halves = []
        for side, turn in enumerate((first, second)):
          rotation, mask = approaches[axis][side][turn]
          ident = transition_first + rotation * 4 + (mask - 1) * 2 + 1
          halves.append(np.asarray(records[ident][0]))
        pixels = np.where(first_half[..., None], halves[0], halves[1])
        ident = shared_first + axis * 4 + first * 2 + second
        records[ident] = (Image.fromarray(pixels), 64, 4, 0)


def SplitDiagonalApproaches(records, transition_first=NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST,
    shared_first=NATIVE_DIAGONAL_ROAD_SHARED_FIRST):
  """Store each curved half once; the renderer borrows ordinary straight halves."""
  y, x = np.mgrid[:64, :128]
  u, v = Unproject(x + .5, y + .5, 0)
  halves = (2, 1, 0, 2, 3, 0, 1, 3)
  for piece, half in enumerate(halves):
    ident = transition_first + piece * 2 + 1
    image, baseline, scale, offset = records[ident]
    pixels = np.array(image)
    first = (u if half // 2 else v) < .5
    pixels[first if half & 1 else ~first] = 0
    records[ident] = Image.fromarray(pixels), baseline, scale, offset
  for ident in range(shared_first, shared_first + 8):
    records.pop(ident, None)


def InfrastructurePreviewRecords(output, updated, transition_first=NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST,
    shared_first=NATIVE_DIAGONAL_ROAD_SHARED_FIRST, straight_first=29):
  records = {}
  for entry in json.loads((output / "manifest.json").read_text()):
    image = Image.open(output / entry["file"]).convert("RGBA")
    records[entry["id"]] = image, entry["baseline"], entry["scale_divisor"], entry["offset_x"]
  records.update(updated)
  y, x = np.mgrid[:64, :128]
  u, v = Unproject(x + .5, y + .5, 0)
  for piece, half in enumerate((2, 1, 0, 2, 3, 0, 1, 3)):
    ident = transition_first + piece * 2 + 1
    image, baseline, scale, offset = records[ident]
    straight = np.asarray(records[straight_first + half // 2][0])
    first = (u if half // 2 else v) < .5
    curved = ~first if half & 1 else first
    pixels = np.where(curved[..., None], np.asarray(image), straight)
    records[ident] = Image.fromarray(pixels), baseline, scale, offset
  BuildSharedDiagonalApproaches(records, transition_first, shared_first)
  return records


def SaveDiagonalApproachSources(output, catalog, records, layouts, shared_first):
  names = {ident: name for ident, name, *_ in layouts}
  removed = range(shared_first, shared_first + 8)
  manifest = json.loads((output / "manifest.json").read_text())
  manifest = [entry for entry in manifest if entry["id"] not in names and entry["id"] not in removed]
  for ident, (image, baseline, scale, offset) in records.items():
    image.save(output / f"{names[ident]}.png")
    manifest.append(dict(id=ident, file=f"{names[ident]}.png", width=image.width, height=image.height,
        baseline=baseline, scale_divisor=scale, offset_x=offset))
  (output / "manifest.json").write_text(json.dumps(sorted(manifest, key=lambda entry: entry["id"]), indent=2) + "\n")
  for path in output.glob("diagonal-shared-*.png"):
    path.unlink()
  InstallInfrastructureSources(catalog, records,
      {ident: output / f"{name}.png" for ident, name in names.items()}, removed)


def InstallInfrastructureSources(catalog, records, source_files, removed=()):
  """Refresh checked-in PNG references; the native build repacks the catalogue."""
  if not catalog.exists():
    return  # The full landscape importer creates the initial catalogue.
  records = {ident: record for ident, record in records.items()
      if not NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST <= ident < NATIVE_UNDERWORLD_ROAD_TUNNEL_FIRST + 2 and
      ident not in (67, 68, 71, 72, 79, 80) and
      not NATIVE_HIGHWAY_MERGE_FIRST + 18 <= ident < NATIVE_HIGHWAY_MERGE_FIRST + 24}
  retained = [entry for entry in json.loads(catalog.read_text())
      if entry["id"] not in records and entry["id"] not in removed]
  ExportSpriteSources(catalog, records, source_files, retained)


def WritePreview(records, output):
  font_path = "/System/Library/Fonts/Supplemental/Arial.ttf"
  if Path(font_path).exists():
    title = ImageFont.truetype(font_path, 22)
    label = ImageFont.truetype(font_path, 16)
  else:
    title = label = ImageFont.load_default()
  rows = (len(LAYOUTS) + 3) // 4
  board = Image.new("RGB", (1168, 78 + rows * 204 + 560), (34, 39, 44))
  draw = ImageDraw.Draw(board)
  draw.text((18, 14), "NATIVE ROAD SET — 128px tiles, shared asphalt, clean lane paint", font=title, fill="white")
  draw.text((18, 45), "75% opaque pavement; optional alpha only outside it. Shown at 2x nearest-neighbour.",
      font=label, fill=(195, 207, 216))
  names = {ident: name for ident, name, *_ in LAYOUTS} | STRUCTURES
  for n, (ident, name, *_rest) in enumerate(LAYOUTS):
    image, baseline, scale, offset = records[ident]
    x, y = 16 + n % 4 * 288, 78 + n // 4 * 204
    draw.rectangle((x, y, x + 277, y + 193), outline=(80, 93, 104))
    draw.text((x + 8, y + 8), f"{ident}: {names[ident].replace('-', ' ')}", font=label, fill="white")
    factor = 4 / scale
    sprite = image.resize((round(image.width * factor), round(image.height * factor)), Image.Resampling.NEAREST)
    sprite = sprite.resize((sprite.width * 2, sprite.height * 2), Image.Resampling.NEAREST)
    board.paste(sprite, (x + (278 - sprite.width) // 2, y + 190 - min(158, sprite.height)), sprite)

  top = 78 + rows * 204
  draw.text((18, top + 8), "CONNECTED ROADS — bends, T junctions and crossroads", font=title, fill="white")
  placements = {(0, 0): 36, (6, 0): 37, (6, 6): 38, (0, 6): 35,
      (3, 0): 41, (3, 6): 39, (0, 3): 40, (6, 3): 42, (3, 3): 43}
  for x in range(1, 6):
    for y in (0, 3, 6):
      placements.setdefault((x, y), 30)
  for y in range(1, 6):
    for x in (0, 3, 6):
      placements.setdefault((x, y), 29)
  for (u, v), ident in sorted(placements.items(), key=lambda item: (sum(item[0]), item[0][0])):
    sprite = records[ident][0]
    board.paste(sprite, (520 + (u - v) * 64, top + 55 + (u + v) * 32), sprite)
  board.save(output / "PREVIEW.png")

  board = Image.new("RGBA", (1168, 600), (34, 39, 44, 255))
  draw = ImageDraw.Draw(board)
  draw.text((18, 14), "CONNECTED DIAGONALS — ordinary bends / contextual artwork", font=title, fill="white")
  placements = {}
  for n in range(7):
    placements[(6 - n, n)] = (36, 2 if n == 0 else 3)
    placements[(6 - n, n + 1)] = (38, 2 if n == 6 else 3)
  placements[(7, 0)] = placements[(-1, 7)] = (30, 0)
  for row in range(2):
    for (u, v), (tile, mask) in sorted(placements.items(), key=lambda item: sum(item[0])):
      ident = NATIVE_DIAGONAL_ROAD_FIRST + (tile - 35) * 3 + mask - 1 if row and mask else tile
      if row:
        if mask == 2:
          ident = NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST + (tile - 35) * 4 + 2
        elif (u, v) in ((7, 0), (-1, 7)):
          curve = 36 if u == 7 else 38
          ident = NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST + (curve - 35) * 4 + 3
      board.alpha_composite(records[ident][0], (520 + (u - v) * 64, 45 + row * 156 + (u + v - 6) * 32))
  draw.text((18, 365), "SHORT CONNECTION — both bends share the middle tile", font=label,
      fill=(195, 207, 216))
  short = {(1, 1): NATIVE_DIAGONAL_ROAD_SHARED_FIRST + 7}
  for rotation, placements in ((1, ((3, 0, 1), (2, 0, 0), (0, 1, 0))),
      (3, ((2, 1, 0), (0, 2, 0), (-1, 2, 1)))):
    for u, v, part in placements:
      short[u, v] = NATIVE_DIAGONAL_ROAD_TRANSITION_FIRST + rotation * 4 + 2 + part
  for (u, v), ident in sorted(short.items(), key=lambda item: sum(item[0])):
    board.alpha_composite(records[ident][0], (520 + (u - v) * 64, 400 + (u + v) * 32))
  board.convert("RGB").save(output / "DIAGONALS.png")

  structures = [(ident, record) for ident, record in sorted(records.items()) if ident in STRUCTURES]
  if structures:
    board = Image.new("RGB", (1168, 60 + ((len(structures) + 3) // 4) * 236), (34, 39, 44))
    draw = ImageDraw.Draw(board)
    draw.text((18, 14), "ROAD CONNECTIONS — new tunnel portals and matching structure surfaces",
        font=title, fill="white")
    for n, (ident, (image, baseline, scale, offset)) in enumerate(structures):
      x, y = 16 + n % 4 * 288, 60 + n // 4 * 236
      draw.rectangle((x, y, x + 277, y + 225), outline=(80, 93, 104))
      draw.text((x + 8, y + 8), f"{ident}: {STRUCTURES[ident].replace('-', ' ')}", font=label, fill="white")
      sprite = image.crop(image.getbbox())
      board.paste(sprite, (x + (278 - sprite.width) // 2, y + 220 - sprite.height), sprite)
    board.save(output / "STRUCTURES.png")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--output", type=Path, default=SOURCE)
  parser.add_argument("--catalog", type=Path, default=SPRITE_CATALOG, help="landscape PNG source manifest")
  parser.add_argument("--terrain", type=Path, default=GROUND_BOUNDARIES,
      help="shared 128px master boundary sheet")
  parser.add_argument("--structure-source", type=Path, help="OpenGFX2 cache; rebuild retained structures offline")
  parser.add_argument("--highway-overpasses-only", action="store_true", help="build road-free highway occluders")
  parser.add_argument("--highway-slopes-only", action="store_true", help="refresh only compact highway slope artwork")
  parser.add_argument("--highway-diagonals-only", action="store_true", help="refresh only diagonal highway artwork")
  parser.add_argument("--tunnels-only", action="store_true", help="refresh the four tunnel PNGs with unchanged anchors")
  parser.add_argument("--diagonal-ends-only", action="store_true",
      help="rebake rounded ends and reusable approach halves")
  args = parser.parse_args()
  if args.diagonal_ends_only:
    records = BuildRoadSprites(terrain=args.terrain, layouts=TRANSITIONS)
    AddRoadVerges(records, args.terrain, TRANSITIONS)
    WritePreview(InfrastructurePreviewRecords(args.output, records), args.output)
    SplitDiagonalApproaches(records)
    SaveDiagonalApproachSources(args.output, args.catalog, records, TRANSITIONS, NATIVE_DIAGONAL_ROAD_SHARED_FIRST)
    print(f"Built eight rounded road ends and eight approach halves in {args.output}")
    return
  if args.highway_overpasses_only:
    identifiers = {75: 'a-0', 76: 'b-0'}
    for axis in range(2):
      for merge in range(1, 4):
        identifiers[NATIVE_HIGHWAY_MERGE_FIRST + (axis + 2) * 3 + merge - 1] = f'{"ab"[axis]}-{merge}'
    records = {}
    BuildHighways(records, SOURCE, ReadTerrainFootprints(args.terrain)[0], identifiers, underpasses=False)
    for ident, (image, *_rest) in records.items():
      image.save(args.output / f'highway-overpass-{identifiers[ident]}.png')
    print(f'Built eight highway occluders in {args.output}')
    return
  if args.tunnels_only:
    records = BuildRoadSprites(terrain=args.terrain, layouts=LAYOUTS[:2])
    BuildTunnels(records)
    args.output.mkdir(parents=True, exist_ok=True)
    for ident in range(63, 67):
      records[ident][0].save(args.output / f"{STRUCTURES[ident]}.png")
    print(f"Built four tunnel portals in {args.output}")
    return
  if args.highway_slopes_only or args.highway_diagonals_only:
    names = HIGHWAY_DIAGONALS if args.highway_diagonals_only else SLOPE_NAMES
    records = {}
    BuildHighways(records, SOURCE, ReadTerrainFootprints(args.terrain)[0], names)
    manifest = json.loads((args.output / "manifest.json").read_text())
    manifest = [entry for entry in manifest if entry["id"] not in names]
    for ident, (image, baseline, scale, offset) in records.items():
      filename = f"{names[ident]}.png"
      image.save(args.output / filename)
      manifest.append(dict(id=ident, file=filename, width=image.width, height=image.height,
          baseline=baseline, scale_divisor=scale, offset_x=offset))
    (args.output / "manifest.json").write_text(json.dumps(sorted(manifest, key=lambda entry: entry["id"]), indent=2) + "\n")
    InstallInfrastructureSources(args.catalog, records,
        {ident: args.output / f"{name}.png" for ident, name in names.items()})
    return
  records = BuildRoadSprites(terrain=args.terrain)
  if args.structure_source:
    BuildStructures(records, args.structure_source)
  elif (SOURCE / "manifest.json").exists():
    for entry in json.loads((SOURCE / "manifest.json").read_text()):
      if entry["id"] in STRUCTURES:
        records[entry["id"]] = (Image.open(SOURCE / entry["file"]).convert("RGBA"), entry["baseline"],
            entry["scale_divisor"], entry["offset_x"])
  BuildTunnels(records)
  BuildHighways(records, SOURCE, ReadTerrainFootprints(args.terrain)[0])
  BuildUnderworldTunnels(records, args.terrain)
  # Keep bridge decks and tunnel interiors free of the terrain verge treatment.
  AddRoadVerges(records, args.terrain)
  BuildSharedDiagonalApproaches(records)
  args.output.mkdir(parents=True, exist_ok=True)
  WritePreview(records, args.output)
  WriteHighwayPreview(records, args.output)
  SplitDiagonalApproaches(records)
  manifest = []
  names = {ident: name for ident, name, *_ in LAYOUTS + DIAGONALS + TRANSITIONS} | STRUCTURES | HIGHWAYS | \
      MERGE_NAMES | SLOPE_NAMES | HIGHWAY_DIAGONALS | UNDERWORLD_TUNNELS | SHARED_APPROACHES
  for ident, (image, baseline, scale, offset) in sorted(records.items()):
    name = names[ident]
    image.save(args.output / f"{name}.png")
    manifest.append(dict(id=ident, file=f"{name}.png", width=image.width, height=image.height,
        baseline=baseline, scale_divisor=scale, offset_x=offset))
  (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
  for path in args.output.glob("diagonal-shared-*.png"):
    path.unlink()
  if args.catalog:
    # Railway composites are rebuilt by build_rail_assets after road materials
    # change. A standalone road refresh must not restore their legacy artwork.
    rail_manifest = SOURCE.parent / "rails/manifest.json"
    rail_ids = {entry["id"] for entry in json.loads(rail_manifest.read_text())} if rail_manifest.exists() else set()
    InstallInfrastructureSources(args.catalog,
        {ident: record for ident, record in records.items() if ident not in rail_ids},
        {ident: args.output / f"{name}.png" for ident, name in names.items()},
        range(NATIVE_DIAGONAL_ROAD_SHARED_FIRST, NATIVE_DIAGONAL_ROAD_SHARED_FIRST + 8))
  print(f"Built {len(records)} native road sprites in {args.output}")


if __name__ == "__main__":
  main()
