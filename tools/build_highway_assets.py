"""Offline highway meshes using the approved road paint and asphalt material.

Coordinates are map tiles; elevation is in pixels at 128px per tile. Finished
RGBA sprites retain the existing highway IDs, footprint anchors and mirroring.
Nothing in this module runs in the game.
"""
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from build_landscape_assets import (NATIVE_HIGHWAY_MERGE_FIRST, NATIVE_HIGHWAY_SLOPE_FIRST,
    NATIVE_DIAGONAL_HIGHWAY_FIRST)

DETAIL = 4
DECK_HEIGHT = 48
RAMP_TOP_HALF_WIDTH = .29
NAMES = {73: "highway-a", 74: "highway-b", 75: "highway-crossing-a", 76: "highway-crossing-b",
    77: "highway-rail-a", 78: "highway-rail-b", 79: "highway-power-a", 80: "highway-power-b",
    93: "highway-ramp-1", 94: "highway-ramp-2", 95: "highway-ramp-3", 96: "highway-ramp-4",
    97: "highway-incline-1", 98: "highway-incline-2", 99: "highway-incline-3", 100: "highway-incline-4",
    101: "highway-bend-1", 102: "highway-bend-2", 103: "highway-bend-3", 104: "highway-bend-4",
    105: "highway-interchange", 106: "highway-bridge-a", 107: "highway-bridge-b"}
MERGE_NAMES = {NATIVE_HIGHWAY_MERGE_FIRST + (ident - 73) * 3 + mask - 1:
    f"{NAMES[ident]}-merge-{mask}" for ident in range(73, 81) for mask in range(1, 4)}
SLOPE_NAMES = {NATIVE_HIGHWAY_SLOPE_FIRST + slope - 1: f"highway-compact-slope-{slope}"
    for slope in range(1, 5)}
DIAGONAL_NAMES = {NATIVE_DIAGONAL_HIGHWAY_FIRST + rotation * 3 + mask - 1:
    f"highway-diagonal-{rotation + 1}-{mask}" for rotation in range(4) for mask in range(1, 4)}


class HighwayMesh:
  def __init__(self, tiles, texture, profile, concrete, footprint, height=None):
    self.tiles = tiles
    self.width = 128 * tiles
    self.height = height if height is not None else 128 if tiles == 1 else 224
    self.baseline = self.height
    self.texture, self.profile, self.concrete = texture, profile, concrete
    self.texture_mean = texture.mean(axis=(0, 1))
    self.pixels = np.zeros((self.height * DETAIL, self.width * DETAIL, 4), dtype=np.uint8)
    self.depth = np.full(self.pixels.shape[:2], -1e10)
    self.ground_mask = np.zeros((self.height, self.width), dtype=bool)
    for u in range(tiles):
      for v in range(tiles):
        left = self.width // 2 - 64 + 64 * (u - v)
        top = self.baseline - self.width // 2 + 32 * (u + v)
        self.ground_mask[top:top + 64, left:left + 128] |= footprint

  def Project(self, point):
    u, v, z = point[:3]
    return np.array((self.width / 2 + 64 * (u - v), self.baseline - self.width / 2 + 32 * (u + v) - z,
        u + v + z / 32))

  def Triangle(self, points, color):
    a, b, c = [self.Project(point) for point in points]
    left = max(0, math.floor(min(a[0], b[0], c[0]) * DETAIL))
    right = min(self.width * DETAIL, math.ceil(max(a[0], b[0], c[0]) * DETAIL))
    top = max(0, math.floor(min(a[1], b[1], c[1]) * DETAIL))
    bottom = min(self.height * DETAIL, math.ceil(max(a[1], b[1], c[1]) * DETAIL))
    if right <= left or bottom <= top:
      return
    yy, xx = np.mgrid[top:bottom, left:right]
    xx, yy = (xx + .5) / DETAIL, (yy + .5) / DETAIL
    denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if abs(denominator) < .000001:
      return
    wa = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / denominator
    wb = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / denominator
    wc = 1 - wa - wb
    depth = wa * a[2] + wb * b[2] + wc * c[2]
    visible = (wa >= -1e-7) & (wb >= -1e-7) & (wc >= -1e-7) & (depth >= self.depth[top:bottom, left:right])
    surface = color.startswith("asphalt") if isinstance(color, str) else color[0] >= 218
    if surface and points[0][2] == points[1][2] == points[2][2]:
      # Elevated road ends use the same nearest-neighbour ownership as ground.
      # Each final pixel is wholly assigned to one tile, even though shoulders
      # and material are supersampled within that footprint.
      mx = np.floor(xx).astype(int)
      my = np.floor(yy + round(points[0][2])).astype(int)
      inside = (my >= 0) & (my < self.height)
      visible &= inside & self.ground_mask[np.clip(my, 0, self.height - 1), mx]
    if not visible.any():
      return
    weights = (wa, wb, wc)
    if isinstance(color, str):
      from build_road_assets import SampleMaterial
      # Travel and lateral texture coordinates are interpolated across the mesh,
      # never restarted at a triangle or carriageway join.
      s, t = (sum(weight * point[axis] for weight, point in zip(weights, points)) for axis in (3, 4))
      if color.startswith("asphalt"):
        base = np.interp(np.clip(t, 0, 1) * (len(self.profile) - 1), np.arange(len(self.profile)), self.profile)
        # A merge has no dark asphalt shoulder crossing its open pavement.
        middle = self.profile[len(self.profile) // 2]
        if color == "asphalt-flat": base = np.full(t.shape, middle)
        elif color == "asphalt-open-negative": base = np.where(t < .5, middle, base)
        elif color == "asphalt-open-positive": base = np.where(t > .5, middle, base)
        sample = SampleMaterial(s % 1, t, self.texture)
        rgb = np.clip(np.rint(base[..., None] + (sample - self.texture_mean) * 2.15), 0, 255)
      else:
        ix, iy = (np.floor(value * 127).astype(int) % 128 for value in (s, t))
        rgb = self.concrete[iy, ix] * (.73 if color == "concrete" else .48)
    else:
      rgb = np.broadcast_to(color, (*wa.shape, 3))
    pixels = self.pixels[top:bottom, left:right]
    pixels[..., :3][visible] = np.clip(rgb[visible], 0, 255).astype(np.uint8)
    pixels[..., 3][visible] = 255
    self.depth[top:bottom, left:right][visible] = depth[visible]

  def Quad(self, points, color):
    self.Triangle((points[0], points[1], points[2]), color)
    self.Triangle((points[0], points[2], points[3]), color)

  def Box(self, u, v, half_u, half_v, bottom, top, color=(136, 133, 123)):
    a, b, c, d = (u - half_u, v - half_v), (u + half_u, v - half_v), \
        (u + half_u, v + half_v), (u - half_u, v + half_v)
    for p, q, shade in ((a, b, .64), (b, c, .76), (c, d, .90), (d, a, .72)):
      self.Quad([(*p, bottom), (*q, bottom), (*q, top), (*p, top)], tuple(value * shade for value in color))
    self.Quad([(*p, top) for p in (a, b, c, d)], tuple(min(255, value * 1.08) for value in color))

  def Ribbon(self, path, half_width=.375, supports=True, marking=True, rails=True, support_positions=None,
      normals=None, widths=None, embankment=False, edge_gaps=(), side_gaps=(), surface="asphalt", fit_dashes=False,
      edge_marking=True):
    path = np.asarray(path, dtype=float)
    tangent = np.gradient(path[:, :2], axis=0)
    tangent /= np.linalg.norm(tangent, axis=1)[:, None]
    normal = np.stack((tangent[:, 1], -tangent[:, 0]), axis=-1) if normals is None else np.asarray(normals)
    distance = np.r_[0, np.cumsum(np.linalg.norm(np.diff(path[:, :2], axis=0), axis=1))]

    def edge_open(side, at):
      return any(start <= at <= end for start, end in edge_gaps) or \
          any(edge == side and start <= at <= end for edge, start, end in side_gaps)

    def point(i, across, lift=0):
      u, v = path[i, :2] + normal[i] * across * (widths[i] / half_width if widths is not None else 1)
      if i == 0 or i == len(path) - 1:
        u, v = np.array((u, v)) + tangent[i] * (-.02 if i == 0 else .02)
      return (u, v, path[i, 2] + lift, distance[i], .5 + across / (2 * half_width))

    for i in range(len(path) - 1):
      pavement = surface
      open_sides = [side for side in (-1, 1)
          if any(edge == side and start <= distance[i] <= end for edge, start, end in side_gaps)]
      if len(open_sides) == 2: pavement = "asphalt-flat"
      elif open_sides: pavement = "asphalt-open-negative" if open_sides[0] < 0 else "asphalt-open-positive"
      self.Quad([point(i, -half_width), point(i + 1, -half_width),
          point(i + 1, half_width), point(i, half_width)], pavement)
      for side in (-1, 1):
        if any(edge == side and start <= distance[i] <= end for edge, start, end in side_gaps):
          continue
        edge = side * half_width
        self.Quad([point(i, edge, -min(5, path[i, 2]) if embankment else -5),
            point(i + 1, edge, -min(5, path[i + 1, 2]) if embankment else -5),
            point(i + 1, edge), point(i, edge)], "concrete")
        if embankment:
          self.Quad([point(i, edge, -path[i, 2]), point(i + 1, edge, -path[i + 1, 2]),
              point(i + 1, edge, -min(5, path[i + 1, 2])), point(i, edge, -min(5, path[i, 2]))],
              "concrete" if side < 0 else "shadow")
      if marking:
        # Two dashes per tile. BOX reduction has no ringing or centre-line ghosts.
        # Short ramps fit two complete, equally spaced dashes to their arc;
        # the ordinary repeating pattern would clip the second dash at the end.
        paint_distance = (distance[i] + distance[i + 1]) / (2 * distance[-1]) if fit_dashes else distance[i]
        if .15 <= paint_distance % .5 <= .35:
          self.Quad([point(i, -.0225, .025), point(i + 1, -.0225, .025),
              point(i + 1, .0225, .025), point(i, .0225, .025)], (245, 245, 242))
        for side in (-1, 1):
          if not edge_marking or edge_open(side, distance[i]):
            continue
          edge = side * (half_width - .045)
          self.Quad([point(i, edge - .008, .025), point(i + 1, edge - .008, .025),
              point(i + 1, edge + .008, .025), point(i, edge + .008, .025)], (218, 218, 211))
      if rails:
        for side in (-1, 1):
          if edge_open(side, distance[i]):
            continue
          edge = side * (half_width + .008)
          self.Quad([point(i, edge, 2), point(i + 1, edge, 2),
              point(i + 1, edge, 4), point(i, edge, 4)], (127, 134, 134))
          self.Quad([point(i, edge - .009, 4), point(i + 1, edge - .009, 4),
              point(i + 1, edge + .009, 4), point(i, edge + .009, 4)], (190, 194, 188))
    if rails:
      for s in np.arange(.12, distance[-1], .24):
        i = int(np.argmin(np.abs(distance - s)))
        for side in (-1, 1):
          if edge_open(side, s):
            continue
          u, v, z = point(i, side * (half_width + .008))[:3]
          self.Box(u, v, .009, .009, z, z + 4, (106, 113, 113))
    if supports:
      positions = np.arange(.5, distance[-1], 1) if support_positions is None else support_positions
      for s in positions:
        i = int(np.argmin(np.abs(distance - s)))
        u, v, z = path[i]
        if z > 18:
          self.Box(u, v, .065, .065, 0, z - 5)
          for side in (-1, 1):
            a, b = point(i, side * .29, -8), point(i, side * .29, -5)
            self.Box(a[0], a[1], .045, .045, a[2], b[2])

  def Finish(self):
    image = Image.fromarray(self.pixels).resize((self.width, self.height), Image.Resampling.BOX)
    # Remove RGB in alpha-zero padding before packaging the finished streams.
    pixels = np.array(image)
    pixels[pixels[..., 3] == 0] = 0
    return Image.fromarray(pixels), self.baseline, 4, 0


def Straight(center, length, axis=0, height=DECK_HEIGHT, slope=0):
  result = []
  for s in np.linspace(0, length, round(length * 128) + 1):
    u, v = (center, s) if axis == 0 else (s, center)
    z = height
    if slope:
      a, b, c = ((1, -1, 0), (1, 0, -1), (0, 1, 0), (0, 0, 1))[slope - 1]
      z += 16 * (a + b * u / length + c * v / length)
    result.append((u, v, z))
  return result


def Bend(radius, rotation=0):
  result = []
  for t in np.linspace(0, 1, 193):
    angle = t * math.pi / 2
    u, v = 2 - radius * math.cos(angle), radius * math.sin(angle)
    for _ in range(rotation):
      u, v = 2 - v, u
    result.append((u, v, DECK_HEIGHT))
  return result


def DiagonalBend(radius, rotation, mask):
  path, normals = [], []
  for t in np.linspace(0, 1, 193):
    # Opposite neighbouring bends share a straight chord. Only the unconnected
    # end of a diagonal run turns back to the cardinal highway approach.
    u = 2 - radius + radius * (2 * t * t - t ** 3 if mask == 2 else t)
    v = radius * (t + t * t - t ** 3 if mask == 1 else t)
    normal = np.array((1 - t, -t))
    for _ in range(rotation):
      u, v = 2 - v, u
      normal = np.array((-normal[1], normal[0]))
    path.append((u, v, DECK_HEIGHT))
    # Skew the cross-section to match the full cardinal width at each tile
    # edge. For a chord these keep both shoulders exactly straight and parallel.
    normals.append(normal)
  return path, normals


def Ramp(rotation, reverse):
  # Cardinal tangents at both ends, with a broad circular sweep. The deck-side
  # fan continues this turn into the highway instead of ending at its edge.
  path, normals = [], []
  for t in np.linspace(0, 1, 193):
    angle = t * math.pi / 2
    u, v = 1 - .5 * math.cos(angle), .5 * math.sin(angle)
    normal = np.array((math.cos(angle), -math.sin(angle)))
    for _ in range(rotation):
      u, v = 1 - v, u
      normal = np.array((-normal[1], normal[0]))
    grade = t * t * (3 - 2 * t)
    path.append((u, v, DECK_HEIGHT * (grade if reverse else 1 - grade)))
    normals.append(normal)
  return path, normals


def RampWidths(path, normals):
  path, normals = np.asarray(path), np.asarray(normals)
  widths = RAMP_TOP_HALF_WIDTH + (.375 - RAMP_TOP_HALF_WIDTH) * (1 - path[:, 2] / DECK_HEIGHT)
  projected = np.column_stack((64 * (path[:, 0] - path[:, 1]),
      32 * (path[:, 0] + path[:, 1]) - path[:, 2]))
  tangent = np.gradient(projected, axis=0, edge_order=2)
  across = np.column_stack((64 * (normals[:, 0] - normals[:, 1]),
      32 * (normals[:, 0] + normals[:, 1])))
  visible_width = 2 * widths * abs(tangent[:, 0] * across[:, 1] - tangent[:, 1] * across[:, 0]) / \
      np.linalg.norm(tangent, axis=1)
  travel = np.r_[0, np.cumsum(np.linalg.norm(np.diff(path[:, :2], axis=0), axis=1))]
  blend = travel / travel[-1]
  blend = blend * blend * (3 - 2 * blend)
  target_width = visible_width[0] + (visible_width[-1] - visible_width[0]) * blend
  # The steep curved deck otherwise balloons in projection. Reduce only that
  # excess; preserve foreshortening and both exact road/highway end widths.
  widths *= np.minimum(1, target_width / np.maximum(visible_width, .000001))
  # Join the corrected taper with zero width derivative at each flat road end.
  # Match the interior value and derivative with a short cubic Hermite blend;
  # abruptly starting the projection correction would kink both deck and rail.
  steps = round((len(widths) - 1) * .15)
  t = np.linspace(0, 1, steps + 1)
  for end, direction in ((0, 1), (len(widths) - 1, -1)):
    join = end + direction * steps
    derivative = (widths[join + direction] - widths[join - direction]) * steps / 2
    widths[end + direction * np.arange(steps + 1)] = \
        (2 * t ** 3 - 3 * t ** 2 + 1) * widths[end] + \
        (-2 * t ** 3 + 3 * t ** 2) * widths[join] + (t ** 3 - t ** 2) * derivative
  return widths


def AddMergeGuardrails(mesh, axis, gaps):
  def point(across, along, height):
    u, v = (across, along) if axis == 0 else (along, across)
    return (u, v, height, along, .5 + (across - .5) / .75 * (1 if axis == 0 else -1))

  for side, start, end in gaps:
    boundary = .5 + side * .5
    deck_edge = .5 + side * .375
    rail_edge = deck_edge + side * .008
    for direction, join in ((-1, start - .008), (1, end + .008)):
      pavement, rail = [], []
      for angle in np.linspace(0, math.pi / 2, 49):
        turn = 1 - math.cos(angle)
        deck_mouth = .5 + direction * RAMP_TOP_HALF_WIDTH
        rail_mouth = .5 + direction * RAMP_TOP_HALF_WIDTH * (1 + .008 / .375)
        pavement.append(point(boundary + (deck_edge - boundary) * math.sin(angle),
            deck_mouth + (join - deck_mouth) * turn, DECK_HEIGHT))
        rail.append(point(boundary + (rail_edge - boundary) * math.sin(angle),
            rail_mouth + (join - rail_mouth) * turn, DECK_HEIGHT))
      # Fill the small outer fillet between the turning fan and the through
      # deck. Its rail meets the ramp's scaled rail exactly at the tile edge.
      for p, q in zip(pavement, pavement[1:]):
        along_p, along_q = (p[1], q[1]) if axis == 0 else (p[0], q[0])
        mesh.Quad([p, q, point(deck_edge, along_q, DECK_HEIGHT),
            point(deck_edge, along_p, DECK_HEIGHT)], "asphalt-flat")
        mesh.Quad([(*p[:2], DECK_HEIGHT - 5, *p[3:]),
            (*q[:2], DECK_HEIGHT - 5, *q[3:]), q, p], "concrete")
      rail = np.asarray(rail)
      tangent = np.gradient(rail[:, :2], axis=0)
      tangent /= np.linalg.norm(tangent, axis=1)[:, None]
      normal = np.column_stack((tangent[:, 1], -tangent[:, 0]))
      bar_width = np.linspace(.009 * RAMP_TOP_HALF_WIDTH / .375, .009, len(rail))
      for i, (p, q) in enumerate(zip(rail, rail[1:])):
        mesh.Quad([(p[0], p[1], DECK_HEIGHT + 2), (q[0], q[1], DECK_HEIGHT + 2),
            (q[0], q[1], DECK_HEIGHT + 4), (p[0], p[1], DECK_HEIGHT + 4)], (127, 134, 134))
        a, b = normal[i] * bar_width[i], normal[i + 1] * bar_width[i + 1]
        mesh.Quad([(*(p[:2] - a), DECK_HEIGHT + 4), (*(q[:2] - b), DECK_HEIGHT + 4),
            (*(q[:2] + b), DECK_HEIGHT + 4), (*(p[:2] + a), DECK_HEIGHT + 4)], (190, 194, 188))
      p = rail[len(rail) // 2]
      mesh.Box(p[0], p[1], .009, .009, DECK_HEIGHT, DECK_HEIGHT + 4, (106, 113, 113))


def AddInterchange(mesh):
  """A complete cloverleaf: two through routes and eight one-way turning ramps."""
  def upper_height(at):
    return DECK_HEIGHT + 32 * math.sin(math.pi * at / 2) ** 2

  def rotate(u, v, rotation):
    for _ in range(rotation):
      u, v = 2 - v, u
    return u, v

  # Narrow the carriageways within the interchange to leave room for loops.
  # All eight ends retain the neighbouring highway's centre, width and height.
  for axis in range(2):
    for side in (-1, 1):
      path, widths, normals = [], [], []
      for at in np.linspace(0, 2, 257):
        blend = min(at, 2 - at, .4) / .4
        blend = blend * blend * (3 - 2 * blend)
        across = 1 + side * (.5 - .3 * blend)
        edge = min(at, 2 - at, .4) / .4
        derivative = -side * .3 * 6 * edge * (1 - edge) / .4 * (1 if at < 1 else -1)
        normal = np.array((1, -derivative) if axis == 0 else (derivative, -1))
        normals.append(normal / np.linalg.norm(normal))
        u, v = (across, at) if axis == 0 else (at, across)
        path.append((u, v, DECK_HEIGHT if axis == 0 else upper_height(at)))
        widths.append(.375 - .235 * blend)
      mesh.Ribbon(path, widths=widths, normals=normals, supports=False,
          edge_gaps=((.32, .77), (1.3, 1.8)))
      # Carry both approaches down to the ground, away from the turning lanes.
      for index in (20, 236):
        u, v, z = path[index]
        mesh.Box(u, v, .07, .07, 0, 2)
        mesh.Box(u, v, .055, .055, 2, z - 5)
  # The overpass supports occupy the median, clear of every through lane.
  for across in (.8, 1.2):
    mesh.Box(across, 1, .04, .04, 0, DECK_HEIGHT - 5)
    mesh.Box(1, across, .04, .04, 0, DECK_HEIGHT + 27)

  for rotation in range(4):
    # The outside quarter-turn handles right turns. Its endpoints join the
    # approaches tangentially, with no guardrail across the shared pavement.
    right = []
    for t in np.linspace(0, 1, 97):
      angle = t * math.pi / 2
      u, v = .4 + .4 * math.cos(angle), .4 + .4 * math.sin(angle)
      blend = t * t * (3 - 2 * t)
      z = DECK_HEIGHT + (upper_height(.4) - DECK_HEIGHT) * (1 - blend if rotation % 2 else blend)
      right.append((*rotate(u, v, rotation), z))
    mesh.Ribbon(right, half_width=.075, supports=False, marking=False,
        edge_gaps=((0, .12), (.50, .7)))

    # A 270-degree loop connects each incoming carriageway to the opposite
    # highway's outgoing carriageway, supplying the otherwise missing left turn.
    left = []
    for t in np.linspace(0, 1, 193):
      angle = t * math.pi * 1.5
      u, v = .45 + .35 * math.cos(angle), 1.55 + .35 * math.sin(angle)
      blend = t * t * (3 - 2 * t)
      z = DECK_HEIGHT + (upper_height(.45) - DECK_HEIGHT) * (1 - blend if rotation % 2 else blend)
      left.append((*rotate(u, v, rotation), z))
    mesh.Ribbon(left, half_width=.075, supports=False, marking=False,
        edge_gaps=((0, .13), (1.5, 1.8)))
    # The outer half of each loop is clear of the through lanes below.
    # Support its two outer bends rather than putting columns through traffic.
    for index in (96, 160):
      u, v, z = left[index]
      mesh.Box(u, v, .065, .065, 0, 2)
      mesh.Box(u, v, .04, .04, 2, z - 5)


def BuildHighways(records, source, footprint, identifiers=NAMES | MERGE_NAMES | SLOPE_NAMES | DIAGONAL_NAMES,
    underpasses=True):
  import json
  texture = np.asarray(Image.open(source / "asphalt-material.png").convert("RGB"), dtype=float)
  concrete = np.asarray(Image.open(source / "tunnel-material.png").convert("RGB"), dtype=float)
  profile = json.loads((source / "lighting-profile.json").read_text())["samples"]
  for ident in identifiers:
    merge = 0
    output_id = ident
    if ident in DIAGONAL_NAMES:
      rotation, mask = divmod(ident - NATIVE_DIAGONAL_HIGHWAY_FIRST, 3)
      mesh = HighwayMesh(2, texture, profile, concrete, footprint)
      for radius in (.5, 1.5):
        path, normals = DiagonalBend(radius, rotation, mask + 1)
        mesh.Ribbon(path, normals=normals)
      records[output_id] = mesh.Finish()
      continue
    if ident in SLOPE_NAMES:
      slope = ident - NATIVE_HIGHWAY_SLOPE_FIRST + 1
      mesh = HighwayMesh(1, texture, profile, concrete, footprint, height=144)
      mesh.Ribbon(Straight(.5, 1, slope & 1, slope=slope))
      records[output_id] = mesh.Finish()
      continue
    if ident in MERGE_NAMES:
      ident, merge = 73 + (ident - NATIVE_HIGHWAY_MERGE_FIRST) // 3, \
          (ident - NATIVE_HIGHWAY_MERGE_FIRST) % 3 + 1
    tiles = 2 if ident >= 97 else 1
    mesh = HighwayMesh(tiles, texture, profile, concrete, footprint)
    if ident <= 80:
      axis = (ident - 73) % 2
      if ident in (75, 76) and underpasses:
        road = records[29 + (1 - axis)][0]
        enlarged = road.resize((road.width * DETAIL, road.height * DETAIL), Image.Resampling.NEAREST)
        mesh.pixels[64 * DETAIL:] = np.array(enlarged)
      elif ident in (77, 78):
        rail_id = 44 + (1 - axis)
        rail_path = source.parent / "rails" / ("straight-b.png" if axis == 0 else "straight-a.png")
        rail = records[rail_id][0] if rail_id in records else Image.open(rail_path).convert("RGBA")
        enlarged = rail.resize((rail.width * DETAIL, rail.height * DETAIL), Image.Resampling.NEAREST)
        mesh.pixels[64 * DETAIL:] = np.array(enlarged)
      elif ident in (79, 80):
        for s in (.04, .96):
          u, v = (s, .5) if axis == 0 else (.5, s)
          mesh.Box(u, v, .015, .015, 0, 29, (101, 79, 50))
          mesh.Box(u, v, .018 if axis == 0 else .19, .19 if axis == 0 else .018, 27, 29, (90, 76, 55))
        for offset in (-.13, 0, .13):
          points = [(u, v, 28 - 3 * math.sin(t * math.pi)) for t in np.linspace(0, 1, 65)
              for u, v in (((t, .5 + offset) if axis == 0 else (.5 + offset, t)),)]
          for p, q in zip(points, points[1:]):
            mesh.Quad([p, q, (q[0], q[1], q[2] + .5), (p[0], p[1], p[2] + .5)], (46, 47, 43))
      gaps = [(side, .14, .86) for side in (-1, 1) if merge & (1 if side < 0 else 2)]
      for side, _, _ in gaps:
        # Two rounded branches provide actual turning space in both directions.
        # Their outer end matches the ramp's half-width at deck height.
        for end in (0, 1):
          path = []
          for angle in np.linspace(0, math.pi / 2, 97):
            across = (1 - .5 * math.cos(angle)) if side > 0 else .5 * math.cos(angle)
            along = .5 * math.sin(angle) if end == 0 else 1 - .5 * math.sin(angle)
            path.append((across, along, DECK_HEIGHT) if axis == 0 else (along, across, DECK_HEIGHT))
          mesh.Ribbon(path, half_width=RAMP_TOP_HALF_WIDTH, supports=False, marking=False,
              rails=False, surface="asphalt-flat")
      # Paint the uninterrupted through lanes over the merged turning apron.
      # Ribbon's lateral normal is +u on axis 0 but -v on axis 1. Merge masks
      # use increasing map coordinates, so convert before opening the rail.
      rail_gaps = [(-side if axis else side, start, end) for side, start, end in gaps]
      mesh.Ribbon(Straight(.5, 1, axis), support_positions=(.06, .94) if ident >= 75 or merge else None,
          side_gaps=rail_gaps)
      AddMergeGuardrails(mesh, axis, gaps)
    elif ident <= 96:
      rotation = (0, 3, 2, 1)[ident - 93]
      path, normals = Ramp(rotation, ident in (94, 96))
      widths = RampWidths(path, normals)
      mesh.Ribbon(path, half_width=.375, supports=False, normals=normals, widths=widths, embankment=True,
          fit_dashes=True, edge_marking=False)
    elif ident <= 100:
      slope = ident - 96
      for center in (.5, 1.5):
        mesh.Ribbon(Straight(center, 2, 1 if slope % 2 else 0, slope=slope))
    elif ident <= 104:
      for radius in (.5, 1.5):
        mesh.Ribbon(Bend(radius, rotation=ident - 101))
    elif ident == 105:
      AddInterchange(mesh)
    else:
      axis = ident - 106
      for center in (.5, 1.5):
        # Each underlying road/rail crosses at a map cell's centre. Keep the
        # piers between those crossings, clear of the full 75% road width.
        mesh.Ribbon(Straight(center, 2, axis), support_positions=(.06, 1, 1.94))
      # A restrained steel through-truss with a clear two-carriageway opening.
      for across in (.10, 1.90):
        for i in range(8):
          a, b = i / 4, (i + 1) / 4
          def p(s, z):
            return (across, s, DECK_HEIGHT + z) if axis == 0 else (s, across, DECK_HEIGHT + z)
          mesh.Quad([p(a, 3), p(b, 3), p(b, 6), p(a, 6)], (102, 68, 54))
          mesh.Quad([p(a, 29), p(b, 29), p(b, 32), p(a, 32)], (151, 94, 69))
          mesh.Quad([p(a, 3), p(b, 29), p(b, 32), p(a, 6)], (138, 83, 60))
          mesh.Quad([p(a, 29), p(b, 3), p(b, 6), p(a, 32)], (122, 72, 54))
    records[output_id] = mesh.Finish()

  if identifiers is NAMES:
    BuildHighways(records, source, footprint, MERGE_NAMES)


def WriteHighwayPreview(records, output):
  font_path = "/System/Library/Fonts/Supplemental/Arial.ttf"
  try:
    title = ImageFont.truetype(font_path, 22)
    label = ImageFont.truetype(font_path, 16)
  except OSError:
    title = ImageFont.load_default(size=22)
    label = ImageFont.load_default(size=16)
  board = Image.new("RGB", (1168, 60 + 6 * 252), (34, 39, 44))
  draw = ImageDraw.Draw(board)
  draw.text((18, 14), "NATIVE HIGHWAYS — shared road material, divided lanes, ramps and junctions",
      font=title, fill="white")
  for n, (ident, name) in enumerate(NAMES.items()):
    x, y = 16 + n % 4 * 288, 60 + n // 4 * 252
    draw.rectangle((x, y, x + 277, y + 241), outline=(80, 93, 104))
    draw.text((x + 8, y + 8), f"{ident}: {name.replace('highway-', '').replace('-', ' ')}",
        font=label, fill="white")
    image = records[ident][0]
    board.paste(image, (x + (278 - image.width) // 2, y + 240 - image.height), image)
  board.save(output / "HIGHWAYS.png")
