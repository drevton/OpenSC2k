#!/usr/bin/env python3
"""Prepare 128px railway artwork; all rasterization and material work is offline.

The ImageGen ballast master supplies stone detail. Exact rail gauge, sleeper
spacing, C2 diagonal approaches and nearest-neighbour tile ownership are code
controlled, sharing the road projection and finished sprite resource format.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from build_landscape_assets import (NATIVE_DIAGONAL_RAIL_FIRST, NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST,
    NATIVE_DIAGONAL_RAIL_SHARED_FIRST, fetch, transparent)
from pack_landscape_assets import SPRITE_CATALOG
from build_road_assets import (BuildSurface, Canonical, DiagonalCoordinates, DiagonalPath, InstallInfrastructureSources,
    ReadTerrainFootprints, Unproject, BuildSharedDiagonalApproaches, SplitDiagonalApproaches,
    SaveDiagonalApproachSources, InfrastructurePreviewRecords)
from build_highway_assets import BuildHighways, HighwayMesh, MERGE_NAMES, NAMES as HIGHWAYS

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/landscape/rails"
ROADS = ROOT / "assets/landscape/roads"
DETAIL = 4
GAUGE_HALF = .145
BALLAST_HALF = .315
LAYOUTS = ((44, "straight-a", "straight", 0, 0), (45, "straight-b", "straight", 1, 0),
    (46, "slope-1", "straight", 1, 1), (47, "slope-2", "straight", 0, 2),
    (48, "slope-3", "straight", 1, 3), (49, "slope-4", "straight", 0, 4)) + \
    tuple((50 + i, f"curve-{i + 1}", "curve", i, 0) for i in range(4)) + \
    tuple((54 + i, f"switch-{i + 1}", "switch", i, 0) for i in range(4)) + \
    ((58, "four-way-junction", "cross", 0, 0),)
DIAGONALS = tuple((NATIVE_DIAGONAL_RAIL_FIRST + r * 3 + m - 1,
    f"diagonal-{r + 1}-{m}", f"diagonal-{m}", r, 0) for r in range(4) for m in range(1, 4))
TRANSITIONS = tuple((NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST + r * 4 + (m - 1) * 2 + p,
    f"diagonal-{name}-{r + 1}-{m}", f"diagonal-{name}-{m}", r, 0)
    for r in range(4) for m in range(1, 3) for p, name in enumerate(("transition", "approach")))
SHARED = {NATIVE_DIAGONAL_RAIL_SHARED_FIRST + a * 4 + f * 2 + s:
    f"diagonal-shared-{a + 1}-{f + 1}-{s + 1}" for a in range(2) for f in range(2) for s in range(2)}
CROSSINGS = {69: "road-crossing-a", 70: "road-crossing-b", 71: "power-crossing-a", 72: "power-crossing-b"}
BRIDGES = {90: "girder-bridge-a", 91: "girder-bridge-b"}
HIGHWAY_IDS = (77, 78) + tuple(i for i in MERGE_NAMES if "highway-rail" in MERGE_NAMES[i])


def SampleBallast(u, v, material):
  # World-plane coordinates never clamp or stretch a texture row along a bend.
  sx, sy = (u % 1) * material.shape[1], (v % 1) * material.shape[0]
  ix, iy = sx.astype(int), sy.astype(int)
  fx, fy = (sx - ix)[..., None], (sy - iy)[..., None]
  nx, ny = (ix + 1) % material.shape[1], (iy + 1) % material.shape[0]
  return (material[iy, ix] * (1 - fx) + material[iy, nx] * fx) * (1 - fy) + \
      (material[ny, ix] * (1 - fx) + material[ny, nx] * fx) * fy


def TrackCoordinates(u, v, kind, rotation):
  u, v = Canonical(u, v, "straight" if kind == "straight" else "curve", rotation)
  if kind.startswith("diagonal-"):
    lateral, travel, _ = DiagonalCoordinates(u, v, kind)
    mask = int(kind[-1])
    extended = kind.startswith(("diagonal-transition-", "diagonal-approach-"))
    ts = np.linspace(0, 1, 513)
    p, d, _ = DiagonalPath(ts, mask, extended)
    lengths = np.r_[0, np.cumsum(np.hypot(*np.diff(p, axis=0).T))]
    along = np.interp(travel, ts, lengths)
    along += np.minimum(travel, 0) * np.hypot(*d[0]) + np.maximum(travel - 1, 0) * np.hypot(*d[-1])
    # Ordinary straight halves meet at a sleeper gap, not at half a sleeper.
    spacing = lengths[-1] / round(lengths[-1] / .10)
    if extended:
      if mask == 1:
        along = np.where(travel > 1, lengths[-1] + (travel - 1) * np.hypot(*d[-1]), along)
        phase = np.where(travel > 1, (along - lengths[-1]) / .10, along / spacing)
      else:
        phase = np.where(travel < 0, along / .10, along / spacing)
    else:
      phase = along / spacing
    return lateral, phase
  if kind == "curve":
    return np.hypot(1 - u, v) - .5, np.arctan2(v, 1 - u) / (math.pi / 2) * 8
  return u - .5, v * 10


def PaintTrack(material, footprint, kind, rotation=0, slope=0, road_axis=None):
  height = 80 if slope else 64
  y, x = np.mgrid[:height * DETAIL, :128 * DETAIL]
  u, v = Unproject((x + .5) / DETAIL, (y + .5) / DETAIL, slope)
  if kind == "switch":
    cu, cv = Canonical(u, v, "curve", rotation)
    paths = [TrackCoordinates(cu, cv, "straight", 1), TrackCoordinates(cu, cv, "curve", 0),
        TrackCoordinates(cu, cv, "curve", 3)]
  elif kind == "cross":
    # The four-way tile permits every exit. Retain both through tracks and
    # connect each adjacent pair with a turnout sharing the ordinary gauge.
    paths = [TrackCoordinates(u, v, "straight", a) for a in range(2)] + \
        [TrackCoordinates(u, v, "curve", a) for a in range(4)]
  else:
    paths = [TrackCoordinates(u, v, kind, rotation)]
  lateral = np.stack([p[0] for p in paths])
  phase = np.stack([p[1] for p in paths])
  owner = np.argmin(np.abs(lateral), axis=0)[None]
  across = np.take_along_axis(lateral, owner, axis=0)[0]
  along = np.take_along_axis(phase, owner, axis=0)[0]
  distance = np.abs(across)
  stone = SampleBallast(u, v, material)
  grain = (stone.mean(axis=-1) - material.mean()) / 255
  edge = BALLAST_HALF + grain * .022 - distance
  alpha = np.clip(edge / .018, 0, 1)
  # Lift the crown gently, retaining the stone's individual highlights.
  rgb = stone * (.82 + .18 * np.clip(1 - (distance / BALLAST_HALF) ** 2, 0, 1))[..., None]
  ties = (np.abs((along % 1) - .5) < .18) & (distance < .247)
  tie_index = np.floor(along)
  wood_grain = 3 * np.sin(across * 850 + tie_index * 7) + 2 * np.cos(across * 370 - tie_index * 19)
  variation = 5 * np.sin(tie_index * 17.1)
  wood = np.stack((96 + variation + wood_grain, 76 + variation + wood_grain, 54 + variation + wood_grain), -1)
  bevel = (np.abs((along % 1) - .5) > .13) | (distance > .228)
  wood += np.where(bevel, -12, 4)[..., None]
  rgb = np.where(ties[..., None], wood, rgb)
  plate = ties & (np.abs(distance - GAUGE_HALF) < .041)
  rgb = np.where(plate[..., None], np.array((63, 59, 52)), rgb)
  rail_distance = np.min(np.abs(np.abs(lateral) - GAUGE_HALF), axis=0)
  foot = rail_distance < .025
  rail = rail_distance < .0105
  rgb = np.where(foot[..., None], np.array((91, 79, 65)) + grain[..., None] * 20, rgb)
  # Dark web below the bright, narrow running face. No painted white lines.
  steel = np.stack((190 + grain * 25, 190 + grain * 25, 181 + grain * 20), -1)
  steel -= (rail_distance / .0105 * 32)[..., None]
  rgb = np.where(rail[..., None], steel, rgb)
  if kind == "cross":
    du, dv = np.abs(u - .5), np.abs(v - .5)
    frog = (np.abs(du - GAUGE_HALF) < .017) & (np.abs(dv - GAUGE_HALF) < .017)
    rgb = np.where(frog[..., None], np.array((55, 53, 47)), rgb)
    guard = ((np.abs(du - GAUGE_HALF + .045) < .006) & (np.abs(dv - GAUGE_HALF) < .075)) | \
        ((np.abs(dv - GAUGE_HALF + .045) < .006) & (np.abs(du - GAUGE_HALF) < .075))
    rgb = np.where(guard[..., None], np.array((150, 148, 137)), rgb)
  bolts = ties & (np.abs(distance - (GAUGE_HALF + .033)) < .007) & \
      (np.abs((along % 1) - .5) < .055)
  rgb = np.where(bolts[..., None], np.array((147, 143, 129)), rgb)
  if road_axis is not None:
    road_distance = np.abs((u if road_axis == 0 else v) - .5)
    road_area = road_distance < .375
    # Flush timber infill makes the track legible through the road. The deck
    # covers lane paint, but keeps the ordinary gauge and ballast at its ends.
    deck = distance < .265
    plank = np.floor((across + .265) / .066)
    grain = 2 * np.sin(along * 47 + plank * 13) + 1.5 * np.cos(along * 119 - plank * 7)
    timber = np.stack((109 + grain, 96 + grain, 75 + grain), -1)
    timber += (4 * np.sin(plank * 17))[..., None]
    joint = ((across + .265) % .066 < .007) | (distance > .255)
    timber -= np.where(joint, 24, 0)[..., None]
    rgb = np.where((road_area & deck)[..., None], timber, rgb)
    flangeway = rail_distance < .029
    rgb = np.where((road_area & flangeway)[..., None], np.array((47, 44, 38)), rgb)
    running_face = rail_distance < .013
    rgb = np.where((road_area & running_face)[..., None], steel, rgb)
    alpha = np.where(road_area, (deck | flangeway).astype(float), alpha)
  pixels = np.zeros((*alpha.shape, 4), dtype=np.uint8)
  pixels[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
  pixels[..., 3] = np.rint(alpha * 255).astype(np.uint8)
  image = Image.fromarray(pixels).resize((128, height), Image.Resampling.BOX)
  pixels = np.array(image)
  pixels[~footprint] = 0
  pixels[pixels[..., 3] == 0] = 0
  return Image.fromarray(pixels)


def BuildCrossings(records, material, footprint):
  road_manifest = {e["id"]: e for e in json.loads((ROADS / "manifest.json").read_text())}
  texture = np.asarray(Image.open(ROADS / "asphalt-material.png").convert("RGB"), dtype=float)
  concrete = np.asarray(Image.open(ROADS / "tunnel-material.png").convert("RGB"), dtype=float)
  profile = json.loads((ROADS / "lighting-profile.json").read_text())["samples"]
  for ident, axis in ((69, 0), (70, 1)):
    road = Image.open(ROADS / road_manifest[29 + axis]["file"]).convert("RGBA")
    unmarked = np.asarray(BuildSurface("straight", axis, 0, texture, profile, footprint, markings=False)[0])
    # Replace opaque pavement with unpainted asphalt, retaining the finished
    # road's exact shoulder alpha and terrain verge at the shared tile edges.
    pixels = np.array(road)
    pavement = unmarked[..., 3] == 255
    pixels[pavement, :3] = unmarked[pavement, :3]
    road = Image.fromarray(pixels)
    rail = PaintTrack(material, footprint, "straight", 1 - axis, road_axis=axis)
    road.alpha_composite(rail)
    mesh = HighwayMesh(1, texture, profile, concrete, footprint)
    mesh.pixels[64 * DETAIL:] = np.array(road.resize((512, 256), Image.Resampling.NEAREST))
    for u, v in ((.09, .28), (.91, .72)):
      if axis: u, v = v, u
      mesh.Box(u, v, .012, .012, 0, 24, (105, 89, 67))
      # White crossbucks face the approaching road; each is two actual boards.
      def point(t, z):
        return (u, v + t, z) if axis else (u + t, v, z)
      for sign in (-1, 1):
        mesh.Quad([point(-.09, 18 - sign * 3), point(.09, 18 + sign * 3),
            point(.09, 20 + sign * 3), point(-.09, 20 - sign * 3)], (225, 224, 204))
      mesh.Box(u, v, .017, .017, 14, 16, (130, 47, 33))
    records[ident] = mesh.Finish()
  for ident, axis in ((71, 0), (72, 1)):
    mesh = HighwayMesh(1, texture, profile, concrete, footprint)
    mesh.pixels[64 * DETAIL:] = np.array(records[44 + axis][0].resize((512, 256), Image.Resampling.NEAREST))
    for s in (.04, .96):
      u, v = (s, .5) if axis == 0 else (.5, s)
      mesh.Box(u, v, .015, .015, 0, 29, (101, 79, 50))
      mesh.Box(u, v, .018 if axis == 0 else .19, .19 if axis == 0 else .018, 27, 29, (90, 76, 55))
    for offset in (-.13, 0, .13):
      points = [(u, v, 28 - 3 * math.sin(t * math.pi)) for t in np.linspace(0, 1, 65)
          for u, v in (((t, .5 + offset) if axis == 0 else (.5 + offset, t)),)]
      for p, q in zip(points, points[1:]):
        mesh.Quad([p, q, (q[0], q[1], q[2] + .5), (p[0], p[1], p[2] + .5)], (46, 47, 43))
    records[ident] = mesh.Finish()


def BuildBridges(records, upstream):
  folder = "graphics/bridges/256/"
  mask_sheet = Image.open(fetch(upstream, folder + "bridge_deckmask.png")).convert("RGB")
  terrain = transparent(Image.open(fetch(upstream, "graphics/terrain/256/general_bridgetiles_32bpp.png")))
  sheet = transparent(Image.open(fetch(upstream, folder + "bridge_girder_32bpp.png")))
  for ident, column in ((90, 0), (91, 130)):
    body = sheet.crop(((column + 1) * 4, 4, (column + 65) * 4, 512))
    ground = terrain.crop((4, 4, 260, 132))
    ground.alpha_composite(records[45][0].resize((256, 128), Image.Resampling.NEAREST))
    deck = Image.new("RGBA", body.size)
    deck.alpha_composite(ground, (0, 324))
    mask = np.asarray(mask_sheet.crop((4, 4, 260, 512)))
    pixels = np.array(deck)
    pixels[np.all(mask == (0, 0, 255), axis=-1) | np.all(mask == (255, 255, 255), axis=-1)] = 0
    body.alpha_composite(Image.fromarray(pixels))
    canvas = Image.new("RGBA", (512, 768))
    canvas.alpha_composite(body, (128, 288))
    for row in (259, 129):
      front = sheet.crop(((column + 1) * 4, (row + 1) * 4, (column + 65) * 4, (row + 128) * 4))
      canvas.alpha_composite(front, (128, 288))
    canvas = canvas.transpose(Image.Transpose.FLIP_LEFT_RIGHT).resize((256, 384), Image.Resampling.BOX)
    records[ident] = canvas, 384, 4, -64


def WritePreview(records, output):
  font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
  font = ImageFont.truetype(str(font_path), 20) if font_path.exists() else ImageFont.load_default()
  small = ImageFont.truetype(str(font_path), 15) if font_path.exists() else ImageFont.load_default()
  canvas = Image.new("RGB", (1080, 1220), (34, 40, 43))
  draw = ImageDraw.Draw(canvas)
  draw.text((24, 16), "Railways · 128px artwork", fill=(239, 238, 227), font=font)
  draw.text((24, 47), "Steel rails, timber sleepers and painted ballast · enlarged with nearest-neighbour sampling",
      fill=(182, 195, 199), font=small)
  examples = ((44, "Straight"), (45, "Straight, rotated"), (NATIVE_DIAGONAL_RAIL_FIRST + 2, "Diagonal"),
      (50, "Curve"), (54, "Switch"), (58, "Four-way junction"),
      (46, "Incline"), (69, "Road crossing"), (71, "Power crossing"))
  for index, (ident, label) in enumerate(examples):
    left, top = 24 + index % 3 * 356, 88 + index // 3 * 194
    draw.rectangle((left, top, left + 330, top + 170), outline=(68, 78, 82))
    draw.text((left + 10, top + 8), label, fill=(220, 226, 227), font=small)
    image = records[ident][0]
    if ident == NATIVE_DIAGONAL_RAIL_FIRST + 2:
      image = Image.new("RGBA", (192, 96))
      image.alpha_composite(records[NATIVE_DIAGONAL_RAIL_FIRST + 5][0], (64, 0))
      image.alpha_composite(records[NATIVE_DIAGONAL_RAIL_FIRST + 11][0], (0, 32))
    visible = image.crop(image.getbbox())
    scale = min(2, 300 / visible.width, 130 / visible.height)
    enlarged = visible.resize((round(visible.width * scale), round(visible.height * scale)), Image.Resampling.NEAREST)
    canvas.paste(enlarged, (left + (330 - enlarged.width) // 2, top + 35 + (130 - enlarged.height) // 2), enlarged)
  draw.text((24, 694), "Connected tracks · rounded bends, switches and crossings", fill=(239, 238, 227), font=font)
  placements = {(0, 0): 51, (6, 0): 52, (6, 6): 53, (0, 6): 50,
      (3, 0): 56, (3, 6): 54, (0, 3): 55, (6, 3): 57, (3, 3): 58}
  for u in range(1, 6):
    for v in (0, 3, 6): placements.setdefault((u, v), 45)
  for v in range(1, 6):
    for u in (0, 3, 6): placements.setdefault((u, v), 44)
  for (u, v), ident in sorted(placements.items(), key=lambda item: (sum(item[0]), item[0][0])):
    sprite = records[ident][0]
    canvas.paste(sprite, (476 + (u - v) * 64, 744 + (u + v) * 32), sprite)
  canvas.save(output / "rail-preview.png")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--output", type=Path, default=SOURCE)
  parser.add_argument("--terrain", type=Path, default=ROADS.parent / "opengfx2/detail128/terrain-masks.png",
      help="shared 128px master boundary sheet")
  parser.add_argument("--upstream", type=Path, default=Path("/tmp/sc2k-opengfx2-source"))
  parser.add_argument("--catalog", type=Path, default=SPRITE_CATALOG)
  parser.add_argument("--diagonal-ends-only", action="store_true",
      help="rebake rounded ends and reusable approach halves")
  args = parser.parse_args()
  footprints = ReadTerrainFootprints(args.terrain)
  material = np.asarray(Image.open(SOURCE / "ballast-material-master.png").convert("RGB").resize(
      (128, 128), Image.Resampling.LANCZOS), dtype=float)
  records = {}
  if args.diagonal_ends_only:
    for ident, _, kind, rotation, slope in TRANSITIONS:
      image = PaintTrack(material, footprints[slope], kind, rotation, slope)
      records[ident] = image, image.height, 4, 0
    WritePreview(InfrastructurePreviewRecords(args.output, records, NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST,
        NATIVE_DIAGONAL_RAIL_SHARED_FIRST, 44), args.output)
    SplitDiagonalApproaches(records, NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST, NATIVE_DIAGONAL_RAIL_SHARED_FIRST)
    SaveDiagonalApproachSources(args.output, args.catalog, records, TRANSITIONS, NATIVE_DIAGONAL_RAIL_SHARED_FIRST)
    print(f"Built eight rounded rail ends and eight approach halves in {args.output}")
    return
  for ident, _, kind, rotation, slope in LAYOUTS + DIAGONALS + TRANSITIONS:
    image = PaintTrack(material, footprints[slope], kind, rotation, slope)
    records[ident] = image, image.height, 4, 0
  for ident in range(59, 63):
    records[ident] = records[44 + (ident & 1)]
  BuildSharedDiagonalApproaches(records, NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST, NATIVE_DIAGONAL_RAIL_SHARED_FIRST)
  BuildCrossings(records, material, footprints[0])
  BuildBridges(records, args.upstream)
  BuildHighways(records, ROADS, footprints[0], HIGHWAY_IDS)
  names = {i: n for i, n, *_ in LAYOUTS + DIAGONALS + TRANSITIONS} | SHARED | CROSSINGS | BRIDGES | \
      {i: f"transition-{i - 58}" for i in range(59, 63)} | {i: (HIGHWAYS | MERGE_NAMES)[i] for i in HIGHWAY_IDS}
  args.output.mkdir(parents=True, exist_ok=True)
  WritePreview(records, args.output)
  SplitDiagonalApproaches(records, NATIVE_DIAGONAL_RAIL_TRANSITION_FIRST, NATIVE_DIAGONAL_RAIL_SHARED_FIRST)
  manifest = []
  for ident, (image, baseline, scale, offset) in sorted(records.items()):
    if 44 <= ident <= 62 or ident >= NATIVE_DIAGONAL_RAIL_FIRST:
      shape = ident - 45 if 46 <= ident <= 49 else 0
      if np.any(np.asarray(image)[..., 3][~footprints[shape]]):
        raise ValueError(f"Rail sprite {ident} crosses its owning tile footprint")
    image.save(args.output / f"{names[ident]}.png")
    manifest.append(dict(id=ident, file=f"{names[ident]}.png", width=image.width, height=image.height,
        baseline=baseline, scale_divisor=scale, offset_x=offset))
  (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
  for path in args.output.glob("diagonal-shared-*.png"):
    path.unlink()
  if args.catalog:
    InstallInfrastructureSources(args.catalog, records,
        {ident: args.output / f"{name}.png" for ident, name in names.items()},
        range(NATIVE_DIAGONAL_RAIL_SHARED_FIRST, NATIVE_DIAGONAL_RAIL_SHARED_FIRST + 8))
  print(f"Built {len(records)} native railway sprites in {args.output}")


if __name__ == "__main__":
  main()
