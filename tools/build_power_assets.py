#!/usr/bin/env python3
"""Bake upright transmission towers and suspended conductors at 128px tile detail.

ImageGen supplies the galvanized-steel material. Geometry, cable anchors,
footings, grades and tile boundaries are deterministic offline artwork.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from build_highway_assets import HighwayMesh
from build_landscape_assets import GROUND_CORNERS, NATIVE_HIGHWAY_MERGE_FIRST
from pack_landscape_assets import SPRITE_CATALOG, ExportSpriteSources
from build_bridge_assets import FIRST as ROAD_BRIDGE_FIRST, PIECES as ROAD_BRIDGE_PIECES

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/landscape/power"
DETAIL = 4
WIRE_FIRST = ROAD_BRIDGE_FIRST + 7 * ROAD_BRIDGE_PIECES * 2
GRADE_LIMIT = 8
GRADES = GRADE_LIMIT * 2 + 1
TOWER_FIRST = WIRE_FIRST + 4 * 4 * GRADES
TOWER_FORMS = ((0, 0), (0, 1), (1, 0), (1, 1), (2, 2), (3, 2))
WATER_FIRST = TOWER_FIRST + 14 * len(TOWER_FORMS)
END = WATER_FIRST + 2
OFFSETS = (-.24, -.08, .08, .24)
TILE_MASKS = (5, 10, 10, 5, 10, 5, 3, 6, 12, 9, 11, 7, 14, 13, 15)
STEPS = ((-1, 0), (0, 1), (1, 0), (0, -1))


class PowerMesh(HighwayMesh):
  def __init__(self, material):
    empty = np.zeros((128, 128, 3))
    super().__init__(1, empty, [100, 100], empty, np.zeros((64, 128), dtype=bool))
    self.height = self.baseline = 192
    self.pixels = np.zeros((self.height * DETAIL, self.width * DETAIL, 4), dtype=np.uint8)
    self.depth = np.full(self.pixels.shape[:2], -1e10)
    self.material = material
    self.beams = 0

  def Project(self, point):
    u, v, z = point[:3]
    return np.array((64 + 64 * (v - u), self.baseline - 64 + 32 * (u + v) - z, u + v + z / 32))

  def Beam(self, a, b, radius=.7, steel=True, color=(49, 54, 53)):
    a, b = np.array(a) * (64, 64, 1), np.array(b) * (64, 64, 1)
    direction = b - a
    direction /= np.linalg.norm(direction)
    tangent = np.cross(direction, (0, 0, 1) if abs(direction[2]) < .9 else (1, 0, 0))
    tangent /= np.linalg.norm(tangent)
    normal = np.cross(direction, tangent)
    light = np.array((-.45, -.65, 1))
    light /= np.linalg.norm(light)
    if steel:
      y = (self.beams * 47) % self.material.shape[0]
      x = (self.beams * 83) % self.material.shape[1]
      color = self.material[y, x] * .86
      self.beams += 1
    for face in range(6):
      angles = np.array((face, face + 1)) * math.pi / 3
      edges = [radius * (tangent * math.cos(t) + normal * math.sin(t)) for t in angles]
      n = tangent * math.cos(angles.mean()) + normal * math.sin(angles.mean())
      shade = .62 + .38 * max(0, np.dot(n, light))
      points = [(a + edges[0]) / (64, 64, 1), (b + edges[0]) / (64, 64, 1),
          (b + edges[1]) / (64, 64, 1), (a + edges[1]) / (64, 64, 1)]
      self.Quad(points, tuple(value * shade for value in color))


def Height(shape, u, v):
  n, e, s, w = GROUND_CORNERS[shape]
  return n * (1 - u) * (1 - v) + e * (1 - u) * v + s * u * v + w * u * (1 - v)


def Tower(material, shape, style, axis):
  mesh = PowerMesh(material)
  center = sum(GROUND_CORNERS[shape]) / 4
  half = (.075, .10, .12, .14, .13)[style]
  top_half = (.022, .032, .04, .045, .035)[style]
  corners = ((-1, -1), (-1, 1), (1, 1), (1, -1))
  sections = (0, 14, 28, 42, 56, 70, 84, 96)

  def point(corner, z):
    du, dv = corners[corner]
    width = half + (top_half - half) * z / 96
    return (.5 + du * width, .5 + dv * width, z)

  for corner in range(4):
    u, v, _ = point(corner, 0)
    ground = 16 * (Height(shape, u, v) - center)
    mesh.Box(u, v, .035, .035, ground - 1.3, ground + 1.5, (161, 158, 144))
    mesh.Beam((u, v, ground + 1.5), point(corner, 96), 1 if style >= 2 else .8)
  for low, high in zip(sections, sections[1:]):
    for side in range(4):
      following = (side + 1) % 4
      mesh.Beam(point(side, high), point(following, high), .55)
      mesh.Beam(point(side, low), point(following, high), .45)
      if style or low < 42:
        mesh.Beam(point(following, low), point(side, high), .45)
  for cross_axis in (range(2) if axis == 2 else (axis,)):
    def arm(s, z):
      return (.5 + s, .5, z) if cross_axis else (.5, .5 + s, z)
    mesh.Beam(arm(-.30, 96), arm(.30, 96), 1.1)
    mesh.Beam(arm(-.30, 96), arm(0, 86), .65)
    mesh.Beam(arm(.30, 96), arm(0, 86), .65)
    if style == 1:
      mesh.Beam(arm(-.22, 81), arm(.22, 81), .75)
    for offset in OFFSETS:
      u, v, _ = arm(offset, 0)
      mesh.Beam((u, v, 90), (u, v, 96), .65, False, (88, 109, 104))
      for z in (91, 92.4, 93.8, 95.2):
        mesh.Box(u, v, .014, .014, z, z + .65, (111, 132, 124))
      mesh.Beam((u, v, 89.5), (u, v, 90.6), .8, False, (45, 49, 47))
  mesh.Beam((.5, .5, 96), (.5, .5, 103), .65)
  if axis == 2:
    # Hanging jumpers join the two conductor banks at bends and junctions.
    for offset in OFFSETS:
      points = [(.5 + offset * math.sin(t * math.pi / 2),
          .5 + offset * math.cos(t * math.pi / 2), 90 - 3 * math.sin(t * math.pi))
          for t in np.linspace(0, 1, 33)]
      for a, b in zip(points, points[1:]):
        mesh.Beam(a, b, .38, False)
  return mesh.Finish()


def Wire(material, direction, profile, grade):
  mesh = PowerMesh(material)
  du, dv = STEPS[direction]
  for offset in OFFSETS:
    points = []
    for t in np.linspace(0, 1, 65):
      u, v = .5 + .5 * du * t, .5 + .5 * dv * t
      if du: v += offset
      else: u += offset
      # Supported spans meet at 86px above the midpoint of neighboring anchors.
      # Consecutive cable-only tiles share the same lower plane without humps.
      if profile == 0:
        z = 82
      elif profile == 3:
        z = 90 - 4 * math.sin(t * math.pi / 2) ** 2
      else:
        z = 86 + (4 if profile == 2 else -4) * math.cos(t * math.pi / 2)
      z += 2 * grade * t
      points.append((u, v, z))
    for a, b in zip(points, points[1:]):
      # A solid steel-gray conductor stays readable over grass without a halo.
      mesh.Beam(a, b, .65, False, (132, 138, 141))
  return mesh.Finish()


def BuildPowerSprites(existing):
  material = np.asarray(Image.open(SOURCE / "galvanized-steel.png").convert("RGB"), dtype=float)
  records = {}
  for direction in range(4):
    for profile in range(4):
      for grade in range(-GRADE_LIMIT, GRADE_LIMIT + 1):
        ident = WIRE_FIRST + (direction * 4 + profile) * GRADES + grade + GRADE_LIMIT
        records[ident] = Wire(material, direction, profile, grade)
  for shape in range(14):
    for form, (style, axis) in enumerate(TOWER_FORMS):
      records[TOWER_FIRST + shape * len(TOWER_FORMS) + form] = Tower(material, shape, style, axis)
  for axis in range(2):
    records[WATER_FIRST + axis] = Tower(material, 0, 4, axis)

  def composite(mask, shape=0):
    axis = 0 if mask == 5 else 1 if mask == 10 else 2
    form = axis if axis < 2 else 4 if mask.bit_count() <= 2 else 5
    image = records[TOWER_FIRST + shape * len(TOWER_FORMS) + form][0].copy()
    for direction in range(4):
      if mask & 1 << direction:
        wire = records[WIRE_FIRST + (direction * 4 + 3) * GRADES + GRADE_LIMIT][0]
        image.alpha_composite(wire)
    return image, 192, 4, 0

  for offset, mask in enumerate(TILE_MASKS):
    shape = offset - 1 if 2 <= offset <= 5 else 0
    records[14 + offset] = composite(mask, shape)
  records[92] = composite(10)
  for ident, base in ((67, 29), (68, 30), (71, 44), (72, 45), (79, 73), (80, 74)):
    records[ident] = existing[base]
  for ident in range(NATIVE_HIGHWAY_MERGE_FIRST + 6 * 3, NATIVE_HIGHWAY_MERGE_FIRST + 8 * 3):
    records[ident] = existing[ident - 6 * 3]
  return records


def Preview(records, existing, output):
  board = Image.new("RGBA", (1152, 1000), (35, 40, 43, 255))
  draw = ImageDraw.Draw(board)
  font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 20)
  draw.text((24, 16), "Power lines: upright lattice towers, transparent spans, individual concrete feet", font=font)
  for form, label in enumerate(("Suspension", "Rotated", "Heavy suspension", "Rotated", "Angle / end", "Junction")):
    x = 24 + form * 184
    draw.text((x, 54), label, font=font)
    image = records[TOWER_FIRST + form][0]
    board.alpha_composite(image, (x + 20, 82))

  places = {}
  for x in range(8): places[x, 1] = 5
  places[0, 1], places[7, 1] = 4, 1
  for y in range(1, 5): places[4, y] = 10
  places[4, 1] = 7
  places[4, 4] = 9
  for x in range(1, 4): places[x, 4] = 5
  places[1, 4] = 4
  ground_board = Image.new("RGBA", board.size)
  for u in range(-3, 10):
    for v in range(-3, 8):
      ground_board.alpha_composite(existing[542][0], (560 + 64 * (v - u), 420 + 32 * (u + v) - 128))
  board.alpha_composite(ground_board.crop((0, 275, 1152, 790)), (0, 275))
  for (u, v), mask in sorted(places.items(), key=lambda item: sum(item[0])):
    px, py = 560 + 64 * (v - u), 420 + 32 * (u + v)
    support = (u + v) % 4 == 0 or mask not in (5, 10)
    for direction in range(4):
      if mask & 1 << direction:
        nx, ny = u + STEPS[direction][0], v + STEPS[direction][1]
        other_mask = places.get((nx, ny), 0)
        other_support = (nx + ny) % 4 == 0 or other_mask not in (5, 10)
        wire = records[WIRE_FIRST + (direction * 4 + support * 2 + other_support) * GRADES + GRADE_LIMIT][0]
        board.alpha_composite(wire, (px, py - 192))
    if support:
      form = (0 if mask == 5 else 1) + 2 * bool((u * 13 + v * 7) & 4) if mask in (5, 10) else \
          4 if mask.bit_count() <= 2 else 5
      board.alpha_composite(records[TOWER_FIRST + form][0], (px, py - 192))
  draw.text((24, 810), "Four planar slopes: towers remain vertical; only footings follow the terrain", font=font)
  for shape in range(1, 5):
    px, py = 200 + (shape - 1) * 230, 980
    board.alpha_composite(existing[542 + shape][0], (px, py - 128))
    center = sum(GROUND_CORNERS[shape]) * 4
    axis = 1 if shape % 2 else 0
    board.alpha_composite(records[TOWER_FIRST + shape * 6 + axis][0], (px, py - 192 - center))
  board.save(output)


def SlopePreview(records, existing, output):
  board = Image.new("RGBA", (1152, 1800), (35, 40, 43, 255))
  draw = ImageDraw.Draw(board)
  font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 20)
  for slope in range(1, 5):
    draw.text((24, (slope - 1) * 450 + 12), f"Slope {slope}: connected cable grades and upright supports", font=font)
    axis = 1 if slope % 2 else 0
    uphill = slope >= 3
    tiles = {}
    for n in range(8):
      shape = slope if n in (2, 3) else 0
      height = max(0, min(2, n - 2)) if uphill else max(0, min(2, 3 - n))
      mask = (10 if axis else 5)
      if n == 0: mask &= 2 if axis else 4
      if n == 7: mask &= 8 if axis else 1
      tiles[(0, n) if axis else (n, 0)] = (shape, height, n % 4 == 0 or n in (0, 7), mask)
    for (u, v), (shape, height, support, mask) in tiles.items():
      px = (240 if axis else 740) + 64 * (v - u)
      py = (slope - 1) * 450 + 220 + 32 * (u + v) - 16 * height
      board.alpha_composite(existing[542 + shape][0], (px, py - 128))
    for (u, v), (shape, height, support, mask) in tiles.items():
      px = (240 if axis else 740) + 64 * (v - u)
      anchor = 4 * height + sum(GROUND_CORNERS[shape])
      py = (slope - 1) * 450 + 220 + 32 * (u + v) - 4 * anchor
      for direction in range(4):
        if not mask & 1 << direction: continue
        other = tiles.get((u + STEPS[direction][0], v + STEPS[direction][1]))
        grade = 4 * other[1] + sum(GROUND_CORNERS[other[0]]) - anchor
        profile = 2 * support + other[2]
        sprite = WIRE_FIRST + (direction * 4 + profile) * GRADES + grade + GRADE_LIMIT
        board.alpha_composite(records[sprite][0], (px, py - 192))
      if support:
        form = 4 if mask.bit_count() == 1 else axis
        board.alpha_composite(records[TOWER_FIRST + shape * 6 + form][0], (px, py - 192))
  board.save(output)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--catalog", type=Path, default=SPRITE_CATALOG)
  args = parser.parse_args()
  entries = json.loads(args.catalog.read_text())
  existing = {entry["id"]: (Image.open(args.catalog.parent / entry["file"]).convert("RGBA"),
      entry["baseline"], entry["scale_divisor"], entry["offset_x"]) for entry in entries}
  records = BuildPowerSprites(existing)
  ExportSpriteSources(args.catalog, records, retained=[entry for entry in entries if entry["id"] not in records])
  Preview(records, existing, SOURCE / "PREVIEW.png")
  SlopePreview(records, existing, SOURCE / "SLOPES.png")
  print(f"{END - WIRE_FIRST} baked cable/support pieces; 22 base tile kinds recreated; tile width 128px")


if __name__ == "__main__":
  main()
