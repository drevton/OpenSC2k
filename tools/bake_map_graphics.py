#!/usr/bin/env python3
"""Bake tile-layer bindings and a direction-agnostic array of sprite references."""
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
DEFINITIONS = ROOT / 'assets/landscape/map-graphics.json'


def ReadCityMapKindSprites():
  kinds = {name: int(value) for name, value in re.findall(r'(TK_\w+)\s*=\s*(\d+)',
      (ROOT / 'src/city/maps/tile_storage.h').read_text())}
  layouts = {name: int(value) for name, value in re.findall(r'(CITY_MAP_TILE_SPRITE_\w+)\s*=\s*(\d+)',
      (ROOT / 'src/assets/graphics.h').read_text())}
  source = (ROOT / 'src/drawing/maps/tables.c').read_text()
  table = re.search(r'const u16 kCityMapKindSprites\[TK_COUNT\] = \{(.*?)\};', source, re.S).group(1)
  tiles = [0] * kinds['TK_COUNT']
  seen = set()
  for kind, layout in re.findall(r'\[(TK_\w+)\]\s*=\s*(CITY_MAP_TILE_SPRITE_\w+)', table):
    index = kinds[kind]
    if index in seen: raise ValueError('Duplicate native kind sprite')
    seen.add(index)
    tiles[index] = layouts[layout]
  return tiles


def BakeMapGraphics(destination, entries, first_civic, first_building):
  spec = json.loads(DEFINITIONS.read_text())
  first_group = (max(entry['id'] for entry in entries) + 4) & ~3
  bindings = [None] * 256
  groups, references, variants = [], [], {}
  sources = {entry['id'] for entry in entries}
  for group in spec['groups']:
    layouts, sprites = group['slots'], group['sprites']
    if len(layouts) != 4 or len(sprites) != 4:
      raise ValueError(f"Expected four entries in {group['name']}")
    first = first_group + len(references)
    for direction, (layout, sprite) in enumerate(zip(layouts, sprites)):
      if not 0 <= layout < len(bindings): raise ValueError('Invalid tile layout')
      if bindings[layout] is None: bindings[layout] = first | direction
      elif bindings[layout] // 4 != first // 4: raise ValueError('Layout belongs to multiple groups')
      groups.append(layout)
      source = sprite.get('source')
      if source is None:
        building, view = sprite['building'], sprite['view']
        if not 0 <= view < 4: raise ValueError('Invalid authored building view')
        if 112 <= building < 198: source = first_building + (building - 112) * 4 + view
        elif 198 <= building < 256: source = first_civic + (building - 198) * 4 + view
        elif building == 13: source = first_civic + 58 * 4 + view
        else: raise ValueError('Invalid building source')
      if source not in sources or source > 65535: raise ValueError(f'Missing sprite source {source}')
      references.append((source, int(sprite.get('flipped', False)), 0))
      if sprite.get('variants'):
        variants[first | direction] = sprite['variants']
  if None in bindings or first_group > 32767 or first_group + len(references) > 65536:
    raise ValueError('Incomplete or oversized sprite catalog')
  # Height metadata uses the same compiled mapping as Drawing. It is not part of
  # the resource bindings and cannot be overridden by an artwork pack.
  tiles = ReadCityMapKindSprites()
  if any(layout >= len(bindings) for layout in tiles): raise ValueError('Invalid native kind sprite')
  layouts = bytearray(struct.pack('<8sIII', b'SC2KTIL1', len(bindings), first_group, len(groups)))
  layouts += struct.pack(f'<{len(bindings)}H', *bindings)
  rotations = [groups[((sprite & ~3) | ((sprite + turn) & 3)) - first_group]
      for sprite in bindings for turn in range(4)]
  layouts += struct.pack(f'<{len(rotations)}H', *rotations)
  path = destination / 'tile-graphics.dat'
  temporary = path.with_suffix('.tmp')
  temporary.write_bytes(layouts)
  temporary.replace(path)
  native = {}
  for layout in (13, *range(112, 256)):
    first = first_building + (layout - 112) * 4 if layout < 198 and layout != 13 else \
        first_civic + (58 if layout == 13 else layout - 198) * 4
    sprite = bindings[layout]
    for direction in range(4):
      native[first + direction] = references[((sprite & ~3) | ((sprite + direction) & 3)) - first_group]
  return references, native, tiles, variants


def RemapMapGraphicsMetadata(measured, baked_ground, references):
  original_measurements, original_ground = dict(measured), set(baked_ground)
  for target, reference in references.items():
    source = reference[0]
    size, height = original_measurements.get(source, (0, 0))
    old_size = original_measurements.get(target, (size, 0))[0]
    if size != old_size:
      raise ValueError('A sprite reference changes its building footprint')
    measured[target] = (size, height)
    baked_ground.discard(target)
    if source in original_ground: baked_ground.add(target)


def EncodeSpriteVariants(variants, lookup):
  """Sparse image-level overrides; packed references use the ordinary header namespace."""
  data = bytearray()
  for sprite, levels in sorted(variants.items()):
    if not 0 <= sprite < len(lookup): raise ValueError('Invalid variant sprite ID')
    levels = {int(level): source for level, source in levels.items()}
    if not levels or any(level < 0 or level > 3 for level in levels):
      raise ValueError('Expected image levels 0..3')
    data += struct.pack('<HBB', sprite, sum(1 << level for level in levels), 0)
    for level, source in sorted(levels.items()):
      if isinstance(source, int): source = dict(source=source)
      index = source['source']
      if not 0 <= index < len(lookup) or not lookup[index]: raise ValueError('Missing variant image')
      data += struct.pack('<H', lookup[index] ^ (int(source.get('flipped', False)) << 15))
  return data
