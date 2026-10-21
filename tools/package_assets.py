#!/usr/bin/env python3
"""Build intro, graphics and audio packs using one indexed Zstandard format."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def AssetCatalog(source_root):
  import re
  text = (source_root / 'src/assets/asset_catalog.inc').read_text()
  entries = re.findall(r'CITY_ASSET\((\w+), (\w+), "([^"]+)"\)', text)
  return {(category.lower(), name): number for number, (_, category, name) in enumerate(entries)}


MAGIC = b'SC2KPAK1'
HEADER = struct.Struct('<8sQQII')
ENTRY = struct.Struct('<IIQQ')
BLOCK = struct.Struct('<QQQ')


def CopyAsset(source, destination):
  destination.parent.mkdir(parents=True, exist_ok=True)
  if destination.exists():
    before, after = source.stat(), destination.stat()
    if before.st_size == after.st_size and before.st_mtime_ns == after.st_mtime_ns:
      return
  shutil.copy2(source, destination)


def AssetFacts(members):
  return [[name, str(path.resolve()), path.stat().st_size, path.stat().st_mtime_ns]
      for name, path in sorted(members.items())]


def CompressBlock(raw, cache, compressor, level):
  digest = hashlib.sha256()
  with raw.open('rb') as source:
    for chunk in iter(lambda: source.read(1024 * 1024), b''):
      digest.update(chunk)
  packed = cache / f'block-{digest.hexdigest()}-{level}.zst'
  if not packed.exists():
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
      temporary = Path(output.name)
      try:
        subprocess.run([compressor, '-q', '--check', f'-{level}', '--zstd=wlog=23', '--stdout', str(raw)],
            stdout=output, check=True)
        output.close()
        temporary.replace(packed)
      finally:
        temporary.unlink(missing_ok=True)
  return packed


def AppendAsset(source, output, compressor):
  with source.open('rb') as file:
    compressed = file.read(4) == b'\x28\xb5\x2f\xfd'
    file.seek(0)
    if not compressed:
      shutil.copyfileobj(file, output)
      return
  # Source packs may already be compressed. Archive entries always contain the
  # decoded source format, rather than nesting compressed frames inside a block.
  output.flush()
  subprocess.run([compressor, '-q', '-d', '--stdout', str(source)], stdout=output, check=True)
  output.seek(0, 2)


def PackageArchive(category, members, groups, destination, cache, compressor, ids, level):
  if not members or len(members) > 4096 or not groups or len(groups) > 4096:
    raise ValueError(f'Invalid {category} archive size')
  assigned = [name for group in groups for name in group]
  if len(assigned) != len(set(assigned)) or set(assigned) != set(members) or any(not group for group in groups):
    raise ValueError(f'{category} block layout must assign every asset exactly once')
  facts = AssetFacts(members)
  configuration = dict(magic=MAGIC.decode(), level=level, inputs=facts, groups=groups, ids=ids)
  state_path = cache / f'{category}.json'
  if state_path.exists():
    previous = json.loads(state_path.read_text())
    if previous['configuration'] == configuration:
      packed = cache / previous['archive']
      if packed.exists():
        CopyAsset(packed, destination)
        print(f'{category}: reused {len(groups)} content blocks, {packed.stat().st_size:,} bytes')
        return
  blocks, entries = [], {}
  for number, names in enumerate(groups):
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
      raw = Path(output.name)
      try:
        for name in names:
          encoded = name.encode('utf-8')
          if not 0 < len(encoded) < 256 or b'\0' in encoded:
            raise ValueError(f'Invalid asset name: {name}')
          start = output.tell()
          AppendAsset(members[name], output, compressor)
          entries[name] = (number, start, output.tell() - start)
        output.close()
        blocks.append((CompressBlock(raw, cache, compressor, level), raw.stat().st_size))
      finally:
        raw.unlink(missing_ok=True)
  index = bytearray()
  for name in sorted(entries, key=ids.get):
    index += ENTRY.pack(ids[name], *entries[name])
  offset = 0
  for packed, size in blocks:
    index += BLOCK.pack(offset, packed.stat().st_size, size)
    offset += packed.stat().st_size
  with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
    raw_index = Path(output.name)
    try:
      output.write(index)
      output.close()
      packed_index = CompressBlock(raw_index, cache, compressor, level)
    finally:
      raw_index.unlink(missing_ok=True)
  digest = hashlib.sha256(index + packed_index.name.encode())
  for packed, _ in blocks:
    digest.update(packed.name.encode())
  archive = cache / f'{category}-{digest.hexdigest()}.sc2assets'
  if not archive.exists():
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
      temporary = Path(output.name)
      try:
        output.write(HEADER.pack(MAGIC, packed_index.stat().st_size, len(index), len(entries), len(blocks)))
        for packed in [packed_index] + [path for path, _ in blocks]:
          with packed.open('rb') as file:
            shutil.copyfileobj(file, output)
        output.close()
        temporary.replace(archive)
      finally:
        temporary.unlink(missing_ok=True)
  if AssetFacts(members) != facts:
    raise RuntimeError(f'{category} inputs changed during packaging; rebuild the assets')
  CopyAsset(archive, destination)
  temporary = state_path.with_suffix('.tmp')
  temporary.write_text(json.dumps(dict(configuration=configuration, archive=archive.name)) + '\n')
  temporary.replace(state_path)
  raw_size = sum(size for _, size in blocks)
  print(f'{category}: {len(entries)} assets, {len(blocks)} content blocks, '
      f'{raw_size:,} -> {archive.stat().st_size:,} bytes')


def AttributionText(value, indent=''):
  if isinstance(value, dict):
    return '\n'.join(f'{indent}{key}:\n{AttributionText(item, indent + "  ")}'
        if isinstance(item, (dict, list)) else f'{indent}{key}: {item}' for key, item in value.items())
  if isinstance(value, list):
    return '\n\n'.join(AttributionText(item, indent) for item in value)
  return indent + str(value)


def WriteLicenses(source_root, destination, sound_metadata, extra_licenses=()):
  # Keep the former destinations only to remove copies left by earlier builds.
  files = {
      'thirdparty/lua/LICENSE': 'Lua-LICENSE.txt',
      'thirdparty/zstd/LICENSE': 'Zstandard-LICENSE.txt',
      'docs/licenses/sc2kfix.txt': 'sc2kfix-LICENSE.txt',
      'src/city/terrain/LICENSE.OpenTTD': 'OpenTTD-terrain-LICENSE.txt',
      'src/city/terrain/LICENSE.OpenSC2K': 'OpenSC2K-terrain-LICENSE.txt',
      'src/city/terrain/LICENSE.FastNoiseLite': 'FastNoiseLite-LICENSE.txt',
      'assets/fonts/noto/LICENSE': 'graphics/fonts/NotoSans-LICENSE.txt',
      'assets/fonts/openttd/LICENSE': 'graphics/fonts/OpenTTD-LICENSE.txt',
      'assets/fonts/openttd/README.md': 'graphics/fonts/OpenTTD-README.md',
      'assets/ui/opengfx2/LICENSE': 'graphics/ui/opengfx2/LICENSE',
      'assets/ui/opengfx2/README.md': 'graphics/ui/opengfx2/README.md',
      'assets/audio/freesound/CREDITS.md': 'graphics/audio/Freesound-CREDITS.md',
      'assets/audio/freesound/sources.json': 'graphics/audio/freesound-sources.json',
      'assets/audio/freesound/LICENSE-CC-BY-4.0.txt': 'graphics/audio/LICENSE-CC-BY-4.0.txt',
  }
  for directory, names, old_root in (
      ('assets/fonts/newspaper',
          ('Anton-LICENSE.txt', 'Chomsky-LICENSE.txt', 'NotoSerif-LICENSE.txt'), 'graphics/fonts'),
      ('assets/landscape/opengfx2', ('LICENSE', 'README.md'), 'graphics/landscape/opengfx2'),
      ('assets/audio', ('README.md',), 'graphics/audio'),
      ('assets/audio/opensfx',
          ('LICENSE', 'LICENSE-CC-BY-SA-3.0.txt', 'LICENSE-CC0-1.0.txt', 'credits.json'), 'graphics/audio'),
      ('assets/ui/bridges', ('README.md',), 'graphics/ui/bridges'),
      ('assets/ui/map-data', ('README.md',), 'graphics/ui/map-data'),
      ('assets/music/openmsx',
          ('LICENSE', 'README.md', 'redfarn_music_grant.txt', 'upstream-readme.md', 'themes.list'), 'openmsx')):
    for name in names: files[f'{directory}/{name}'] = f'{old_root}/{name}'
  sources = [(name, source_root / name) for name in files]
  sources.append(('Sound effect mapping (generated)', sound_metadata))
  sources.extend(extra_licenses)
  sections = ['OpenSC2K third-party licenses and attribution\n\nContents:\n' +
      '\n'.join(f'  {name}' for name, _ in sources)]
  for name, source in sources:
    text = source.read_text(encoding='utf-8')
    if source.suffix == '.json': text = AttributionText(json.loads(text))
    sections.append(f'{"=" * 78}\n{name}\n{"=" * 78}\n\n{text}')
  text = '\n\n'.join(sections) + '\n'
  output = destination / 'licenses.txt'
  if not output.exists() or output.read_text(encoding='utf-8') != text:
    temporary = output.with_suffix('.tmp')
    temporary.write_text(text, encoding='utf-8')
    temporary.replace(output)
  obsolete = [destination / name for name in files.values()]
  obsolete.append(destination / 'graphics/audio/effects.json')
  for path in obsolete: path.unlink(missing_ok=True)
  directories = {parent for path in obsolete for parent in path.parents if parent.is_relative_to(destination)
      and parent != destination}
  for path in sorted(directories, key=lambda path: len(path.parts), reverse=True):
    if path.is_dir() and not any(path.iterdir()): path.rmdir()


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--source-root', type=Path, required=True)
  parser.add_argument('--destination', type=Path, required=True)
  parser.add_argument('--cache', type=Path, required=True)
  parser.add_argument('--zstd', required=True, help='host Zstandard compressor')
  parser.add_argument('--level', type=int, choices=range(1, 20), default=1)
  parser.add_argument('--landscape', type=Path, required=True)
  parser.add_argument('--advisors', type=Path, required=True)
  parser.add_argument('--neighbors', type=Path, required=True)
  parser.add_argument('--sounds', type=Path, required=True)
  parser.add_argument('--sound-override', type=Path)
  parser.add_argument('--music', type=Path, required=True)
  parser.add_argument('--block-layout', type=Path,
      help='JSON category -> lists of member names, one list per content block')
  args = parser.parse_args()
  replacements = {'landscape/opengfx2/landscape.dat': args.landscape,
      'landscape/opengfx2/tile-graphics.dat': args.landscape.parent / 'tile-graphics.dat',
      'ui/advisors/advisors.dat': args.advisors, 'ui/neighbors/neighbors.dat': args.neighbors}
  catalog = AssetCatalog(args.source_root)
  categories = {name: {} for name in ('intro', 'graphics', 'audio')}
  for category, name in catalog:
    if category == 'audio':
      if name == 'sound-effects.dat': source = args.sounds
      elif name == 'sound-effects.override.dat':
        if not args.sound_override: continue
        source = args.sound_override
      else: source = args.music / name
    else: source = replacements.get(name, args.source_root / 'assets' / name)
    categories[category][name] = source
  args.cache.mkdir(parents=True, exist_ok=True)
  args.destination.mkdir(parents=True, exist_ok=True)
  layout = json.loads(args.block_layout.read_text()) if args.block_layout else {}
  if set(layout) - set(categories):
    raise ValueError('Block layout categories must be intro, graphics or audio')
  for category, members in categories.items():
    groups = layout.get(category, [list(members)])
    ids = {name: catalog[category, name] for name in members}
    PackageArchive(category, members, groups, args.destination / f'{category}.sc2assets',
        args.cache, args.zstd, ids, args.level)
  # Remove only known obsolete runtime binaries; preserve credits and licenses.
  for obsolete in ('music.sc2assets', 'sounds.sc2assets', 'Data/native-resources.sc2res'):
    (args.destination / obsolete).unlink(missing_ok=True)
  for category, name in catalog:
    roots = {'intro': 'graphics', 'graphics': 'graphics',
        'audio': 'openmsx' if name.endswith('.mid') else 'graphics/audio'}
    (args.destination / roots[category] / name).unlink(missing_ok=True)
  WriteLicenses(args.source_root, args.destination, args.sounds.with_suffix('.json'))


if __name__ == '__main__':
  main()
