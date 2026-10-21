#!/usr/bin/env python3
"""Prepare generated building paintings for the shared native foundation baker, offline only."""
import argparse
import hashlib
import json
import math
import os
import shutil
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

import numpy as np

from bake_civic_sprites import Bake, BuildingRoofTop, Foreground, Ground, GroundCorners, NativeFoundation

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/landscape/buildings'
CONSTRUCTION_KINDS = (136, 137, 166, 167, 168, 169, 194, 195)
HEIGHT_REFERENCES = DIRECTORY / 'height-references.json'


def AuditHeights(output):
  """Audit actual baked alpha against stock scale; canvas padding never counts as building height."""
  references = {row['kind']: row for row in json.loads(HEIGHT_REFERENCES.read_text())['buildings']}
  rows = []
  for directory in (DIRECTORY, ROOT/'assets/landscape/civic'):
    for path in directory.glob('*/sprites.json'):
      spec = json.loads(path.read_text())
      reference = references[spec['kind']]
      target = reference.get('target_roof_height', reference['target_height'])
      heights, roof_heights = [], []
      for view in spec['views']:
        with Image.open(path.parent/view['file']) as image:
          alpha = image.convert('RGBA').getchannel('A').point(lambda value: 255 if value >= 128 else 0)
          heights.append(spec['baseline']-alpha.getbbox()[1])
          roof_heights.append(spec['baseline']-BuildingRoofTop(image, image.width//16))
      rows.append(dict(kind=spec['kind'], name=spec['name'], target_height=target, heights=heights,
          roof_heights=roof_heights, maximum_height=math.ceil(target*1.1)+2,
          oversized=max(roof_heights)>math.ceil(target*1.1)+2,
          shared_construction=spec.get('status')=='shared construction artwork'))
  rows.sort(key=lambda row: row['kind'])
  output.parent.mkdir(parents=True, exist_ok=True)
  output.write_text(json.dumps(dict(pixels_per_tile=128, buildings=rows), indent=2)+'\n')
  for name, low, high in (('ordinary',112,197),('civic',198,255)):
    selected = [row for row in rows if low <= row['kind'] <= high and not row['shared_construction']]
    print(f'{name}: {len(selected)} kinds, {sum(row["oversized"] for row in selected)} exceed stock height by >10%')


def ComponentPanels(source):
  """Separate four disconnected sites when their rectangular bounds overlap."""
  solid = np.asarray(source.getchannel('A')) >= 128
  labels = Image.fromarray(np.where(solid, 255, 0).astype('uint8')).copy()
  for view in range(4):
    remaining = np.asarray(labels) == 255
    yy, xx = np.nonzero(remaining)
    cx = source.width * (.25 + .5 * (view % 2))
    cy = source.height * (.3 + .5 * (view // 2))
    nearest = np.argmin((xx-cx)**2 + (yy-cy)**2)
    ImageDraw.floodfill(labels, (int(xx[nearest]), int(yy[nearest])), view+1)
  pixels = np.asarray(labels).copy()
  if any(np.count_nonzero(pixels == view+1) < solid.sum()*.1 for view in range(4)) or \
      np.count_nonzero(pixels == 255) > solid.sum()*.005:
    raise ValueError('Four separate complete sites cannot be identified')
  # Tiny disconnected details belong to their quadrant; never discard opaque source pixels.
  yy,xx = np.nonzero(pixels == 255)
  pixels[yy,xx] = 1 + (xx >= source.width/2) + 2*(yy >= source.height/2)
  panels, rectangles, masks = [], [], []
  for view in range(4):
    mask = Image.fromarray(np.where(pixels == view+1,255,0).astype('uint8')).filter(ImageFilter.MaxFilter(3))
    rectangle = mask.getbbox()
    panel = source.crop(rectangle)
    local_mask = mask.crop(rectangle)
    panel.putalpha(ImageChops.multiply(panel.getchannel('A'),local_mask))
    panels.append(panel)
    rectangles.append(rectangle)
    masks.append(local_mask)
  return panels, rectangles, masks


def ExtractTurnaround(directory):
  """Split a generated four-view resource sheet at its genuinely transparent panel gutters."""
  with Image.open(directory / 'turnaround.png') as source:
    source = source.convert('RGBA')
    solid = np.asarray(source.getchannel('A')) >= 128
    splits, masks = [], []
    for axis, length in ((0, source.width), (1, source.height)):
      counts = solid.sum(axis=axis)
      candidates = np.flatnonzero(counts == 0)
      candidates = candidates[(candidates > length * .35) & (candidates < length * .65)]
      if not len(candidates):
        break
      splits.append(int(min(candidates, key=lambda x: abs(x-length/2))))
    if len(splits) == 2:
      x,y = splits
      rectangles = [(0,0,x,y),(x,0,source.width,y),(0,y,x,source.height),(x,y,source.width,source.height)]
      crops = [source.crop(rectangle) for rectangle in rectangles]
    else:
      crops, rectangles, masks = ComponentPanels(source)
    panels = []
    for panel in crops:
      bounds = panel.getchannel('A').point(lambda a: 255 if a >= 192 else 0).getbbox()
      if not bounds or bounds[1] == 0:
        raise ValueError(f'Empty or cropped roof in turnaround: {directory}')
      padded = Image.new('RGBA',(panel.width+64,panel.height+64))
      padded.alpha_composite(panel,(32,32))
      panels.append(padded)
    reference = directory/'design-reference.png'
    if not reference.exists(): shutil.copyfile(directory/'painted-0.png',reference)
    drafts = directory/'drafts'
    drafts.mkdir(exist_ok=True)
    for view,panel in enumerate(panels):
      old = directory/f'painted-{view}.png'
      rejected = drafts/f'unguided-{view}.png'
      if old.exists() and not rejected.exists(): shutil.copyfile(old,rejected)
      panel.save(old)
      if masks: masks[view].save(directory/f'source-panel-mask-{view}.png')
  (directory/'source-panels.json').write_text(json.dumps(dict(sheet='turnaround.png',
      panel_rectangles=rectangles,padding=32,angles=[0,90,180,270],
      component_masks=bool(masks)),indent=2)+'\n')


def InstallGuidedArtwork(study):
  """Register transparent ImageGen paintings to fixed guides and install only after all four bakes pass.

  This is uniform offline bitmap registration, not runtime geometry or shading. Pool/coping/path pixels
  remain part of the foreground; no colour-key ground removal is applied to the generated paintings.
  """
  spec = json.loads((study/'sprites.json').read_text())
  kind = spec['kind']
  target = next(path.parent for path in DIRECTORY.glob('*/sprites.json')
      if json.loads(path.read_text())['kind'] == kind)
  stage = ROOT/'build/artwork-studies/guided-bake'/target.name
  stage.mkdir(parents=True, exist_ok=True)
  backup = ROOT/'build/artwork-studies/guided-before'/target.name
  backup.mkdir(parents=True, exist_ok=True)
  config = json.loads((target/'bake.json').read_text())
  config.update(views=[],omit_ground_in_sprite=True,retain_painted_ground=False,
      reject_opaque_trim=True,edge_registration_pixels=0,align_building_height=False,content_inset_pixels=2)
  config.pop('target_roof_height',None)
  config.pop('structure_registration',None)
  options_path = study/'bake-options.json'
  options = json.loads(options_path.read_text()) if options_path.exists() else {}
  config['artwork_scale'] = options.get('artwork_scale',1)
  overrides_path = study/'view-overrides.json'
  overrides = json.loads(overrides_path.read_text()) if overrides_path.exists() else {}
  registration = []
  for view in range(4):
    source_name = overrides.get(str(view),f'painted-{view}.png')
    source = Image.open(study/source_name).convert('RGBA')
    guide = Image.open(study/f'layout-guide-{view}.png').convert('RGBA')
    pixels = np.asarray(guide)
    structure = (pixels[:,:,3]>=128) & np.any(pixels[:,:,:3] != (147,145,110),axis=2)
    yy,xx = np.nonzero(structure)
    bounds = [int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)]
    source_bounds = source.getchannel('A').point(lambda value:255 if value>=128 else 0).getbbox()
    if not source_bounds:
      raise ValueError(f'{kind} view {view}: generated artwork is empty')
    left,top,right,bottom = source_bounds
    scale = min((bounds[2]-bounds[0])/(right-left),(bounds[3]-bounds[1])/(bottom-top))
    dx = (bounds[0]+bounds[2]-(left+right)*scale)/2
    dy = bounds[3]-bottom*scale
    aligned = source.convert('RGBa').transform(guide.size,Image.Transform.AFFINE,
        (1/scale,0,-dx/scale,0,1/scale,-dy/scale),Image.Resampling.BICUBIC).convert('RGBA')
    foreground = f'cutout-{view}.png'
    original = f'registration-guide-{view}.png'
    aligned.save(stage/foreground)
    guide.save(stage/original)
    config['views'].append(dict(original=original,foreground=foreground,plinth_pixels=0))
    registration.append(dict(view=view,source=source_name,source_bounds=source_bounds,guide_structure_bounds=bounds,
        uniform_scale=scale,translation=[dx,dy]))
  # The stage uses absolute immutable resource paths; the installed recipe keeps project-relative paths.
  staged_config = dict(config)
  for field in ('mask','terrain_masks','paving'):
    staged_config[field] = str((target/config[field]).resolve())
  (stage/'bake.json').write_text(json.dumps(staged_config,indent=2)+'\n')
  Bake(stage)
  names = ['bake.json','bake-result.json','sprites.json','final-rotations-preview.png']
  names += [f'{stem}-{view}.png' for view in range(4)
      for stem in ('registration-guide','cutout','foreground','ground','view','painted')]
  for name in names:
    if (target/name).exists() and not (backup/name).exists():
      shutil.copyfile(target/name,backup/name)
  for name in names:
    if (stage/name).exists() and name != 'bake.json':
      shutil.copyfile(stage/name,target/name)
  for view in range(4):
    shutil.copyfile(study/overrides.get(str(view),f'painted-{view}.png'),target/f'painted-{view}.png')
  (target/'bake.json').write_text(json.dumps(config,indent=2)+'\n')
  manifest = json.loads((target/'sprites.json').read_text())
  manifest.update(artwork_note='Baked ImageGen artwork; four views; separate reusable ground.',
      source_notes=os.path.relpath(study/'prompt.txt',target))
  (target/'sprites.json').write_text(json.dumps(manifest,indent=2)+'\n')
  (study/'bake-registration.json').write_text(json.dumps(registration,indent=2)+'\n')
  shutil.copyfile(stage/'final-rotations-preview.png',study/'baked-preview.png')
  print(f'{kind}: installed four views after strict foundation/opaque-overflow validation')


def ExtractForeground(directory):
  """Use the original sheet's registration, rather than recalibrating the extracted building."""
  registration = json.loads((directory/'source-panels.json').read_text())
  with Image.open(directory/'turnaround.png') as original, \
      Image.open(directory/'foreground-turnaround.png') as source:
    source_size = source.size
    if original.size != source.size:
      scale_x,scale_y = source.width/original.width,source.height/original.height
      reduced_canvas = scale_x<1 and scale_y<1 and abs(scale_x/scale_y-1)<.002
      if not reduced_canvas and any(abs(a-b)>4 for a,b in zip(original.size,source.size)):
        raise ValueError(f'Foreground extraction changed canvas dimensions: {directory}')
      # ImageGen can uniformly reduce a large canvas or change a few encoding pixels.
      # Restore the original coordinate system, then verify each building's silhouette below.
      source = source.convert('RGBa').resize(original.size,Image.Resampling.LANCZOS).convert('RGBA')
    checks = []
    for view,rectangle in enumerate(registration['panel_rectangles']):
      panel = source.convert('RGBA').crop(rectangle)
      if registration.get('component_masks'):
        with Image.open(directory/f'source-panel-mask-{view}.png') as mask:
          panel.putalpha(ImageChops.multiply(panel.getchannel('A'),mask))
      padding = registration['padding']
      padded = Image.new('RGBA',(panel.width+2*padding,panel.height+2*padding))
      padded.alpha_composite(panel,(padding,padding))
      with Image.open(directory/f'painted-{view}.png') as reference:
        corners,_ = GroundCorners(reference,0)
        rear = round(corners[0][1]+corners[2][1]-corners[1][1])
        expected = reference.getchannel('A').point(lambda a: 255 if a>=192 else 0).crop((0,0,reference.width,rear))
        actual = padded.getchannel('A').point(lambda a: 255 if a>=192 else 0).crop((0,0,padded.width,rear))
        def Placement(actual):
          a,b = np.asarray(expected)!=0,np.asarray(actual)!=0
          coverage = np.count_nonzero(a & (np.asarray(actual.filter(ImageFilter.MaxFilter(5)))!=0))/a.sum()
          excess = np.count_nonzero(b & (np.asarray(expected.filter(ImageFilter.MaxFilter(5)))==0))/b.sum()
          return float(coverage),float(excess)
        coverage,excess = Placement(actual)
        alignment = None
        if coverage<.97 or excess>.03:
          a,b = expected.getbbox(),actual.getbbox()
          scale_x = (a[2]-a[0])/(b[2]-b[0])
          offset_x = a[0]-scale_x*b[0]
          offset_y = a[1]-scale_x*b[1]
          if .85<=scale_x<=1.15 and abs(offset_x)<=padded.width*.15 and abs(offset_y)<=padded.height*.15:
            padded = padded.convert('RGBa').transform(padded.size,Image.Transform.AFFINE,
                (1/scale_x,0,-offset_x/scale_x,0,1/scale_x,-offset_y/scale_x),
                Image.Resampling.BICUBIC).convert('RGBA')
            actual = padded.getchannel('A').point(lambda a:255 if a>=192 else 0).crop((0,0,padded.width,rear))
            coverage,excess = Placement(actual)
            alignment = dict(scale=scale_x,offset=[offset_x,offset_y],source_bounds=b,target_bounds=a)
        checks.append(dict(view=view,upper_silhouette_coverage=coverage,upper_silhouette_excess=excess,
            alignment=alignment))
        if coverage<.97 or excess>.03:
          raise ValueError(f'Foreground moved or changed the upper building: {directory}, view {view}: {checks[-1]}')
      padded.save(directory/f'cutout-{view}.png')
  digest = hashlib.sha256((directory/'turnaround.png').read_bytes()).hexdigest()
  (directory/'foreground-panels.json').write_text(json.dumps(dict(source_sheet_sha256=digest,
      source_canvas=list(source_size),registered_canvas=list(original.size),checks=checks),indent=2)+'\n')


def Prepare(directory):
  kind = int(directory.name.split('-', 1)[0])
  catalog = json.loads((DIRECTORY / 'catalog.json').read_text())
  entry = next(row for row in catalog['buildings'] if row['kind'] == kind)
  size = entry['footprint_tiles'][0]
  site_path = directory/'site.json'
  site = json.loads(site_path.read_text()) if site_path.exists() else {}
  cutouts = (directory/'foreground-panels.json').exists()
  if cutouts:
    registration = json.loads((directory/'foreground-panels.json').read_text())
    if registration['source_sheet_sha256'] != hashlib.sha256((directory/'turnaround.png').read_bytes()).hexdigest():
      raise ValueError(f'Foreground extraction belongs to an earlier painting: {directory}')
  width = size * 128
  views, heights = [], []
  for view in range(4):
    source = directory / f'painted-{view}.png'
    if not source.exists():
      break
    with Image.open(source) as image:
      image = image.convert('RGBA')
      corners, slopes = GroundCorners(image, 0)
      scale_y = width / (corners[2][0] - corners[0][0]) / (slopes[0] - slopes[1])
      # Alpha at the highest occupied pixel includes genuine roof detail; faint background halos do not set scale.
      solid = image.getchannel('A').point(lambda alpha: 255 if alpha >= 192 else 0)
      bounds = solid.getbbox()
      heights.append(math.ceil((corners[1][1] - bounds[1]) * scale_y) + 24)
    view_spec = dict(original=source.name, plinth_pixels=0)
    if cutouts: view_spec['foreground'] = f'cutout-{view}.png'
    views.append(view_spec)
  if not views:
    raise ValueError(f'No paintings in {directory}')
  shared = kind in CONSTRUCTION_KINDS
  if shared:
    views = views[:1] * 4
  height = max(width // 2 + 32, math.ceil((max(heights)+site.get('height_padding',0)) / 16) * 16)
  if height > 1024:
    raise ValueError(f'Building exceeds the native sprite height limit: {directory}')
  config = dict(kind=kind, footprint_tiles=[size, size], mask='native-foundation-mask.png',
      terrain_masks='../../opengfx2/detail128/terrain-masks.png',
      paving='../../civic/211-fire-station/paving-texture.png', views=views,
      retain_painted_ground=not cutouts, omit_ground_in_sprite=cutouts,
      corner_fill_color=[139, 133, 109], content_inset_pixels=site.get('content_inset_pixels',2),
      reject_opaque_trim=cutouts,edge_registration_pixels=2 if cutouts else 0)
  config['align_building_height'] = not shared and heights[0] - 24 > width
  mask = NativeFoundation(config, directory, (width, height))
  mask.save(directory / config['mask'])
  (directory / 'bake.json').write_text(json.dumps(config, indent=2) + '\n')
  Bake(directory)
  family = 0 if kind < 124 else 1
  manifest = dict(kind=kind, name=entry['tile_info_description'], footprint_tiles=[size, size],
      pixels_per_tile=128, canvas=[width, height], baseline=height,
      views=[dict(angle=view * 90, file=f'view-{view}.png') for view in range(len(views))],
      baker='tools/bake_building_sprites.py', config='bake.json', verification='bake-result.json',
      construction=dict(family=family),
      ground=site.get('ground','terrain') if cutouts else 'painted',
      status='shared construction artwork' if shared else
          'complete four-view artwork' if len(views) == 4 and (directory/'source-panels.json').exists() else
          'master view; rotations pending')
  temporary = directory / '.sprites.json.tmp'
  temporary.write_text(json.dumps(manifest, indent=2) + '\n')
  temporary.replace(directory / 'sprites.json')
  print(f'{kind}: baked {len(views)} views, {width}x{height}, exact native foundation')


def PrepareConstruction():
  directory = DIRECTORY / 'construction'
  sites = []
  for size in range(1, 4):
    width, height = size * 128, size * 64 + 64
    config = dict(footprint_tiles=[size, size],
        terrain_masks='../../opengfx2/detail128/terrain-masks.png')
    mask = NativeFoundation(config, directory, (width, height))
    owned = np.asarray(mask) != 0
    last_row = np.where(owned, np.arange(height)[:, None], -1).max(axis=0)
    prism = Image.fromarray(np.where(np.arange(height)[:, None] <= last_row, 255, 0).astype('uint8'))
    site = dict(footprint=size, foundation=f'foundation-{size}.png', foundation_baseline=height)
    for name in ('foundation', 'timber', 'steel'):
      with Image.open(directory / f'source-{name}-{size}.png') as original:
        original = original.convert('RGBA')
        corners, slopes = GroundCorners(original, 0)
        image = Foreground(original, corners, slopes, (width, height), height, 2)
      image.putalpha(ImageChops.multiply(image.getchannel('A'), prism))
      if name == 'foundation':
        ground = Image.new('RGBA', mask.size, (127, 102, 71, 255))
        ground.putalpha(mask)
        image = Image.alpha_composite(ground, image)
        filename = site['foundation']
      else:
        bounds = image.getchannel('A').point(lambda a: 255 if a >= 192 else 0).getbbox()
        # The rear ground corner is half a plot above the front one. Above it is one storey.
        step = max(8, min(64, height - width // 2 - bounds[1]))
        site[name + '_step'] = step
        filename = f'frame-{name}-{size}.png'
        site[name] = filename
      pixels = np.asarray(image).copy()
      pixels[pixels[:, :, 3] == 0] = 0
      Image.fromarray(pixels).save(directory / filename)
    mask.save(directory / f'foundation-mask-{size}.png')
    sites.append(site)
  (directory / 'kit.json').write_text(json.dumps(dict(pixels_per_tile=128, sites=sites), indent=2) + '\n')
  print('Baked three foundations and six repeatable structural floors')


def PrepareGrounds():
  directory = DIRECTORY/'ground'
  config = dict(footprint_tiles=[1,1],terrain_masks='../../opengfx2/detail128/terrain-masks.png')
  mask = NativeFoundation(config,directory,(128,128))
  textures = dict(stone=ROOT/'assets/landscape/civic/211-fire-station/paving-texture.png',
      concrete=directory/'concrete-material.png')
  textures.update({name:directory/f'{name}-material.png' for name in ('asphalt','gravel','brick','earth')})
  for material,path in textures.items():
    with Image.open(path) as texture:
      Ground(texture,mask,0).save(directory/f'{material}.png')
  print('Baked six shared ground materials, 128 pixels wide')


def WriteArtworkIndex():
  rows = []
  for path in sorted(DIRECTORY.glob('*/sprites.json')):
    spec = json.loads(path.read_text())
    rows.append(dict(kind=spec['kind'], name=spec['name'], footprint_tiles=spec['footprint_tiles'],
        status=spec['status'], manifest=str(path.relative_to(DIRECTORY)),
        source_paintings=len(list(path.parent.glob('painted-[0-3].png'))),
        baked_views=len(spec['views'])))
  temporary = DIRECTORY / f'.artwork-{os.getpid()}.tmp'
  temporary.write_text(json.dumps(dict(pixels_per_tile=128, buildings=rows), indent=2) + '\n')
  temporary.replace(DIRECTORY / 'artwork.json')


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('directories', type=Path, nargs='*')
  parser.add_argument('--construction', action='store_true')
  parser.add_argument('--grounds', action='store_true')
  parser.add_argument('--split-turnaround', action='store_true')
  parser.add_argument('--split-foreground', action='store_true')
  parser.add_argument('--install-guided', action='store_true')
  parser.add_argument('--audit-heights', type=Path)
  args = parser.parse_args()
  if args.audit_heights:
    AuditHeights(args.audit_heights)
  if args.construction:
    PrepareConstruction()
  if args.grounds:
    PrepareGrounds()
  if not args.directories and not args.construction and not args.grounds and \
      not args.audit_heights:
    parser.error('Supply building directories or --construction')
  for directory in args.directories:
    if args.install_guided:
      ExtractTurnaround(directory.resolve())
      InstallGuidedArtwork(directory.resolve())
      continue
    if args.split_turnaround:
      ExtractTurnaround(directory.resolve())
    if args.split_foreground:
      ExtractForeground(directory.resolve())
    Prepare(directory.resolve())
  if args.directories and not args.install_guided or args.construction or args.grounds:
    WriteArtworkIndex()


if __name__ == '__main__':
  main()
