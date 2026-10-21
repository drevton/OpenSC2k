#!/usr/bin/env python3
"""Compose OpenGFX2 bridge structures with native 128px road and rail decks, offline.

Individual PNGs are checked in. The normal build only packs the local catalogue.
OpenTTD's six piece layouts are recorded in layouts.json; no runtime searches or
upstream dependencies are needed to select a bridge's prepared artwork.
"""
import argparse
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from build_landscape_assets import NATIVE_BAKED_UNDERWORLD_FIRST, fetch, transparent
from pack_landscape_assets import ExportSpriteSources, SPRITE_CATALOG

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/landscape/bridges"
FIRST = NATIVE_BAKED_UNDERWORLD_FIRST + 20 + 52 + 15 * 20 + 2 * 20 + 20 + 1 + 2
RAIL_FIRST = 4331  # Follows the baked power-line artwork.
PIECES = 6
HEAD_FIRST = RAIL_FIRST + 7 * PIECES * 2
HEAD_COUNT = 32  # Road/rail, wooden/general structure, rising/level, four directions.
BASELINE = 242  # Road starts at 162: bottom 226 is exactly 16px above the ground.
GEOMETRY = (0, 1, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6)
STRUCTURE_FIRST = 192


def BuildBridgeHeads(source, output, quantizer, source_palette, original_luma, foreground_only=False):
  from build_road_assets import BuildSurface, ReadTerrainFootprints, Unproject
  roads = ROOT / "assets/landscape/roads"
  texture = np.asarray(Image.open(roads / "asphalt-material.png").convert("RGB"), dtype=float)
  profile = json.loads((roads / "lighting-profile.json").read_text())["samples"]
  footprints = ReadTerrainFootprints()
  records, files, entries = {}, {}, []
  for transport in range(2):
    for form, name in enumerate(("wood", "norecolour" if transport else "general")):
      prefix = "graphics/bridges/256/bridgeramps_" + name
      sheet = transparent(Image.open(fetch(source, prefix + "_32bpp.png")), True)
      mask = Image.open(fetch(source, prefix + "_palmask.png")).convert("RGB") if name == "general" else None
      for variant in range(8):
        level, shape = variant >= 4, variant % 4 + 1
        column = ((2, 0, 3, 1) if level else (6, 4, 7, 5))[shape - 1]
        box = ((column * 65 + 1) * 4, 4, (column * 65 + 65) * 4, 260)
        structure = np.array(sheet.crop(box))
        painted = np.zeros(structure.shape[:2], dtype=bool)
        if mask:
          colors = np.asarray(mask.crop(box), dtype=float)
          painted = (colors[..., 0] > colors[..., 1] * 1.3) & (colors[..., 1] > colors[..., 2] * 1.1)
        material = np.zeros_like(structure)
        material[..., :3] = painted[..., None] * 255
        material[..., 3] = structure[..., 3]
        axis = shape & 1
        y, x = np.mgrid[:256, :256]
        deck_top = 48 if level and column & 1 else 32
        surface_shape = 0 if level else shape
        u, v = Unproject((x + .5) / 2, (y + .5) / 2 - deck_top, surface_shape)
        front = (v if axis else u) > .5
        body, paint = Image.new("RGBA", (256, 256)), Image.new("RGBA", (256, 256))
        if transport:
          filename = f"straight-{'b' if axis else 'a'}.png" if level else f"slope-{shape}.png"
          deck = Image.open(ROOT / "assets/landscape/rails" / filename).convert("RGBA")
        else:
          deck = BuildSurface("straight", axis, surface_shape, texture, profile, footprints[surface_shape])[0]
        # Translate each intact side beam outwards instead of stretching its
        # texture. OpenGFX2's deck is narrower than the native pavement/ballast.
        widening = .015 if transport else .075
        for side in (-1, 1):
          visible = front if side > 0 else ~front
          offset = (round(side * (-1 if axis else 1) * 128 * widening),
              round(side * 64 * widening) + (2 * (48 - deck_top) if level else 0))
          for target, pixels in ((body, structure), (paint, material)):
            if foreground_only and side < 0: continue
            layer = pixels.copy()
            layer[~visible] = 0
            target.alpha_composite(Image.fromarray(layer), offset)
          if side < 0 and not foreground_only:
            surface = deck.resize((256, deck.height * 2), Image.Resampling.NEAREST)
            position = (0, 96 if level else 64)
            body.alpha_composite(surface, position)
            clear = Image.new("RGBA", surface.size)
            clear.putalpha(surface.getchannel("A"))
            paint.alpha_composite(clear, position)
        body = body.resize((128, 128), Image.Resampling.BOX)
        recolor = np.asarray(paint.resize(body.size, Image.Resampling.BOX))[..., 0] >= 128
        pixels = np.array(body)
        indices = np.array(body.convert("RGB").quantize(palette=quantizer, dither=Image.Dither.NONE))
        indices[indices >= STRUCTURE_FIRST] = 0
        luma = pixels[..., :3].astype(float) @ np.array([.2126, .7152, .0722])
        shade = np.interp(luma, original_luma, np.linspace(0, 63, 10))
        indices[recolor] = STRUCTURE_FIRST + np.rint(shade[recolor]).astype(np.uint8)
        pixels[..., :3] = source_palette[indices]
        image = Image.fromarray(pixels)
        ident = HEAD_FIRST + (transport * 2 + form) * 8 + variant
        filename = f"head-{'rail' if transport else 'road'}-{'general' if form else 'wood'}-" + \
            ("level-" if level else "") + f"{shape}.png"
        if foreground_only: filename = "front-" + filename
        image.save(output / filename)
        records[ident] = (image, 112, 4, 0)
        files[ident] = output / filename
        entries.append(dict(id=ident, file=filename, baseline=112, scale_divisor=4, offset_x=0,
            palette="palette-source.png"))
  return records, files, entries


def BridgePalettes(source, layouts, static_colors):
  upstream = np.array(Image.open(fetch(source, "graphics/palette_openttd.png")).getpalette(),
      dtype=np.uint8).reshape(256, 3)
  text = fetch(source, "baseset/nml/base/base-0775-recolor.pnml", False).read_text()
  import re
  remaps = {}
  for name, block in re.findall(r"// (PALETTE_TO_STRUCT_\w+)\s*recolour_sprite\s*\{(.*?)\}", text, re.S):
    remap = list(range(256))
    for start, end, target, target_end in re.findall(
        r"(0x[0-9A-Fa-f]+)(?:\.\.(0x[0-9A-Fa-f]+))?\s*:\s*(0x[0-9A-Fa-f]+)(?:\.\.(0x[0-9A-Fa-f]+))?;", block):
      first, last, value = int(start, 16), int(end or start, 16), int(target, 16)
      for index in range(first, last + 1): remap[index] = value + (index - first if target_end else 0)
    remaps[name] = remap
  shade = np.linspace(0, 9, 64)
  palettes = []
  for layout in layouts:
    names = {layer["palette"] for piece in layout["pieces"] for axis in piece for layer in axis if layer}
    names.discard("PAL_NONE")
    if len(names) > 1: raise ValueError("Bridge has conflicting structure remaps")
    remap = remaps[next(iter(names))] if names else list(range(256))
    ramp = upstream[np.array(remap)[0x46:0x50]]
    colors = np.array([np.interp(shade, np.arange(10), ramp[:, channel]) for channel in range(3)]).T
    palette = np.vstack((static_colors, np.rint(colors).astype(np.uint8)))
    palettes.append(palette)
  return palettes, upstream[0x46:0x50]


def BridgePiece(north, south):
  if north == 1: return 0
  if south == 1: return 1
  if north < south: return 3 if north & 1 else 2
  if north > south: return 2 if south & 1 else 3
  return 5 if north & 1 else 4


def BuildBridgeSprites(source, output=OUTPUT):
  layouts = json.loads((OUTPUT / "layouts.json").read_text())["styles"]
  sheets, masks = {}, {}
  for style in ("wood", "steelslow", "steelfast", "girder", "suspension", "tubular"):
    prefix = f"graphics/bridges/256/bridge_{style}"
    sheets[style] = transparent(Image.open(fetch(source, prefix + "_32bpp.png")))
    if style in ("girder", "suspension", "tubular"):
      masks[style] = Image.open(fetch(source, prefix + "_palmask.png")).convert("RGB")
  decks = {transport: [Image.open(ROOT / "assets/landscape" / folder / name).convert("RGBA")
      for name in ("straight-a.png", "straight-b.png")] for transport, folder in (("road", "roads"), ("rail", "rails"))}

  def Layer(spec):
    if spec is None:
      empty = Image.new("RGBA", (256, 508))
      return empty, empty.copy()
    col, row = spec["column"], spec["row"]
    box = ((col + 1) * 4, (row + 1) * 4, (col + 65) * 4, (row + 128) * 4)
    image = sheets[spec["sheet"]].crop(box)
    pixels = np.array(image)
    painted = np.zeros(pixels.shape[:2], dtype=bool)
    if spec["sheet"] in masks:
      mask = np.array(masks[spec["sheet"]].crop(box)).astype(float)
      painted = (mask[..., 0] > mask[..., 1] * 1.3) & (mask[..., 1] > mask[..., 2] * 1.1)
    material = np.zeros_like(pixels)
    material[..., :3] = painted[..., None] * 255
    material[..., 3] = pixels[..., 3]
    return image, Image.fromarray(material)

  output.mkdir(parents=True, exist_ok=True)
  bodies, materials = [], []
  for transport in ("road", "rail"):
    for style in (0, 1, 2, 3, 6, 9, 10):
      for axes in layouts[style]["rail_pieces" if transport == "rail" else "pieces"]:
        for axis, layers in enumerate(axes):
          body, material = Layer(layers[0])
          deck = decks[transport][axis].resize((256, 128), Image.Resampling.NEAREST)
          body.alpha_composite(deck, (0, 324))
          deck_material = Image.new("RGBA", deck.size)
          deck_material.putalpha(deck.getchannel("A"))
          material.alpha_composite(deck_material, (0, 324))
          for layer in (layers[2], layers[1]):
            pixels, paint = Layer(layer)
            body.alpha_composite(pixels); material.alpha_composite(paint)
          bodies.append(body.resize((128, 254), Image.Resampling.BOX))
          materials.append(np.array(material.resize((128, 254), Image.Resampling.BOX))[..., 0] >= 128)
  samples = np.concatenate([np.array(body)[..., :3][(np.array(body)[..., 3] > 0) & ~paint]
      for body, paint in zip(bodies, materials)])
  swatch = Image.fromarray(samples[::max(1, len(samples) // 262144)].reshape(-1, 1, 3))
  static = swatch.quantize(colors=STRUCTURE_FIRST, dither=Image.Dither.NONE)
  static_colors = np.array(static.getpalette()[:STRUCTURE_FIRST * 3], dtype=np.uint8).reshape(-1, 3)
  palettes, original_ramp = BridgePalettes(source, layouts, static_colors)
  original_luma = original_ramp.astype(float) @ np.array([.2126, .7152, .0722])
  source_palette = np.vstack((static_colors,
      np.array([np.interp(np.linspace(0, 9, 64), np.arange(10), original_ramp[:, channel])
          for channel in range(3)]).T.astype(np.uint8)))
  # RGB source colors identify indices during offline packing, so keep them unique.
  used = set()
  for color in source_palette:
    while tuple(color) in used:
      value = (int(color[0]) << 16 | int(color[1]) << 8 | int(color[2])) + 1
      color[:] = ((value >> 16) & 255, (value >> 8) & 255, value & 255)
    used.add(tuple(color))
  quantizer = Image.new("P", (1, 1))
  quantizer.putpalette(np.vstack((static_colors, np.tile(static_colors[0], (64, 1)))).reshape(-1))
  records, files, entries = {}, {}, []
  for geometry, (body, paint) in enumerate(zip(bodies, materials)):
    pixels = np.array(body)
    indices = np.array(body.convert("RGB").quantize(palette=quantizer, dither=Image.Dither.NONE))
    indices[indices >= STRUCTURE_FIRST] = 0
    luma = pixels[..., :3].astype(float) @ np.array([.2126, .7152, .0722])
    shade = np.interp(luma, original_luma, np.linspace(0, 63, 10))
    indices[paint] = STRUCTURE_FIRST + np.rint(shade[paint]).astype(np.uint8)
    pixels[..., :3] = source_palette[indices]
    image = Image.fromarray(pixels)
    prefix = "rail-" if geometry >= 7 * PIECES * 2 else ""
    part = geometry % (7 * PIECES * 2)
    name = f"{prefix}geometry-{part // 12}-{part // 2 % 6}-{part % 2}.png"
    image.save(output / name)
    ident = (RAIL_FIRST + part) if prefix else FIRST + part
    records[ident] = (image, BASELINE, 4, 0)
    files[ident] = output / name
    entries.append(dict(id=ident, file=name, baseline=BASELINE, scale_divisor=4, offset_x=0,
        palette="palette-source.png"))
  heads, head_files, head_entries = BuildBridgeHeads(source, output, quantizer, source_palette, original_luma)
  records.update(heads)
  files.update(head_files)
  entries.extend(head_entries)
  Image.fromarray(source_palette.reshape(1, 256, 3)).save(output / "palette-source.png")
  for style, palette in enumerate(palettes):
    Image.fromarray(palette.reshape(1, 256, 3)).save(output / f"palette-{style}.png")
  entries[0]["palettes"] = [f"palette-{style}.png" for style in range(13)]
  (output / "manifest.json").write_text(json.dumps(entries, indent=2) + "\n")
  for path in output.glob("*.png"):
    if path.name not in {e["file"] for e in entries} and not path.name.startswith(("palette-", "icon-", "front-")) and \
        path.name not in ("preview.png", "preview-rail.png", "preview-heads.png"): path.unlink()
  return layouts, records, files


def BuildTrafficForeground(source, output=OUTPUT):
  # Retain the source's actual front layer. Inferring depth from the finished
  # composite would confuse its pavement with beams and trusses crossing it.
  layouts = json.loads((OUTPUT / "layouts.json").read_text())["styles"]
  source_palette = np.array(Image.open(OUTPUT / "palette-source.png")).reshape(256, 3)
  quantizer = Image.new("P", (1, 1))
  quantizer.putpalette(np.vstack((source_palette[:STRUCTURE_FIRST],
      np.tile(source_palette[0], (64, 1)))).reshape(-1))
  original = np.array(Image.open(fetch(source, "graphics/palette_openttd.png")).getpalette()).reshape(256, 3)
  original_luma = original[0x46:0x50].astype(float) @ np.array([.2126, .7152, .0722])
  sheets, masks, entries = {}, {}, []
  for transport in ("road", "rail"):
    for geometry, style in enumerate((0, 1, 2, 3, 6, 9, 10)):
      for piece, axes in enumerate(layouts[style]["rail_pieces" if transport == "rail" else "pieces"]):
        for axis, layers in enumerate(axes):
          spec = layers[1]
          if spec is None:
            image = Image.new("RGBA", (128, 254))
          else:
            name = spec["sheet"]
            if name not in sheets:
              prefix = f"graphics/bridges/256/bridge_{name}"
              sheets[name] = transparent(Image.open(fetch(source, prefix + "_32bpp.png")))
              if name in ("girder", "suspension", "tubular"):
                masks[name] = Image.open(fetch(source, prefix + "_palmask.png")).convert("RGB")
            col, row = spec["column"], spec["row"]
            box = ((col + 1) * 4, (row + 1) * 4, (col + 65) * 4, (row + 128) * 4)
            source_image = sheets[name].crop(box)
            image = source_image.resize((128, 254), Image.Resampling.BOX)
            material = np.zeros_like(np.asarray(source_image))
            if name in masks:
              mask = np.asarray(masks[name].crop(box), dtype=float)
              painted = (mask[..., 0] > mask[..., 1] * 1.3) & (mask[..., 1] > mask[..., 2] * 1.1)
              material[..., :3] = painted[..., None] * 255
            material[..., 3] = np.asarray(source_image)[..., 3]
            painted = np.asarray(Image.fromarray(material).resize(image.size, Image.Resampling.BOX))[..., 0] >= 128
            pixels = np.array(image)
            indices = np.array(image.convert("RGB").quantize(palette=quantizer, dither=Image.Dither.NONE))
            indices[indices >= STRUCTURE_FIRST] = 0
            luma = pixels[..., :3].astype(float) @ np.array([.2126, .7152, .0722])
            shade = np.interp(luma, original_luma, np.linspace(0, 63, 10))
            indices[painted] = STRUCTURE_FIRST + np.rint(shade[painted]).astype(np.uint8)
            pixels[..., :3] = source_palette[indices]
            image = Image.fromarray(pixels)
          name = f"front-{transport}-{geometry}-{piece}-{axis}.png"
          image.save(output / name)
          entries.append(dict(file=name, baseline=BASELINE, scale_divisor=4, offset_x=0))
  _, _, heads = BuildBridgeHeads(source, output, quantizer, source_palette, original_luma, foreground_only=True)
  entries += [{key: entry[key] for key in ("file", "baseline", "scale_divisor", "offset_x")} for entry in heads]
  (output / "traffic-foreground.json").write_text(json.dumps(entries, indent=2) + "\n")


def BridgeHeadPreview(records, output):
  board = Image.new("RGB", (1088, 480), (34, 39, 44))
  draw = ImageDraw.Draw(board)
  font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
  font = ImageFont.truetype(str(font_path), 16) if font_path.exists() else ImageFont.load_default()
  for transport in range(2):
    draw.text((16, 16 + transport * 240), "Rail bridge approaches" if transport else "Road bridge approaches",
        fill="white", font=font)
    for shape in range(4):
      image = records[HEAD_FIRST + transport * 16 + shape][0]
      cropped = image.crop(image.getbbox())
      image = cropped.resize((cropped.width * 2, cropped.height * 2), Image.Resampling.NEAREST)
      board.paste(image, (16 + shape * 272, 56 + transport * 240), image)
  board.save(output / "preview-heads.png")


def BridgePreviews(layouts, records, output):
  font_path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
  font = ImageFont.truetype(str(font_path), 16) if font_path.exists() else ImageFont.load_default()
  source_colors = np.array(Image.open(output / "palette-source.png")).reshape(256, 3)
  lookup = {tuple(color): index for index, color in enumerate(source_colors)}
  for transport in ("road", "rail"):
    board = Image.new("RGB", (900, len(layouts) * 150 + 36), (34, 39, 44))
    draw = ImageDraw.Draw(board)
    draw.text((16, 10), f"OpenGFX2 {transport} bridges — native 128px decks", fill="white", font=font)
    first = RAIL_FIRST if transport == "rail" else FIRST
    for style, layout in enumerate(layouts):
      draw.text((16, 43 + style * 150), layout["name"].replace("_", " "), fill="white", font=font)
      scene = Image.new("RGBA", (512, 420))
      colors = np.array(Image.open(output / f"palette-{style}.png")).reshape(256, 3)
      for tile in range(1, 7):
        piece = BridgePiece(tile, 7 - tile)
        image = records[first + (GEOMETRY[style] * PIECES + piece) * 2][0]
        pixels = np.array(image)
        index = np.array([lookup[tuple(color)] for color in pixels[..., :3].reshape(-1, 3)]).reshape(pixels.shape[:2])
        pixels[..., :3] = colors[index]
        scene.alpha_composite(Image.fromarray(pixels), (16 + (6 - tile) * 64, 65 + tile * 32 - BASELINE + 100))
      cropped = scene.crop(scene.getbbox())
      fitted = cropped.copy(); fitted.thumbnail((650, 128), Image.Resampling.LANCZOS)
      board.paste(fitted, (240, 43 + style * 150), fitted)
      icon = Image.new("RGBA", (128, 128))
      fitted = cropped.copy(); fitted.thumbnail((124, 100), Image.Resampling.LANCZOS)
      icon.alpha_composite(fitted, ((128 - fitted.width) // 2, (128 - fitted.height) // 2))
      icon.save(output / (f"icon-rail-{style}.png" if transport == "rail" else f"icon-{style}.png"))
    board.save(output / ("preview-rail.png" if transport == "rail" else "preview.png"))


def BridgeCatalogMetadata(catalog):
  import os
  metadata = {}
  for entry in json.loads((OUTPUT / "manifest.json").read_text()):
    extra = {"palette": os.path.relpath(OUTPUT / entry["palette"], catalog.parent)}
    if "palettes" in entry:
      extra["palettes"] = [os.path.relpath(OUTPUT / name, catalog.parent) for name in entry["palettes"]]
    metadata[entry["id"]] = extra
  return metadata


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--source", type=Path, default=Path("/tmp/sc2k-opengfx2-source"))
  parser.add_argument("--catalog", type=Path, default=SPRITE_CATALOG)
  parser.add_argument("--traffic-foreground-only", action="store_true")
  args = parser.parse_args()
  if args.traffic_foreground_only:
    BuildTrafficForeground(args.source)
    print(f"Exported front bridge layers: {OUTPUT}")
    return
  layouts, records, files = BuildBridgeSprites(args.source)
  BuildTrafficForeground(args.source)
  retained = [entry for entry in json.loads(args.catalog.read_text())
      if not FIRST <= entry["id"] < FIRST + 7 * PIECES * 2 and
      not RAIL_FIRST <= entry["id"] < RAIL_FIRST + 7 * PIECES * 2 and
      not HEAD_FIRST <= entry["id"] < HEAD_FIRST + HEAD_COUNT]
  metadata = BridgeCatalogMetadata(args.catalog)
  ExportSpriteSources(args.catalog, records, files, retained, metadata)
  BridgePreviews(layouts, records, OUTPUT)
  BridgeHeadPreview(records, OUTPUT)
  print(f"Exported {len(records)} bridge pieces: {OUTPUT}")


if __name__ == "__main__":
  main()
