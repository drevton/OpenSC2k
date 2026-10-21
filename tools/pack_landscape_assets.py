#!/usr/bin/env python3
"""Pack checked-in landscape PNGs and anchors; requires only Pillow, with no downloads."""
import argparse
import json
import os
from pathlib import Path
import struct

from PIL import Image, ImageChops

from bake_map_graphics import BakeMapGraphics, RemapMapGraphicsMetadata, EncodeSpriteVariants

ROOT = Path(__file__).resolve().parents[1]
SPRITE_CATALOG = ROOT / "assets/landscape/opengfx2/sprites.json"
CIVIC_DIRECTORY = ROOT / "assets/landscape/civic"
BUILDING_DIRECTORY = ROOT / "assets/landscape/buildings"
BUILDING_KIND_FIRST = 112
BUILDING_KIND_COUNT = 86
CONSTRUCTION_HEIGHTS = (32, 64, 128, 192, 320, 512)
CONSTRUCTION_PHASES = (.25, .6, 1)
CONSTRUCTION_PIECES = 1 + 2 * len(CONSTRUCTION_HEIGHTS) * len(CONSTRUCTION_PHASES)
SMALL_PARK_KIND = 13
CIVIC_KIND_FIRST = 198
CIVIC_KIND_COUNT = 58
GROUND_MATERIALS = ('terrain', 'stone', 'concrete', 'asphalt', 'gravel', 'brick', 'earth')
GROUND_BAKED = 0x80
CONSTRUCTION_GROUND_BAKED = 0x40
POWER_SMOKE_DIRECTORY = CIVIC_DIRECTORY / "power-smoke"
POWER_SMOKE_KINDS = (202, 203, 207)
POWER_SMOKE_FRAMES = 16
WIND_DIRECTORY = CIVIC_DIRECTORY / "200-wind-power/animation"
WIND_FRAMES = 16
WATER_COLORS = ((32, 68, 112), (36, 72, 116), (40, 76, 120), (44, 80, 124), (48, 84, 128))
# Upstream indices 250..254 animate independently of dark water (245..249).
# Preserve their palette identity when encoding RGB sprites.
WATER_GLITTER_COLORS = ((72, 100, 144), (100, 132, 168), (216, 244, 252), (96, 128, 164), (68, 96, 140))
WATER_PALETTE_COLORS = WATER_COLORS + WATER_GLITTER_COLORS

RUN_PAYLOAD_BYTES = (0, 3, 4, 1, 2, 1, 2, 0)
ANIMATION_WATER_FIRST = 66
LANDSCAPE_MAGIC = b"SC2KLAN2"


def picture_pack(images):
  """Static UI pictures use native runs and optional per-image RGB palettes."""
  packed = bytearray(struct.pack('<8sI', b'SC2KPIC1', len(images)))
  for ident, image in images:
    payload = encode(image, water_animation=False)
    palette = bytes(image.getpalette()[:768]).ljust(768, b'\0') if image.mode == 'P' else b''
    packed += struct.pack('<4HI', ident, image.width, image.height, 256 if palette else 0, len(payload))
    packed += palette + payload
  return packed


def CleanCivicAlpha(image):
  """Discard invisible resampling remnants before measuring or packing a building."""
  image = image.convert('RGBA')
  alpha = image.getchannel('A').point(lambda value: 0 if value <= 4 else value)
  image.putalpha(alpha)
  image.paste((0, 0, 0, 0), (0, 0, image.width, image.height), alpha.point(lambda value: 255 if value == 0 else 0))
  return image


def run_header(kind, length):
  if not 0 <= kind < 7 or not 1 <= length <= 65535:
    raise ValueError("Invalid sprite run")
  header = bytes((kind << 5 | min(length - 1, 31),))
  if length >= 32:
    header += bytes((length - 32,)) if length <= 286 else b"\xff" + struct.pack("<H", length)
  return header


def initial_skip(length):
  return bytes((length,)) if length < 255 else b"\xff" + struct.pack("<H", length)


def row_runs(payload, height):
  """Yield (row, x, mode, length, pixel bytes) from trusted packaged data."""
  offsets = struct.unpack_from(f"<{height + 1}I", payload)
  data = memoryview(payload)[4 * (height + 1):]
  for y in range(height):
    at, end = offsets[y:y + 2]
    if at == end:
      continue
    x, at = data[at], at + 1
    if x == 255:
      x, at = struct.unpack_from("<H", data, at)[0], at + 2
    while at < end:
      header, at = data[at], at + 1
      kind, length = header >> 5, (header & 31) + 1
      if length == 32:
        extension, at = data[at], at + 1
        length = 32 + extension
        if extension == 255:
          length, at = struct.unpack_from("<H", data, at)[0], at + 2
      size = length * RUN_PAYLOAD_BYTES[kind]
      yield y, x, kind, length, data[at:at + size]
      x, at = x + length, at + size


def encode(image, animation_indices=None, palette=None, animation_mask=None, water_animation=True):
  """One-byte mode/length headers, initial row skips and omitted trailing skips.

  Static palette runs contain source indices; animated runs contain compact
  animation IDs. Optional animation_indices maps indexed source entries to IDs.
  Both have an optional alpha byte, with no brightness sentinel.
  """
  width, height = image.size
  if not 0 < width <= 65535 or height <= 0:
    raise ValueError("Invalid sprite dimensions")
  pixels = list(image.getdata())
  indexed = image.mode == "P" or palette is not None
  transparency = image.info.get("transparency")
  animation_indices = animation_indices or {}
  animation = list(animation_mask.getdata()) if animation_mask is not None else None
  if animation_mask is not None and (animation_mask.size != image.size or
      animation_mask.mode != "L" or any(not 0 <= ident < 76 for ident in animation)):
    raise ValueError("Animation mask must match the sprite and contain compact IDs 0..75")
  if any(not 0 <= index <= 255 or not 1 <= ident < 76 for index, ident in animation_indices.items()):
    raise ValueError("Invalid source palette index or animation ID")

  def alpha_at(pixel):
    if palette is not None or not indexed:
      return pixel[3]
    if isinstance(transparency, bytes):
      return transparency[pixel] if pixel < len(transparency) else 255
    return 0 if pixel == transparency else 255

  def kind_at(pixel, position):
    alpha = alpha_at(pixel)
    if not alpha:
      return 0
    if animation is not None and animation[position]:
      return 5 if alpha == 255 else 6
    if indexed:
      if index_at(pixel) in animation_indices:
        return 5 if alpha == 255 else 6
      return 3 if alpha == 255 else 4
    if water_animation and pixel[:3] in WATER_PALETTE_COLORS:
      return 5 if alpha == 255 else 6
    return 1 if alpha == 255 else 2

  def index_at(pixel):
    return palette[pixel[:3]] if palette is not None else pixel

  data, offsets = bytearray(), []
  for y in range(height):
    offsets.append(len(data))
    row = pixels[y * width:(y + 1) * width]
    row_start = y * width
    first, last = 0, width
    while first < last and not kind_at(row[first], row_start + first):
      first += 1
    while last > first and not kind_at(row[last - 1], row_start + last - 1):
      last -= 1
    if first == last:
      continue
    data += initial_skip(first)
    x = first
    while x < last:
      start, kind = x, kind_at(row[x], row_start + x)
      x += 1
      while x < last and x - start < 65535 and kind_at(row[x], row_start + x) == kind:
        x += 1
      data += run_header(kind, x - start)
      if kind:
        for offset, pixel in enumerate(row[start:x], start):
          if animation is not None and kind in (5, 6) and animation[row_start + offset]:
            data += bytes((animation[row_start + offset],))
          elif indexed:
            index = index_at(pixel)
            data += bytes((animation_indices[index] if kind in (5, 6) else index,))
          elif kind in (5, 6):
            data += bytes((WATER_PALETTE_COLORS.index(pixel[:3]) + ANIMATION_WATER_FIRST,))
          else:
            data += bytes(pixel[:3])
          if kind in (2, 4, 6):
            data += bytes((alpha_at(pixel),))
  offsets.append(len(data))
  return struct.pack(f"<{len(offsets)}I", *offsets) + data


def ArtworkEntries(first, directory=CIVIC_DIRECTORY):
  """Admit complete four-view sets only after checking their actual bitmap ownership."""
  catalog = json.loads((directory / 'catalog.json').read_text())
  building = directory == BUILDING_DIRECTORY
  footprints = {entry['kind']: entry['footprint_tiles'][0] for entry in catalog['buildings']} if building else {
      entry['kind']: entry['tiles_wide'] for entry in catalog}
  with Image.open(ROOT / 'assets/landscape/opengfx2/detail128/terrain-masks.png') as source:
    tile = source.convert('RGBA').crop((0, 0, 128, 128)).getchannel('A')
  entries, seen = [], set()
  for manifest in sorted(directory.glob('*/sprites.json')):
    spec = json.loads(manifest.read_text())
    kind = spec['kind']
    if kind in seen or not (BUILDING_KIND_FIRST <= kind < BUILDING_KIND_FIRST + BUILDING_KIND_COUNT if building else
        kind == SMALL_PARK_KIND or CIVIC_KIND_FIRST <= kind < CIVIC_KIND_FIRST + CIVIC_KIND_COUNT):
      raise ValueError(f'Duplicate or invalid civic kind: {manifest}')
    seen.add(kind)
    if building and spec.get('status') == 'master view; rotations pending':
      raise ValueError(f'Building artwork is still incomplete: {manifest}')
    size = footprints[kind]
    width, height = spec['canvas']
    if spec['pixels_per_tile'] != 128 or spec['footprint_tiles'] != [size, size] or width != size * 128 or \
        not width // 2 <= height <= 2048 or spec['baseline'] != height:
      raise ValueError(f'Invalid civic dimensions or foundation: {manifest}')
    views = spec['views']
    if len(views) != 4 or [view['angle'] for view in views] != [0, 90, 180, 270]:
      raise ValueError(f'Expected four clockwise civic views: {manifest}')
    config = json.loads((manifest.parent / spec['config']).read_text())
    if config['kind'] != kind or config['footprint_tiles'] != [size, size]:
      raise ValueError(f'Civic bake does not match the manifest: {manifest}')
    authored_site = spec.get('ground') == 'painted' and \
        config.get('retain_painted_ground') and not config.get('omit_ground_in_sprite')
    if not authored_site and spec.get('status') != 'shared construction artwork' and (
        not config.get('omit_ground_in_sprite') or spec.get('ground') not in GROUND_MATERIALS):
      raise ValueError(f'Finished building requires separate reusable ground: {manifest}')
    with Image.open(manifest.parent / config['mask']) as source:
      mask = source.convert('L')
    native = Image.new('L', (width, height))
    for u in range(size):
      for v in range(size):
        layer = Image.new('L', native.size)
        layer.paste(tile, (width // 2 + 64 * (u - v) - 64, height - 64 * size + 32 * (u + v) - 64))
        native = ImageChops.lighter(native, layer)
    if mask.size != native.size or ImageChops.difference(mask, native).getbbox():
      raise ValueError(f'Civic ground does not match the native tile union: {manifest}')
    prism = Image.new('L', mask.size)
    for x in range(width):
      column = mask.crop((x, 0, x + 1, height)).getbbox()
      if column:
        prism.paste(255, (x, 0, x + 1, column[3]))
    registration = config.get('structure_registration')
    upper_mask = None
    if registration:
      bottom = registration['shared_silhouette_bottom']
      if not 0 < bottom < height - width // 2:
        raise ValueError(f'Invalid shared upper silhouette: {manifest}')
      reference = views[registration['reference_view']]['file']
      with Image.open(manifest.parent / reference) as source:
        upper_mask = source.convert('RGBA').getchannel('A').crop((0, 0, width, bottom))
    crop_top = height - width // 2
    for view, entry in enumerate(views):
      filename = manifest.parent / entry['file']
      with Image.open(filename) as source:
        image = source.convert('RGBA')
      if image.size != native.size or \
          ImageChops.multiply(image.getchannel('A'), ImageChops.invert(prism)).getbbox():
        raise ValueError(f'Civic sprite exceeds its site or changes canvas: {filename}')
      if authored_site and ImageChops.subtract(native, image.getchannel('A')).getbbox():
        raise ValueError(f'Authored site leaves its ground dependent on underlying terrain: {filename}')
      if upper_mask is not None and ImageChops.difference(upper_mask,
          image.getchannel('A').crop((0, 0, width, registration['shared_silhouette_bottom']))).getbbox():
        raise ValueError(f'Civic upper structure changes size or silhouette between views: {filename}')
      with Image.open(manifest.parent / entry.get('ground_file', f'ground-{view}.png')) as source:
        ground = source.convert('RGBA').getchannel('A')
      if ground.size != native.size or ImageChops.difference(ground, native).getbbox():
        raise ValueError(f'Civic baked ground differs from its foundation: {filename}')
      if not building:
        bounds = CleanCivicAlpha(image).getchannel('A').getbbox()
        if bounds: crop_top = min(crop_top, bounds[1])
      slot = kind - BUILDING_KIND_FIRST if building else \
          CIVIC_KIND_COUNT if kind == SMALL_PARK_KIND else kind - CIVIC_KIND_FIRST
      record = dict(id=first + slot * 4 + view, file=str(filename),
          baseline=height, scale_divisor=4, offset_x=0, civic=True, civic_kind=kind, height_size=size)
      if spec.get('ground') in GROUND_MATERIALS[1:]:
        record['bake_ground'] = spec['ground']
      if spec.get('encoding'):
        record['encoding'] = spec['encoding']
      record['mirror'] = bool(entry.get('mirror', False))
      if 'animation_mask' in entry:
        record['animation_mask'] = str(manifest.parent / entry['animation_mask'])
      entries.append(record)
    # Use one crop for all rotations, preserving their common size and ground anchor.
    for record in entries[-4:]:
      record['crop_top'] = 0 if building else crop_top
      record['baseline'] -= record['crop_top']
  if building and seen != set(range(BUILDING_KIND_FIRST,BUILDING_KIND_FIRST+BUILDING_KIND_COUNT)):
    raise ValueError('Expected artwork for every building kind 112..197')
  return entries


def ConstructionEntries(first):
  directory = BUILDING_DIRECTORY / 'construction'
  manifest = directory / 'kit.json'
  if not manifest.exists():
    raise ValueError('Missing shared construction artwork kit')
  spec = json.loads(manifest.read_text())
  if spec['pixels_per_tile'] != 128:
    raise ValueError('Construction artwork must use128pixels per tile')
  entries,footprints = [],set()
  for site in spec['sites']:
    size = site['footprint']
    if not 1 <= size <= 3 or size in footprints:
      raise ValueError('Invalid construction kit footprint')
    footprints.add(size)
    slot = first + (size - 1) * CONSTRUCTION_PIECES
    with Image.open(directory / site['foundation']) as source:
      foundation = source.convert('RGBA')
    baseline = site['foundation_baseline']
    if foundation.width != size * 128 or baseline != foundation.height:
      raise ValueError('Construction foundation must retain its native footprint and baseline')
    entries.append(dict(id=slot, file=str(directory / site['foundation']),
        baseline=baseline, scale_divisor=4, offset_x=0, civic=True, height_size=size, ground_coverage=True))
    for family, name in enumerate(('timber', 'steel')):
      with Image.open(directory / site[name]) as source:
        module = source.convert('RGBA')
      step = site[name + '_step']
      if not 8 <= step <= 64 or module.width != size * 128:
        raise ValueError('Invalid repeatable construction floor')
      for profile, height in enumerate(CONSTRUCTION_HEIGHTS):
        for phase, fraction in enumerate(CONSTRUCTION_PHASES):
          canvas_height = max(baseline, size * 64 + height + step)
          image = Image.new('RGBA', (module.width, canvas_height))
          image.alpha_composite(foundation, (0, canvas_height - baseline))
          floors = max(1, round(height * fraction / step))
          for floor in range(floors):
            image.alpha_composite(module, (0, canvas_height - module.height - floor * step))
          piece = 1 + (family * len(CONSTRUCTION_HEIGHTS) + profile) * len(CONSTRUCTION_PHASES) + phase
          entries.append(dict(id=slot + piece, image=image, baseline=canvas_height,
              scale_divisor=4, offset_x=0, civic=True, height_size=size, ground_coverage=True))
  if footprints != {1,2,3}:
    raise ValueError('Construction kit must cover all three building footprints')
  return entries


def WriteBuildingMetadata(output, baked_ground, first):
  """Construction height, frame family and ground material; no finished-building cutaways."""
  manifests = {}
  for path in BUILDING_DIRECTORY.glob('*/sprites.json'):
    spec = json.loads(path.read_text())
    if spec.get('status') != 'master view; rotations pending':
      manifests[spec['kind']] = (path, spec)
  output.write(b'SC2KBLD2')
  metadata = {}
  for kind in range(BUILDING_KIND_FIRST, BUILDING_KIND_FIRST + BUILDING_KIND_COUNT):
    if kind not in manifests:
      raise ValueError(f'Missing complete building artwork: {kind}')
    path, spec = manifests[kind]
    width, height = spec['canvas']
    images = [Image.open(path.parent / view['file']).convert('RGBA') for view in spec['views']]
    top = min(image.getchannel('A').getbbox()[1] for image in images)
    structure = spec.get('construction', {}).get('height', max(16, height - width // 4 - top))
    family = spec.get('construction', {}).get('family', 1)
    if not 0 < structure <= 1024 or family not in (0, 1):
      raise ValueError(f'Invalid construction metadata: {path}')
    ground = GROUND_MATERIALS.index(spec.get('ground','terrain'))
    if all(first + (kind - BUILDING_KIND_FIRST) * 4 + view in baked_ground for view in range(4)):
      ground |= GROUND_BAKED
    construction = first + BUILDING_KIND_COUNT * 4 + (width // 128 - 1) * CONSTRUCTION_PIECES
    if all(construction + piece in baked_ground for piece in range(CONSTRUCTION_PIECES)):
      ground |= CONSTRUCTION_GROUND_BAKED
    output.write(struct.pack('<5I', structure, family, width, height, ground))
    metadata[kind] = (structure, family)
  return metadata


def WriteCivicGrounds(output, baked_ground, first):
  """Immutable per-artwork base choices, including the separate Small Park slot."""
  grounds = bytearray(CIVIC_KIND_COUNT + 1)
  seen = set()
  for path in CIVIC_DIRECTORY.glob('*/sprites.json'):
    spec = json.loads(path.read_text())
    kind = spec['kind']
    slot = CIVIC_KIND_COUNT if kind == SMALL_PARK_KIND else kind-CIVIC_KIND_FIRST
    if not 0 <= slot < len(grounds) or kind in seen:
      raise ValueError(f'Invalid or duplicate civic ground: {path}')
    seen.add(kind)
    grounds[slot] = 0 if spec['ground'] == 'painted' else GROUND_MATERIALS.index(spec['ground'])
    if all(first + slot * 4 + view in baked_ground for view in range(4)):
      grounds[slot] |= GROUND_BAKED
  # Kind 199 is retired hydro artwork; its native drawing uses kind 198.
  if seen != {SMALL_PARK_KIND, *range(CIVIC_KIND_FIRST, CIVIC_KIND_FIRST+CIVIC_KIND_COUNT)} - {199}:
    raise ValueError('Missing civic or special ground metadata')
  output.write(b'SC2KGRND')
  output.write(grounds)


def PowerSmokeAssets(first):
  spec = json.loads((POWER_SMOKE_DIRECTORY / 'smoke.json').read_text())
  if spec['frames'] != POWER_SMOKE_FRAMES or spec['fps'] != 3 or \
      spec['canvas'] != [128, 128] or spec['anchors'] != [[40, 128], [64, 128]] or \
      spec['baselines'] != [128, 110] or \
      [entry['kind'] for entry in spec['kinds']] != list(POWER_SMOKE_KINDS):
    raise ValueError('Invalid shared power smoke metadata')
  entries, frames = [], []
  for effect, name in enumerate(('smoke', 'steam')):
    images = []
    for frame in range(POWER_SMOKE_FRAMES):
      filename = POWER_SMOKE_DIRECTORY / f'{name}-{frame:02d}.png'
      with Image.open(filename) as source:
        image = source.convert('RGBA')
      if image.size != (128, 128) or not image.getchannel('A').getbbox():
        raise ValueError(f'Invalid power smoke frame: {filename}')
      images.append(image)
      entries.append(dict(id=first + effect * POWER_SMOKE_FRAMES + frame, file=str(filename),
          baseline=spec['baselines'][effect], scale_divisor=4, offset_x=0, civic=True))
    frames.append(images)
  for plant in spec['kinds']:
    if len(plant['views']) != 4: raise ValueError('Power smoke needs four emitter layouts')
    manifest = next(path for path in CIVIC_DIRECTORY.glob('*/sprites.json')
        if json.loads(path.read_text())['kind'] == plant['kind'])
    directory = manifest.parent
    shape = json.loads(manifest.read_text())
    config = json.loads((directory / shape['config']).read_text())
    mask = Image.open(directory / config['mask']).convert('L')
    prism = Image.new('L', mask.size)
    for x in range(mask.width):
      column = mask.crop((x, 0, x + 1, mask.height)).getbbox()
      if column: prism.paste(255, (x, 0, x + 1, column[3]))
    for view, emitters in enumerate(plant['views']):
      mirrored = shape['views'][view].get('mirror', False)
      site = prism.transpose(Image.Transpose.FLIP_LEFT_RIGHT) if mirrored else prism
      if not 1 <= len(emitters) <= 2: raise ValueError('Invalid power smoke emitter count')
      for emitter in emitters:
        x, y, effect, flipped = (emitter[key] for key in ('x', 'y', 'type', 'flipped'))
        if not 0 <= x < shape['canvas'][0] or not 0 <= y < shape['canvas'][1] or \
            effect not in (0, 1) or flipped not in (False, True):
          raise ValueError('Invalid power smoke emitter')
        if mirrored:
          x = emitter['x'] = shape['canvas'][0] - x
          flipped = emitter['flipped'] = not flipped
        for image in frames[effect]:
          if flipped: image = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
          layer = Image.new('RGBA', mask.size)
          anchor = spec['anchors'][effect][0]
          if flipped: anchor = 128 - anchor
          layer.alpha_composite(image, (x - anchor, y - spec['baselines'][effect]))
          if ImageChops.multiply(layer.getchannel('A'), ImageChops.invert(site)).getbbox():
            raise ValueError('Animated smoke exceeds its native site')
  return entries, spec['kinds']


def ExportSpriteSources(catalog, records, source_files=None, retained=(), metadata=None):
  """Save each generated image separately; infrastructure can reference its existing PNG."""
  source_files = source_files or {}
  directory = catalog.parent / "sprites"
  directory.mkdir(parents=True, exist_ok=True)
  entries = {entry["id"]: entry for entry in retained}
  for ident, (image, baseline, scale, offset) in sorted(records.items()):
    filename = source_files.get(ident)
    if filename is None:
      filename = directory / f"{ident:04d}.png"
      image.save(filename)
    entries[ident] = dict(id=ident, file=Path(os.path.relpath(filename, catalog.parent)).as_posix(),
        baseline=baseline, scale_divisor=scale, offset_x=offset)
    entries[ident].update((metadata or {}).get(ident, {}))
  catalog.write_text(json.dumps(sorted(entries.values(), key=lambda entry: entry["id"]), indent=2) + "\n")
  used = {entry["file"] for entry in entries.values()}
  for filename in directory.glob("[0-9][0-9][0-9][0-9].png"):
    if filename.relative_to(catalog.parent).as_posix() not in used:
      filename.unlink()


def WindPowerAssets(first):
  spec = json.loads((WIND_DIRECTORY / 'wind.json').read_text())
  if spec['frames'] != WIND_FRAMES or spec['canvas'] != [128, 320]:
    raise ValueError('Invalid wind-power animation metadata')
  mask = Image.open(WIND_DIRECTORY.parent / 'foundation-mask.png').convert('L')
  prism = Image.new('L', mask.size)
  for x in range(mask.width):
    column = mask.crop((x, 0, x + 1, mask.height)).getbbox()
    if column: prism.paste(255, (x, 0, x + 1, column[3]))
  filenames = [f'modern-base-{view}.png' for view in range(4)]
  filenames += [f'{form}-rotor-{view}-{frame:02d}.png'
      for form in ('old', 'modern') for view in range(4) for frame in range(WIND_FRAMES)]
  entries = []
  for index, name in enumerate(filenames):
    filename = WIND_DIRECTORY / name
    with Image.open(filename) as source:
      image = source.convert('RGBA')
    if image.size != mask.size or ImageChops.multiply(image.getchannel('A'), ImageChops.invert(prism)).getbbox():
      raise ValueError(f'Wind-power artwork exceeds its site: {filename}')
    if index < 4 and ImageChops.multiply(ImageChops.invert(image.getchannel('A')), mask).getbbox():
      raise ValueError(f'Incomplete wind-power ground: {filename}')
    entries.append(dict(id=first + index, file=str(filename), baseline=320,
        scale_divisor=4, offset_x=0, civic=True))
  # A complete stationary frame lets the chooser use the same simple sprite path.
  for form in ('old', 'modern'):
    for view in range(4):
      entries.append(dict(id=first + len(entries), file=str(WIND_DIRECTORY / f'{form}-base-{view}.png'),
          overlay=str(WIND_DIRECTORY / f'{form}-rotor-{view}-00.png'), baseline=320,
          scale_divisor=4, offset_x=0, civic=True))
  manifest = json.loads((WIND_DIRECTORY.parent / 'sprites.json').read_text())
  if manifest.get('single_view_rotor_pending'):
    filename = WIND_DIRECTORY.parent / manifest['views'][0]['file']
    width, height = manifest['canvas']
    for view in range(4):
      for frame in range(WIND_FRAMES):
        entry = entries[4 + view * WIND_FRAMES + frame]
        entry.pop('file')
        entry['image'] = Image.new('RGBA', (width, height))
        entry['baseline'] = height
      entry = entries[4 + 2 * 4 * WIND_FRAMES + view]
      entry.update(file=str(filename), baseline=height, encoding=manifest.get('encoding'),
          mirror=manifest['views'][view].get('mirror', False))
      entry.pop('overlay')
  return entries


# Compact command stream. Integer fields use unsigned LEB128, offsets use zigzag.
GRAPHICS_END, GRAPHICS_PALETTE, GRAPHICS_IMAGE, GRAPHICS_SPRITE, \
    GRAPHICS_WATER_COLORS, GRAPHICS_BRIDGE_PALETTES = range(6)


def WriteGraphicsInteger(output, value):
  if not 0 <= value <= 0xFFFFFFFF:
    raise ValueError('Graphics integer outside u32')
  while value >= 128:
    output.write(bytes(((value & 127) | 128,)))
    value >>= 7
  output.write(bytes((value,)))


def WriteGraphicsCommand(output, command, *values):
  output.write(bytes((command,)))
  for value in values:
    WriteGraphicsInteger(output, value)


def MeasureBuildingHeight(image, baseline, size, scale=4):
  """Maximum occupied height above any footprint tile, in smallest-zoom pixels."""
  tile_width = 32 * scale
  if not 1 <= size <= 4 or image.width != size * tile_width:
    raise ValueError('Invalid building footprint')
  alpha = image.convert('RGBA').getchannel('A')
  height = 4
  for y in range(size):
    for x in range(size):
      left = tile_width // 2 * (size - 1 + y - x)
      bottom = baseline - tile_width // 2 * (size - 1) + tile_width // 4 * (x + y)
      bounds = alpha.crop((left, 0, left + tile_width, bottom)).getbbox()
      if bounds: height = max(height, (bottom - bounds[1] + 4 * scale - 1) // (4 * scale))
  if height > 255:
    raise ValueError('Building height exceeds one byte at the smallest zoom')
  return height


def WriteKindHeightMetadata(output, measured, first_civic, first_building, tiles, buildings):
  by_layout = {}
  for first, count in ((first_civic, CIVIC_KIND_COUNT + 1), (first_building, BUILDING_KIND_COUNT)):
    for slot in range(count):
      size = measured[first + slot * 4][0]
      height = 0
      for view in range(4):
        other_size, other_height = measured[first + slot * 4 + view]
        if size != other_size: raise ValueError('Building footprints differ between views')
        height = max(height, other_height)
      layout = BUILDING_KIND_FIRST + slot if first == first_building else \
          SMALL_PARK_KIND if slot == CIVIC_KIND_COUNT else CIVIC_KIND_FIRST + slot
      if layout in buildings:
        structure, family = buildings[layout]
        profile = 0
        while profile + 1 < len(CONSTRUCTION_HEIGHTS) and \
            (CONSTRUCTION_HEIGHTS[profile] + CONSTRUCTION_HEIGHTS[profile + 1]) // 2 < structure:
          profile += 1
        construction = first_building + BUILDING_KIND_COUNT * 4 + (size - 1) * CONSTRUCTION_PIECES
        height = max(height, measured[construction][1])
        for phase in range(len(CONSTRUCTION_PHASES)):
          piece = 1 + (family * len(CONSTRUCTION_HEIGHTS) + profile) * len(CONSTRUCTION_PHASES) + phase
          other_size, other_height = measured[construction + piece]
          if size != other_size: raise ValueError('Construction footprint mismatch')
          height = max(height, other_height)
      by_layout[layout] = height
  output.write(b'SC2KHGT1')
  output.write(bytes(by_layout.get(layout, 0) for layout in tiles))


def WriteGraphicsSprites(output, entries, catalog, banks, measured, baked_ground, references, variants):
  """Image bytes, palettes and asset identities are independent, shared resources."""
  import hashlib
  palettes, images, files, bindings = {}, {}, {}, {}
  stats = dict(assets=0, images=0, palettes=0, references=0, mirrored=0)

  def Palette(colors):
    if colors not in palettes:
      ident = len(palettes) + 1
      palettes[colors] = ident
      WriteGraphicsCommand(output, GRAPHICS_PALETTE, ident)
      output.write(bytes((len(colors) // 3 - 1,)))
      output.write(colors)
    return palettes[colors]

  first_group = (max(entry['id'] for entry in entries) + 4) & ~3
  if max(entry['id'] for entry in entries) >= 32767:
    raise ValueError('Shared sprite header IDs exceed fifteen bits')
  lookup = [0] * (first_group + len(references))
  output.write(struct.pack('<8sI', b'SC2KGFX3', len(lookup)))
  lookup_offset = output.tell()
  output.write(bytes(2 * len(lookup)))
  variants = dict(variants)
  for entry in entries:
    if entry.get('variants'): variants[entry['id']] = entry['variants']
  for index, (source, flipped, _) in enumerate(references):
    target = first_group + index
    if target in variants or source not in variants: continue
    variants[target] = {}
    for level, replacement in variants[source].items():
      replacement = dict(source=replacement) if isinstance(replacement, int) else dict(replacement)
      replacement['flipped'] = bool(replacement.get('flipped', False)) ^ bool(flipped)
      variants[target][level] = replacement
  # Reserve the sparse records until deduplication has assigned header references.
  variant_size = sum(4 + 2 * len(levels) for levels in variants.values())
  output.write(struct.pack('<I', variant_size))
  variant_offset = output.tell()
  output.write(bytes(variant_size))
  output.write(bytes((GRAPHICS_WATER_COLORS,)))
  for r, g, b in WATER_COLORS:
    output.write(struct.pack('<I', r << 16 | g << 8 | b))
  bridge = []
  for filename in banks:
    with Image.open(catalog.parent / filename) as source:
      if source.size != (256, 1) or any(a != 255 for *_, a in source.convert('RGBA').getdata()):
        raise ValueError('Expected a 256-entry opaque bridge palette')
      bridge.append(Palette(source.convert('RGB').tobytes()))
  WriteGraphicsCommand(output, GRAPHICS_BRIDGE_PALETTES, *bridge)

  def ImageRecord(entry):
    filename = catalog.parent / entry['file'] if 'file' in entry else None
    key = (str(filename.resolve()) if filename else None, entry.get('crop_top'), entry.get('overlay'),
        entry.get('alpha_mask'), entry.get('animation_mask'), entry.get('palette'), entry.get('encoding'),
        not entry.get('civic', False), entry.get('global_palette', False), entry.get('height_size'),
        entry.get('bake_ground'), entry.get('ground_coverage'), entry.get('tile_half'),
        entry['scale_divisor'], entry['baseline'])
    if filename and key in files:
      return files[key]
    image = entry.get('image')
    if image is None:
      with Image.open(filename) as source:
        image = source.copy() if source.mode == 'P' else source.convert('RGBA')
    if 'tile_half' in entry:
      half = entry['tile_half']
      if image.size != (128, 64) or half not in range(4):
        raise ValueError('Straight halves require a 128x64 source and a half index from zero to three')
      image = image.convert('RGBA')
      # Match the curved pieces' binary pixel-centre ownership. Transparent
      # margins become ordinary omitted runs; the renderer needs no half mode.
      for row in range(64):
        cut = 127 - 2 * row if half // 2 else 2 * row + 1
        cut = max(0, min(128, cut))
        left, right = (cut, 128) if half in (1, 2) else (0, cut)
        image.paste((0, 0, 0, 0), (left, row, right, row + 1))
    if 'alpha_mask' in entry:
      image = image.convert('RGBA')
      with Image.open(entry['alpha_mask']) as mask:
        image.putalpha(ImageChops.multiply(image.getchannel('A'), mask.convert('L')))
    if 'overlay' in entry:
      with Image.open(entry['overlay']) as overlay:
        image = CleanCivicAlpha(Image.alpha_composite(image.convert('RGBA'), overlay.convert('RGBA')))
    if 'bake_ground' in entry:
      # Repeat the same material tiles at the exact native footprint anchors.
      # Source foreground paintings remain separate and editable on disk.
      size = entry['height_size']
      ground = Image.new('RGBA', image.size)
      with Image.open(BUILDING_DIRECTORY / 'ground' / (entry['bake_ground'] + '.png')) as source:
        tile = source.convert('RGBA')
      for u in range(size):
        for v in range(size):
          ground.alpha_composite(tile, (image.width // 2 + 64 * (u - v) - 64,
              image.height - 64 * size + 32 * (u + v) - 64))
      image = Image.alpha_composite(ground, image.convert('RGBA'))
    if 'civic_kind' in entry:
      image = CleanCivicAlpha(image).crop((0, entry['crop_top'], image.width, image.height))
    height_size = entry.get('height_size', 0)
    height = MeasureBuildingHeight(image, entry['baseline'], height_size, entry['scale_divisor']) if height_size else 0
    covered = False
    if 'civic_kind' in entry or entry.get('ground_coverage'):
      with Image.open(ROOT / 'assets/landscape/opengfx2/detail128/terrain-masks.png') as source:
        tile = source.convert('RGBA').crop((0, 0, 128, 128)).getchannel('A')
      mask = Image.new('L', image.size)
      for u in range(height_size):
        for v in range(height_size):
          layer = Image.new('L', image.size)
          layer.paste(tile, (image.width // 2 + 64 * (u - v) - 64,
              entry['baseline'] - 64 * height_size + 32 * (u + v) - 64))
          mask = ImageChops.lighter(mask, layer)
      covered = not ImageChops.subtract(mask, image.getchannel('A')).getbbox()
      if (entry.get('bake_ground') or entry.get('ground_coverage')) and not covered:
        raise ValueError(f'Baked building ground does not cover its footprint: {filename}')
    animation = None
    if 'animation_mask' in entry:
      with Image.open(catalog.parent / entry['animation_mask']) as source:
        animation = source.copy()
      if 'crop_top' in entry:
        animation = animation.crop((0, entry['crop_top'], animation.width, animation.height))
    palette_id, mapping = 0, None
    if 'palette' in entry:
      with Image.open(catalog.parent / entry['palette']) as source:
        if source.size != (256, 1): raise ValueError('Expected 256 source palette entries')
        colors = source.convert('RGB')
        mapping = {color: index for index, color in enumerate(colors.getdata())}
        if image.mode == 'P': mapping = None
        elif len(mapping) != 256: raise ValueError('Source palette indices must have unique RGB colours')
        palette_id = Palette(colors.tobytes())
    elif image.mode == 'P':
      if not entry.get('global_palette', False):
        palette_id = Palette(bytes(image.getpalette()[:768]))
    elif entry.get('encoding') == 'indexed':
      alpha = image.getchannel('A')
      indexed = image.convert('RGB').quantize(colors=256, method=Image.Quantize.MEDIANCUT,
          dither=Image.Dither.NONE)
      colors = indexed.getpalette()[:768]
      palette_id = Palette(bytes(colors))
      image = indexed.convert('RGBA')
      image.putalpha(alpha)
      mapping = {tuple(colors[i:i + 3]): i // 3 for i in range(0, len(colors), 3)}
    if (entry.get('opaque') or entry['id'] == 5) and set(image.getchannel('A').getdata()) - {0, 255}:
      raise ValueError(f'Opaque tile {entry["id"]} contains translucent pixels')
    payload = encode(image, palette=mapping, animation_mask=animation,
        water_animation=not entry.get('civic', False))
    digest = (image.width, image.height, hashlib.sha256(payload).digest())
    if digest not in images:
      image_id = len(images)
      images[digest] = (image_id, palette_id)
      WriteGraphicsCommand(output, GRAPHICS_IMAGE, image_id, image.width, image.height, palette_id, len(payload))
      output.write(payload)
    image_id, default_palette = images[digest]
    result = (image_id, palette_id, default_palette, height_size, height, covered)
    if filename: files[key] = result
    return result

  resolved = {}
  by_id = {entry['id']: entry for entry in entries}
  if len(by_id) != len(entries): raise ValueError('Duplicate graphics IDs')

  def Resolve(ident, visiting):
    if ident in resolved: return resolved[ident]
    if ident not in by_id or ident in visiting: raise ValueError('Missing or cyclic sprite reference')
    entry = by_id[ident]
    if 'ref' in entry:
      base = dict(Resolve(entry['ref'], visiting | {ident}))
      if 'palette' in entry: base['recolor_palette'] = entry['palette']
      base.update((k, v) for k, v in entry.items() if k not in ('ref', 'mirror', 'palette'))
      base['mirror'] = bool(base.get('mirror', False)) ^ bool(entry.get('mirror', False))
      entry = base
    resolved[ident] = entry
    return entry

  for target, levels in variants.items():
    source = references[target - first_group][0] if target >= first_group else target
    base = Resolve(source, set())
    if not base.get('height_size'): continue
    for replacement in levels.values():
      index = replacement if isinstance(replacement, int) else replacement['source']
      variant = Resolve(index, set())
      if variant.get('height_size', base['height_size']) != base['height_size']:
        raise ValueError('A resolution variant changes the building footprint')
      variant['height_size'] = base['height_size']

  for ident in sorted(by_id):
    entry = Resolve(ident, set())
    image_id, palette_id, default_palette, height_size, height, covered = ImageRecord(entry)
    if covered: baked_ground.add(ident)
    if 'recolor_palette' in entry:
      with Image.open(catalog.parent / entry['recolor_palette']) as colors:
        if colors.size != (256, 1): raise ValueError('Expected 256 recolour palette entries')
        palette_id = Palette(colors.convert('RGB').tobytes())
    mirrored = bool(entry.get('mirror', False))
    if height_size: measured[ident] = (height_size, height)
    binding = (image_id, palette_id, entry['scale_divisor'], entry['baseline'], entry.get('offset_x', 0))
    stats['assets'] += 1
    stats['mirrored'] += mirrored
    if binding in bindings:
      lookup[ident] = bindings[binding] | (mirrored << 15)
      stats['references'] += 1
      continue
    header = ident + 1
    bindings[binding] = header
    lookup[ident] = header | (mirrored << 15)
    offset = entry.get('offset_x', 0)
    WriteGraphicsCommand(output, GRAPHICS_SPRITE, header, image_id, entry['scale_divisor'], entry['baseline'],
        (offset << 1) ^ (offset >> 31))
    flags = 2 if palette_id != default_palette else 0
    output.write(bytes((flags,)))
    if flags: WriteGraphicsInteger(output, palette_id)
  for index, (source, flipped, _) in enumerate(references):
    lookup[first_group + index] = lookup[source] ^ (flipped << 15)
  for target, levels in variants.items():
    source = references[target - first_group][0] if target >= first_group else target
    if source not in measured: continue
    size, height = measured[source]
    for replacement in levels.values():
      index = replacement if isinstance(replacement, int) else replacement['source']
      other_size, other_height = measured[index]
      if size != other_size: raise ValueError('Resolution variant footprint mismatch')
      height = max(height, other_height)
    measured[source] = size, height
  output.write(bytes((GRAPHICS_END,)))
  end = output.tell()
  output.seek(lookup_offset)
  output.write(struct.pack(f'<{len(lookup)}H', *lookup))
  output.seek(variant_offset)
  output.write(EncodeSpriteVariants(variants, lookup))
  output.seek(end)
  stats.update(images=len(images), palettes=len(palettes), headers=len(bindings),
      indexes=len(lookup), references=stats['references'] + len(references))
  return stats


def ApplyGraphicsBindings(entries, path):
  """Compile finite ID/range/masked-ID rules; drawing never evaluates these patterns."""
  by_id = {entry['id']: entry for entry in entries}
  spec = json.loads(path.read_text())
  rules = spec if isinstance(spec, list) else spec['bindings']
  for rule in rules:
    if 'id' in rule:
      ids = [rule['id']]
    elif 'ids' in rule:
      ids = rule['ids']
    else:
      match = rule['match']
      ids = [ident for ident in by_id if match.get('first', 0) <= ident <= match.get('last', 0xFFFFFFFF) and
          (ident & match.get('mask', 0)) == match.get('value', 0)]
    if not ids: raise ValueError('Graphics binding matches no IDs')
    for ident in ids:
      if ident not in by_id: raise ValueError(f'Unknown graphics binding ID: {ident}')
      source = {key: value for key, value in rule.items() if key not in ('id', 'ids', 'match')}
      if 'ref' not in source:
        source = dict(by_id[ident], **source)
      for key in ('file', 'palette', 'animation_mask', 'overlay', 'alpha_mask'):
        if key in rule: source[key] = str((path.parent / source[key]).resolve())
      source['id'] = ident
      by_id[ident] = source
  return list(by_id.values())


def PackSprites(catalog, destination, bindings=None):
  entries = json.loads(catalog.read_text())
  world = ROOT / 'assets/landscape/world/catalog.json'
  overrides = json.loads(world.read_text())
  replaced = {entry['id'] for entry in overrides}
  entries = [entry for entry in entries if entry['id'] not in replaced] + overrides
  # Every compatibility slot must have native art, an explicit retired blank,
  # or a complete building set resolved by CityMapCatalogSprite at load time.
  kinds = {entry['id'] for entry in entries}
  missing = set(range(500)) - kinds - {SMALL_PARK_KIND} - set(range(BUILDING_KIND_FIRST, 256))
  if missing:
    raise ValueError(f'Missing native catalog artwork: {sorted(missing)}')
  first_civic = max(entry['id'] for entry in entries) + 1
  entries += ArtworkEntries(first_civic)
  crop_tops = {entry['civic_kind']: entry['crop_top'] for entry in entries if 'civic_kind' in entry}
  smoke, emitters = PowerSmokeAssets(first_civic + (CIVIC_KIND_COUNT + 1) * 4)
  entries += smoke
  wind = WindPowerAssets(first_civic + (CIVIC_KIND_COUNT + 1) * 4 + len(smoke))
  entries += wind
  first_steam = wind[-1]['id'] + 1
  for view in range(4):
    for emitter in range(2):
      nuclear = CIVIC_DIRECTORY / '203-nuclear-power-plant'
      manifest = json.loads((nuclear / 'sprites.json').read_text())
      mask = nuclear / (manifest['steam_masks'][emitter] if 'steam_masks' in manifest else
          f'steam-mask-{view}-{emitter}.png')
      with Image.open(mask) as source:
        if source.mode != 'L' or source.size != (128, 128):
          raise ValueError(f'Invalid cooling-tower steam mask: {mask}')
      for frame in range(POWER_SMOKE_FRAMES):
        entries.append(dict(id=first_steam + (view * 2 + emitter) * POWER_SMOKE_FRAMES + frame,
            file=str(POWER_SMOKE_DIRECTORY / f'steam-{frame:02d}.png'), alpha_mask=str(mask),
            baseline=110, scale_divisor=4, offset_x=0, civic=True))
  first_building = first_steam + 4 * 2 * POWER_SMOKE_FRAMES
  entries += ArtworkEntries(first_building, BUILDING_DIRECTORY)
  entries += ConstructionEntries(first_building + BUILDING_KIND_COUNT * 4)
  first_ground = first_building + BUILDING_KIND_COUNT * 4 + 3 * CONSTRUCTION_PIECES
  for material,name in enumerate(GROUND_MATERIALS[1:]):
    entries.append(dict(id=first_ground+material,file=str(BUILDING_DIRECTORY/'ground'/f'{name}.png'),
        baseline=128,scale_divisor=4,offset_x=0,civic=True))
  first_hydro_water = first_ground + len(GROUND_MATERIALS) - 1
  for shape in range(1, 5):
    entries.append(dict(id=first_hydro_water + shape - 1,
        file=str(CIVIC_DIRECTORY / f'198-hydro-power/water-slope-{shape}.png'),
        baseline=128, scale_divisor=4, offset_x=0))
  first_road_paving = first_hydro_water + 4
  for shape in range(14):
    entries.append(dict(id=first_road_paving + shape,
        file=str(BUILDING_DIRECTORY / f'ground/road-stone-{shape}.png'),
        baseline=128, scale_divisor=4, offset_x=0, civic=True))
  vehicles = ROOT / 'assets/landscape/vehicles'
  vehicle_manifest = json.loads((vehicles / 'blender/manifest.json').read_text())
  if vehicle_manifest['canvas'] != 64 or vehicle_manifest['ground_anchor'] != [32, 48]:
    raise ValueError('Vehicle ground registration differs from the renderer')
  vehicle_count = len(vehicle_manifest['models']) * vehicle_manifest['views'] * len(vehicle_manifest['steering_angles'])
  for slot in range(vehicle_count):
    filename = vehicles / f'car-{slot:04d}.png'
    with Image.open(filename) as source:
      image = source.convert('RGBA')
    bounds = image.getchannel('A').getbbox()
    if not bounds: raise ValueError(f'Empty car sprite: {filename}')
    # Four source pixels are one map pixel. Aligned crops preserve the original
    # sampling grid at every map scale; the existing anchors retain placement.
    left, top = bounds[0] // 4 * 4, bounds[1] // 4 * 4
    right, bottom = (bounds[2] + 3) // 4 * 4, (bounds[3] + 3) // 4 * 4
    entries.append(dict(id=first_road_paving + 14 + slot, file=str(filename),
        image=image.crop((left, top, right, bottom)), palette=str(vehicles / 'palette.png'),
        baseline=48 - top, scale_divisor=4, offset_x=left, civic=True))
  for axis in range(2):
    for merge in range(4):
      entries.append(dict(id=first_road_paving + 14 + vehicle_count + axis * 4 + merge,
          file=str(ROOT / f'assets/landscape/roads/highway-overpass-{"ab"[axis]}-{merge}.png'),
          baseline=128, scale_divisor=4, offset_x=0, civic=True))
  bridge = ROOT / 'assets/landscape/bridges'
  foreground = json.loads((bridge / 'traffic-foreground.json').read_text())
  if len(foreground) != 2 * 7 * 6 * 2 + 32:
    raise ValueError('Expected road/rail front bridge pieces and approaches')
  first_front = entries[-1]['id'] + 1
  for slot, entry in enumerate(foreground):
    entries.append(dict(entry, id=first_front + slot, file=str(bridge / entry['file']),
        palette=str(bridge / 'palette-source.png')))
  first_waste = entries[-1]['id'] + 1
  for shape in range(14):
    entries.append(dict(id=first_waste + shape, file=str(world.parent / f'waste-ground-{shape:02}.png'),
        baseline=128, scale_divisor=4, offset_x=0, civic=True, opaque=True))
  # Four reusable straight halves per network follow the waste artwork.
  # Derive them from the existing source PNGs, without new authoring files.
  originals = {entry['id']: entry for entry in entries if entry['id'] in (29, 30, 44, 45)}
  for base in (29, 44):
    for half in range(4):
      entries.append(dict(originals[base + half // 2], id=entries[-1]['id'] + 1, tile_half=half))
  first_coast_recovery = entries[-1]['id'] + 1
  for stage in range(3):
    for shape in range(14):
      entries.append(dict(id=first_coast_recovery + stage * 14 + shape,
          file=str(catalog.parent / f'sprites/coast-recovery-{stage}-{shape:02}.png'),
          baseline=128, scale_divisor=4, offset_x=0))
  if bindings: entries = ApplyGraphicsBindings(entries, bindings)
  banks = [entry["palettes"] for entry in entries if "palettes" in entry]
  if len(banks) != 1: raise ValueError("Expected one bridge palette bank")
  destination.parent.mkdir(parents=True, exist_ok=True)
  temporary = destination.with_suffix(".tmp")
  try:
    with temporary.open("wb") as output:
      measured, baked_ground = {}, set()
      references, native, tiles, variants = BakeMapGraphics(destination.parent, entries, first_civic, first_building)
      stats = WriteGraphicsSprites(output, entries, catalog, banks[0], measured, baked_ground, references, variants)
      RemapMapGraphicsMetadata(measured, baked_ground, native)
      output.write(b'SC2KSMK1')
      for plant in emitters:
        for view in plant['views']:
          output.write(struct.pack('<I', len(view)))
          for emitter in view:
            output.write(struct.pack('<iiII', emitter['x'], emitter['y'] - crop_tops[plant['kind']],
                emitter['type'], emitter['flipped']))
      buildings = WriteBuildingMetadata(output, baked_ground, first_building)
      WriteCivicGrounds(output, baked_ground, first_civic)
      output.write(b'SC2KCAR1')
      with Image.open(vehicles / 'palettes.png') as colors:
        if colors.size != (256, 32): raise ValueError('Expected 32 vehicle palettes')
        for r, g, b in colors.convert('RGB').getdata():
          output.write(struct.pack('<I', 0xFF000000 | r << 16 | g << 8 | b))
      WriteKindHeightMetadata(output, measured, first_civic, first_building, tiles, buildings)
    temporary.replace(destination)
  finally:
    temporary.unlink(missing_ok=True)
  print(f"Shared graphics: {stats}")
  print(f"Packed {len(entries)} landscape sprites: {destination.stat().st_size:,} bytes: {destination}")


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--catalog", type=Path, default=SPRITE_CATALOG)
  destination = parser.add_mutually_exclusive_group(required=True)
  destination.add_argument("--output", type=Path)
  destination.add_argument("--app", type=Path, help="Replace graphics in an existing macOS app without compiling")
  parser.add_argument("--zstd", action="store_true", help="Compress the complete archive as one Zstandard frame")
  parser.add_argument("--bindings", type=Path, help="Override IDs, ID ranges or masked ID patterns from JSON")
  parser.add_argument("--input-list", type=Path, help="Build-system source dependency list")
  parser.add_argument("--depfile", type=Path, help="Write compiler-style dependencies without large process arguments")
  args = parser.parse_args()
  output = args.output or args.app / 'Contents/Resources/graphics/landscape/opengfx2/landscape.dat'
  PackSprites(args.catalog, output, args.bindings)
  if args.zstd:
    import subprocess
    packed = output.with_suffix('.zst.tmp')
    try:
      subprocess.run(['zstd', '-q', '-f', '--check', '-15', '--zstd=wlog=23', str(output), '-o', str(packed)],
          check=True)
      before = output.stat().st_size
      packed.replace(output)
      print(f'Whole-archive Zstandard: {before:,} -> {output.stat().st_size:,} bytes')
    finally:
      packed.unlink(missing_ok=True)
  if args.depfile:
    inputs = args.input_list.read_text().splitlines() if args.input_list else []
    inputs += [str(args.catalog.resolve()), str(Path(__file__).resolve())]
    def Escape(value):
      return str(value).replace('\\', '\\\\').replace(' ', '\\ ').replace('#', '\\#').replace('$', '$$')
    args.depfile.write_text(Escape(output.resolve()) + ': ' + ' '.join(Escape(path) for path in inputs) + '\n')


if __name__ == "__main__":
  main()
