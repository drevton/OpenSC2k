#!/usr/bin/env python3
"""Bake compact lane positions/headings from the road artwork's shared geometry."""
import argparse
import bisect
import math
import json
from pathlib import Path


def DiagonalCoefficients(mask, extended=False):
  """Centreline polynomial in the road painter's (u, v) coordinates."""
  if extended:
    # A cubic's control edges turn steadily from diagonal to axis-aligned.
    # Both halves meet their physical tile boundary exactly at t = 1/2.
    if mask == 1:
      return (.5, 0), (1.05, 1.05), (-.15, -.6), (.1, .05), (0, 0), (0, 0)
    return (.5, -.5), (0, 1.05), (.45, -.15), (.05, .1), (0, 0), (0, 0)
  origin = (.5, 0)
  start = (.5, .5) if mask & 1 else (0, .75)
  end = (.5, .5) if mask & 2 else (.75, 0)
  delta = (.5, .5)
  a = tuple(10 * v - 6 * s - 4 * e for v, s, e in zip(delta, start, end))
  b = tuple(-15 * v + 8 * s + 7 * e for v, s, e in zip(delta, start, end))
  c = tuple(6 * v - 3 * s - 3 * e for v, s, e in zip(delta, start, end))
  return origin, start, (0, 0), a, b, c


def Diagonal(t, mask, extended):
  origin, start, q, a, b, c = DiagonalCoefficients(mask, extended)
  point = tuple(o + s * t + q * t ** 2 + a * t ** 3 + b * t ** 4 + c * t ** 5
      for o, s, q, a, b, c in zip(origin, start, q, a, b, c))
  tangent = tuple(s + 2 * q * t + 3 * a * t ** 2 + 4 * b * t ** 3 + 5 * c * t ** 4
      for s, q, a, b, c in zip(start, q, a, b, c))
  return (*point[::-1], *tangent[::-1])


def Rotate(point, rotation):
  x, y, dx, dy = point
  for _ in range(rotation & 3):
    x, y, dx, dy = y, 1 - x, dy, -dx
  return x, y, dx, dy


def Prepared(phase, mask, extended, approach, reverse, rotation=0):
  q = 1 - phase if reverse else phase
  cut = .5
  t, rate = q, 1
  if approach and ((mask == 1 and q >= .5) or (mask == 2 and q <= .5)):
    point = (.5, q, 0, 1) if mask == 1 else (q, .5, 1, 0)
  else:
    if extended:
      if approach:
        rate = (1 - cut) * 2
        t = cut + q * rate if mask == 1 else (q - .5) * rate
      else:
        rate = cut
        t = q * rate if mask == 1 else 1 - cut + q * rate
    x, y, dx, dy = Diagonal(t, mask, extended)
    if approach:
      if mask == 1:
        y -= 1
      else:
        x += 1
    point = x, y, dx * rate, dy * rate
  x, y, dx, dy = Rotate(point, rotation)
  return (x, y, -dx, -dy) if reverse else (x, y, dx, dy)


def Shared(phase, shared, reverse):
  approaches = ((((1, 1), (2, 2)), ((0, 2), (3, 1))),
      (((0, 1), (1, 2)), ((2, 1), (3, 2))))
  axis = shared // 4
  entry = ((3 if axis else 0) + (2 if reverse else 0)) & 3
  side = (1 - phase if reverse else phase) >= .5
  turn = shared & 1 if side else (shared >> 1) & 1
  rotation, mask = approaches[axis][side][turn]
  backwards = (entry - rotation) & 3 != (3 if mask == 1 else 0)
  return Prepared(phase, mask, True, True, backwards, rotation)


def Curve(phase, reverse):
  angle = (1 - phase if reverse else phase) * math.pi / 2
  sign = -1 if reverse else 1
  return (.5 * math.sin(angle), 1 - .5 * math.cos(angle),
      sign * math.cos(angle), sign * math.sin(angle))


def Turnaround(phase):
  angle = phase * math.pi
  return .1875 * math.sin(angle), .5 - .1875 * math.cos(angle), math.cos(angle), math.sin(angle)


def BakeLane(function, ports, offset=True):
  def position(t):
    x, y, dx, dy = function(t)
    if offset:
      length = math.hypot(dx, dy)
      x, y = x + .1875 * dy / length, y - .1875 * dx / length
    return x, y, dx, dy

  def boundary(point, side):
    x, y, dx, dy = point
    distance = ((0 if side == 0 else 1) - x) / dx if side % 2 == 0 else \
        ((1 if side == 1 else 0) - y) / dy
    return x + distance * dx, y + distance * dy

  start, end = position(0), position(1)
  first, last = boundary(start, ports[0]), boundary(end, ports[1])
  before = first[0] - start[0], first[1] - start[1]
  after = last[0] - end[0], last[1] - end[1]
  points = []
  # Shift the endpoints onto their real tile edges. Smooth endpoint corrections
  # retain the road's entry/exit tangents without reading a neighbouring path.
  for step in range(4097):
    t = step / 4096
    x, y, *_ = position(t)
    blend = t * t * (3 - 2 * t)
    x += before[0] * (1 - blend) + after[0] * blend
    y += before[1] * (1 - blend) + after[1] * blend
    if min(x, y) < -1e-8 or max(x, y) > 1 + 1e-8:
      raise ValueError('A baked road path leaves its physical tile')
    points.append((x, y))
  # An inside offset folds when the bend radius is smaller than the lane offset.
  # Keep both boundary positions and tangents, but join them with a regular cubic.
  folded = any(sum((points[i + 1][axis] - points[i - 1][axis]) * function(i / 4096)[axis + 2]
      for axis in range(2)) < 0 for i in range(1, 4096))
  if folded:
    handle = math.dist(first, last) / 3
    controls = [first,
        tuple(first[axis] + handle * start[axis + 2] / math.hypot(*start[2:]) for axis in range(2)),
        tuple(last[axis] - handle * end[axis + 2] / math.hypot(*end[2:]) for axis in range(2)), last]
    points = [tuple(sum(controls[k][axis] * weight for k, weight in enumerate(
        ((1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t ** 2 * (1 - t), t ** 3)))
        for axis in range(2)) for t in (i / 4096 for i in range(4097))]
  lengths = [0.0]
  for a, b in zip(points, points[1:]):
    lengths.append(lengths[-1] + math.dist(a, b))
  headings = []
  for step in range(4097):
    if step in (0, 4096):
      dx, dy = (start if step == 0 else end)[2:]
    else:
      dx, dy = (points[step + 1][i] - points[step - 1][i] for i in range(2))
    headings.append(math.atan2(dy, dx) * 256 / math.tau)
  # Quantize a straight lane's endpoint delta once. Independent X/Y rounding
  # otherwise changes its lateral position by one Q8 unit along a diagonal.
  straight = all(abs((x - first[0]) * (last[1] - first[1]) -
      (y - first[1]) * (last[0] - first[0])) < 1e-10 for x, y in points)
  endpoints = [(round(first[axis] * 256), round(last[axis] * 256)) for axis in range(2)]
  samples = []
  for step in range(33):
    distance = lengths[-1] * step / 32
    index = min(4095, max(0, bisect.bisect_right(lengths, distance) - 1))
    fraction = (distance - lengths[index]) / (lengths[index + 1] - lengths[index])
    x, y = points[index]
    nx, ny = points[index + 1]
    yaw = headings[index]
    yaw += ((headings[index + 1] - yaw + 128) % 256 - 128) * fraction
    position = tuple(a + round((b - a) * step / 32) for a, b in endpoints) if straight else (
        round((x + (nx - x) * fraction) * 256), round((y + (ny - y) * fraction) * 256))
    samples.append((*position, round(yaw) & 255))
  return samples, round(lengths[-1] * 256), ports


def PackSample(point):
  x, y, yaw = point
  if not (0 <= x <= 256 and 0 <= y <= 256):
    raise ValueError(f'Car path leaves tile: {point}')
  return x | y << 9 | yaw << 18


def LaneRecipes():
  lanes = [BakeLane(lambda t, r=r: Curve(t, r), (1, 0) if r else (0, 1)) for r in range(2)]
  for extended, approach, masks in ((False, False, 3), (True, False, 2), (True, True, 2)):
    for mask in range(1, masks + 1):
      for reverse in range(2):
        ports = (3, 1) if approach and mask == 1 else (0, 2) if approach else (0, 1)
        lanes.append(BakeLane(lambda t, m=mask, r=reverse: Prepared(t, m, extended, approach, r),
            ports[::-1] if reverse else ports))
  for shared in range(8):
    for reverse in range(2):
      entry = ((3 if shared // 4 else 0) + reverse * 2) & 3
      lanes.append(BakeLane(lambda t, s=shared, r=reverse: Shared(t, s, r), (entry, (entry + 2) & 3)))
  lanes.append(BakeLane(Turnaround, (0, 0), offset=False))
  lanes.append(BakeLane(lambda t: (t, .3125, 1, 0), (0, 2), offset=False))
  return lanes


def CanonicalLanes(lanes):
  unique, descriptors, names = [], [], {}
  for points, length, ports in lanes:
    variants = []
    for rotation in range(4):
      rotated = []
      for x, y, yaw in points:
        for _ in range(rotation):
          x, y, yaw = y, 256 - x, (yaw - 64) & 255
        rotated.append(PackSample((x, y, yaw)))
      sides = tuple((side + rotation) & 3 for side in ports)
      variants.append((tuple(rotated), length, sides))
    rotation = min(range(4), key=variants.__getitem__)
    key = variants[rotation]
    if key not in names:
      names[key] = len(unique)
      unique.append(key)
    descriptors.append((names[key] + 1) | ((-rotation & 3) << 5))
  if len(unique) > 31:
    raise ValueError('Lane index and inactive sentinel do not fit five bits')
  return unique, descriptors


def Bake(output):
  lanes = LaneRecipes()
  unique, descriptors = CanonicalLanes(lanes)
  text = ['// Generated by tools/bake_road_car_paths.py; do not edit.',
      '// Tile-local points pack Q8 X/Y (9 bits each) and world yaw (8 bits).',
      '// Every path starts and ends at a tile edge, with unchanged road tangents.',
      f'static const u32 kRoadCarPathPoints[{len(unique)}][33] = {{']
  for points, *_ in unique:
    text.append('    {')
    for first in range(0, 33, 8):
      text.append('        ' + ', '.join(f'0x{p:07X}' for p in points[first:first + 8]) + ',')
    text.append('    },')
  text += ['};', 'static const u16 kRoadCarPathLengths[] = {',
      '    ' + ', '.join(str(length) for _, length, _ in unique) + ',', '};',
      'static const u8 kRoadCarPathPorts[] = {',
      '    ' + ', '.join(str(ports[0] | ports[1] << 2) for _, _, ports in unique) + ',', '};',
      'static const u8 kRoadCarPathKinds[] = {']
  for first in range(0, len(descriptors), 16):
    text.append('    ' + ', '.join(str(d) for d in descriptors[first:first + 16]) + ',')
  text += ['};', f'enum {{ ROAD_CAR_STRAIGHT_PATH = {descriptors[-1] & 31} }};',
      'static const u16 kRoadCarDirectionQuarter[65] = {']
  directions = [round(math.sin(i * math.pi / 128) * 256) for i in range(65)]
  for first in range(0, 65, 16):
    text.append('    ' + ', '.join(str(d) for d in directions[first:first + 16]) + ',')
  text.append('};')
  manifest = json.loads((Path(__file__).resolve().parents[1] /
      'assets/landscape/vehicles/blender/manifest.json').read_text())
  # The runtime front axle follows the lane; the rear axle determines body yaw.
  # Keep both model measurements at Q12 so sprite registration shares that geometry.
  for name, field in [('FrontAxles', 'front_axle_offsets'), ('RearAxles', 'rear_axle_offsets')]:
    offsets = [round(offset * .75 * 4096) for offset in manifest[field]]
    text += [f'static const u16 kRoadCar{name}[] = {{',
        '    ' + ', '.join(str(v) for v in offsets) + ',', '};']
  headings = []
  for frame in manifest['frames'][:manifest['views']]:
    angle = math.radians(frame['screen_angle'])
    sx, sy = math.cos(angle), math.sin(angle)
    headings.append(round(math.atan2(2 * sy + sx, 2 * sy - sx) * 65536 / math.tau) & 65535)
  text += ['static const u16 kRoadCarViewHeadings[] = {',
      '    ' + ', '.join(str(v) for v in headings) + ',', '};']
  poses = []
  for yaw in range(256):
    angle = yaw * math.tau / 256
    screen = math.atan2((math.cos(angle) + math.sin(angle)) / 2, math.sin(angle) - math.cos(angle))
    poses.append(min(range(len(headings)), key=lambda view:
        abs((math.radians(manifest['frames'][view]['screen_angle']) - screen + math.pi) % math.tau - math.pi)))
  text.append('static const u8 kRoadCarPoseViews[256] = {')
  for first in range(0, 256, 16):
    text.append('    ' + ', '.join(str(v) for v in poses[first:first + 16]) + ',')
  text.append('};')
  output.parent.mkdir(parents=True, exist_ok=True)
  output.write_text('\n'.join(text) + '\n')
  size = len(unique) * (33 * 4 + 3) + len(descriptors) + 130 + 4 * len(offsets) + 2 * len(headings) + len(poses)
  print(f'Baked {len(lanes)} lane recipes into {len(unique)} tile-local paths ({size} immutable bytes)')


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('output', type=Path)
  Bake(parser.parse_args().output)


if __name__ == '__main__':
  main()
