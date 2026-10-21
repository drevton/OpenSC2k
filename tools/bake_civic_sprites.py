#!/usr/bin/env python3
"""Bake Imagegen paintings onto an authoritative civic foundation mask.

Artwork and optional foreground extraction come from Imagegen. This offline resource step
normalizes its ground coordinates, projects the supplied paving, and applies the
same half-open pixel ownership used by terrain. Nothing runs in the game.
corner_fill_color can match unpaved site edges. omit_ground_in_sprite uses an
explicit extracted foreground for native-water sites, while ground-N.png still
retains the authoritative footprint for validation.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

from pack_landscape_assets import CleanCivicAlpha


def NativeFoundation(config, directory, size):
  columns, rows = config['footprint_tiles']
  if columns != rows or size[0] != columns * 128:
    raise ValueError('This baker requires a square footprint at 128 pixels per tile')
  tile = Image.open(directory / config['terrain_masks']).convert('RGBA').crop((0, 0, 128, 128))
  result = Image.new('L', size)
  for u in range(columns):
    for v in range(rows):
      layer = Image.new('L', size)
      layer.paste(tile.getchannel('A'), (size[0] // 2 + 64 * (u - v) - 64,
          size[1] - 64 * columns + 32 * (u + v) - 64))
      result = ImageChops.lighter(result, layer)
  return result


def GroundCorners(image, plinth):
  """Calibrate the two visible front edges of the original, uncut painted site."""
  alpha = np.asarray(image.getchannel('A')) > 192
  _, columns = np.where(alpha)
  left, right = int(columns.min()), int(columns.max())
  bottom = np.where(alpha, np.arange(image.height)[:, None], -1).max(axis=0)
  lines = []
  for start, end in ((.1, .4), (.6, .9)):
    x = np.arange(round(left + (right - left) * start), round(left + (right - left) * end))
    y = bottom[x]
    keep = y >= 0
    for _ in range(4):
      slope, intercept = np.polyfit(x[keep], y[keep], 1)
      residual = y - (slope * x + intercept)
      center = np.median(residual[keep])
      threshold = max(2, 3 * np.median(np.abs(residual[keep] - center)))
      keep = (y >= 0) & (np.abs(residual - center) < threshold)
    lines.append((slope, intercept - plinth))
  a, b = lines[0]
  c, d = lines[1]
  front = (d - b) / (a - c)
  if not left < front < right or not a > 0 > c:
    raise ValueError('Original painted ground does not have two calibratable front edges')
  return [[left, a * left + b], [front, a * front + b], [right, c * right + d]], [a, c]


def ViewGroundCorners(image, entry, foreground=False):
  # Explicit registration keeps architecture parallel to the tile axes when
  # an authored ground plane and its building use slightly different projections.
  corners = entry.get('foreground_ground_corners') if foreground else None
  if corners is None: corners = entry.get('ground_corners')
  if corners is None: return GroundCorners(image, entry['plinth_pixels'])
  left, front, right = corners
  if not left[0] < front[0] < right[0]:
    raise ValueError('Registered ground corners must run left, front, right')
  slopes = [(front[1] - left[1]) / (front[0] - left[0]),
      (right[1] - front[1]) / (right[0] - front[0])]
  if not slopes[0] > 0 > slopes[1]:
    raise ValueError('Registered ground edges must face the front corner')
  return corners, slopes


def Foreground(image, corners, slopes, size, baseline, inset=0):
  """Correct the ground projection while keeping every vertical line vertical.

  Two affine halves meet continuously at the front corner. A global affine fit
  would lean the building when Imagegen puts that corner off the centerline.
  Premultiplied filtering avoids dark halos around the transparent cutout.
  """
  width, height = size
  half = width / 2
  left, front, right = corners
  scale_y = width / (right[0] - left[0]) / (slopes[0] - slopes[1])
  source = image.convert('RGBa')
  # Affine bicubic sampling has a fixed source-pixel footprint. Prefilter large
  # reductions so thin outlines and window frames cannot fall between samples.
  # Keep enough source detail for the existing 2x projection and final filter.
  reduction = max(1, int(min((front[0] - left[0]) / half,
      (right[0] - front[0]) / half, 1 / scale_y) / 2))
  if reduction > 1:
    source = source.reduce(reduction)
  result = Image.new('RGBa', size)
  for side, (p, q, origin, edge_slope) in enumerate(
      ((left, front, baseline - width / 4, .5), (front, right, baseline, -.5))):
    coefficients = ((q[0] - p[0]) / half, 0, p[0],
        (q[1] - p[1]) / half - edge_slope / scale_y, 1 / scale_y, p[1] - origin / scale_y)
    coefficients = tuple(value / reduction for value in coefficients)
    # Supersample the resource conversion, then retain the authoritative mask at final size.
    coefficients = tuple(value / 2 if i in (0, 1, 3, 4) else value
        for i, value in enumerate(coefficients))
    piece = source.transform((width, height * 2), Image.Transform.AFFINE, coefficients,
        Image.Resampling.BICUBIC).resize((width // 2, height), Image.Resampling.LANCZOS)
    result.paste(piece, (side * (width // 2), 0))
  if inset:
    scale = (width - 2 * inset) / width
    center_x, center_y = width / 2, baseline - width / 4
    result = result.transform(size, Image.Transform.AFFINE,
        (1 / scale, 0, center_x * (1 - 1 / scale), 0, 1 / scale, center_y * (1 - 1 / scale)),
        Image.Resampling.BICUBIC)
  return result.convert('RGBA')


def Ground(texture, mask, view):
  width, height = mask.size
  baseline = height
  top = baseline - width / 2
  texture = texture.transpose((Image.Transpose.ROTATE_90, Image.Transpose.ROTATE_180,
      Image.Transpose.ROTATE_270)[view - 1]) if view else texture
  texture = texture.convert('RGB')
  tw, th = texture.size
  coefficients = (tw / width, tw * 2 / width, -tw * (.5 + top * 2 / width),
      -th / width, th * 2 / width, th * (.5 - top * 2 / width))
  neutral = tuple(np.asarray(texture).mean(axis=(0, 1)).astype(int))
  coefficients = tuple(value / 2 if i in (0, 1, 3, 4) else value
      for i, value in enumerate(coefficients))
  result = texture.transform((width * 2, height * 2), Image.Transform.AFFINE, coefficients,
      Image.Resampling.BILINEAR, fillcolor=neutral).resize(mask.size, Image.Resampling.LANCZOS).convert('RGBA')
  result.putalpha(mask)
  return result


def BuildingRoofTop(image, minimum_width=0):
  """Ignore thin antenna tips when measuring the architectural silhouette."""
  alpha = np.asarray(image.getchannel('A')) >= 192
  if not minimum_width:
    return int(np.nonzero(alpha)[0].min())
  rows = alpha.sum(axis=1) >= minimum_width
  for y in range(image.height - 2):
    if rows[y:y+3].all():
      return y
  # Narrow towers and mast-only structures have no broad roof to distinguish from an antenna.
  return int(np.nonzero(alpha)[0].min())


def RegisterBuildingScale(image, target_height, roof_width):
  """Match stock roof scale without stretching windows or the isometric roof planes."""
  width, height = image.size
  alpha = np.asarray(image.getchannel('A')) >= 192
  yy, xx = np.nonzero(alpha)
  anchor_y = int(yy.max())
  anchor_x = float(np.median(xx[yy >= anchor_y - 2]))
  top = BuildingRoofTop(image, roof_width)
  target = height - target_height
  scale = (anchor_y - target) / (anchor_y - top)
  if not .5 <= scale <= 2:
    raise ValueError('Building scale differs too far from its stock roof height')
  left, right = anchor_x + (np.array([xx.min(), xx.max()]) - anchor_x) * scale
  if left < 0 or right >= width:
    raise ValueError('Stock roof scale would crop the building; repair its registration')
  result = image.convert('RGBa').transform(image.size, Image.Transform.AFFINE,
      (1 / scale, 0, anchor_x * (1 - 1 / scale), 0, 1 / scale, anchor_y * (1 - 1 / scale)),
      Image.Resampling.BICUBIC).convert('RGBA')
  return result, dict(source_top=top, target_top=target, scale=scale, mode='uniform',
      anchor=[anchor_x, anchor_y])


def RegisterBuildingHeight(image, reference, roof_width=0):
  """Normalize tall artwork above the rear ground plane; retain the whole ground projection.

  Quarter-turn paintings sometimes change camera elevation slightly. Native ground calibration
  then changes the tower's apparent height even when its storey count is unchanged. Registration
  is an offline resource conversion and never touches pixels in the ground-plane region.
  """
  width, height = image.size
  fixed = height - width // 2
  top = BuildingRoofTop(image, roof_width)
  target = BuildingRoofTop(reference, roof_width)
  if top == target:
    return image, dict(source_top=top, target_top=target, fixed_ground_row=fixed, scale=1)
  if fixed <= max(top, target):
    raise ValueError('Tall building registration has no structure above the ground plane')
  scale = (fixed - target) / (fixed - top)
  if not .5 <= scale <= 2:
    raise ValueError('Rotation changes building height too far; regenerate the artwork')
  result = image.convert('RGBa').transform(image.size, Image.Transform.AFFINE,
      (1, 0, 0, 0, 1 / scale, fixed * (1 - 1 / scale)), Image.Resampling.BICUBIC).convert('RGBA')
  result.paste(image.crop((0, fixed, width, height)), (0, fixed))
  return result, dict(source_top=top, target_top=target, fixed_ground_row=fixed, scale=scale)


def RegisterStructure(image, entry, registration, reference):
  """Align a round upper structure independently of its fixed ground footprint.

  Artist landmarks lock the canopy and drum. A smooth transition through the
  supports reaches the identity mapping before any ground pixels are touched.
  The reference alpha fixes the rotationally symmetric upper silhouette, while
  the registered painting supplies each view's landscape and facade details.
  This is an offline resource conversion, never a runtime deformation.
  """
  target_rows = registration['rows']
  source_rows = entry['structure_rows']
  target_left, target_right = registration['x_bounds']
  source_left, source_right = entry['structure_x_bounds']
  width, height = image.size
  if len(target_rows) != len(source_rows) or len(target_rows) < 3 or \
      any(a >= b for rows in (target_rows, source_rows) for a, b in zip(rows, rows[1:])) or \
      target_rows[-1] != source_rows[-1] or not 0 < target_rows[-1] < height or \
      not 0 <= target_left < target_right < width or not 0 <= source_left < source_right < width:
    raise ValueError('Invalid upper-structure registration landmarks')
  body_bottom, fixed_row = target_rows[-2:]
  if fixed_row > height - width // 2:
    raise ValueError('Upper-structure registration must stop above the ground footprint')
  shared_bottom = registration['shared_silhouette_bottom']
  if not 0 < shared_bottom <= body_bottom:
    raise ValueError('Shared upper silhouette extends into the support transition')
  if target_rows == source_rows and [target_left, target_right] == [source_left, source_right]:
    return image.copy()
  x_scale = (source_right - source_left) / (target_right - target_left)
  x_offset = source_left - target_left * x_scale
  source_bottom = source_rows[-2]
  start_slope = (source_rows[-2] - source_rows[-3]) / (target_rows[-2] - target_rows[-3])

  def Mapping(y):
    if y <= body_bottom:
      if y < target_rows[0]:
        source_y = y + source_rows[0] - target_rows[0]
      else:
        source_y = float(np.interp(y, target_rows, source_rows))
      return x_offset, x_scale, source_y
    t = (y - body_bottom) / (fixed_row - body_bottom)
    # Match the drum's vertical scale at the top and the unchanged footings at
    # the bottom; smooth horizontal easing avoids a kink in the curved legs.
    length = fixed_row - body_bottom
    source_y = (2 * t**3 - 3 * t**2 + 1) * source_bottom + \
        (t**3 - 2 * t**2 + t) * length * start_slope + \
        (-2 * t**3 + 3 * t**2) * fixed_row + (t**3 - t**2) * length
    blend = t * t * (3 - 2 * t)
    return x_offset * (1 - blend), x_scale + (1 - x_scale) * blend, source_y

  boundaries = sorted({0, *target_rows, *range(body_bottom, fixed_row, 8)})
  mesh = []
  for top, bottom in zip(boundaries, boundaries[1:]):
    x0, scale0, y0 = Mapping(top)
    x1, scale1, y1 = Mapping(bottom)
    mesh.append(((0, top, width, bottom),
        (x0, y0, x1, y1, x1 + width * scale1, y1, x0 + width * scale0, y0)))
  result = image.convert('RGBa').transform(image.size, Image.Transform.MESH, mesh,
      Image.Resampling.BICUBIC).convert('RGBA')
  result.paste(image.crop((0, fixed_row, width, height)), (0, fixed_row))
  upper = Image.alpha_composite(reference.crop((0, 0, width, shared_bottom)),
      result.crop((0, 0, width, shared_bottom)))
  upper.putalpha(reference.getchannel('A').crop((0, 0, width, shared_bottom)))
  result.paste(upper, (0, 0))
  return result


def Bake(directory):
  config = json.loads((directory / 'bake.json').read_text())
  keep_ground = not config.get('omit_ground_in_sprite', False)
  if not keep_ground and config.get('retain_painted_ground', False):
    raise ValueError('Foreground-only sprites cannot retain painted ground')
  mask = Image.open(directory / config['mask']).convert('L')
  if np.unique(np.asarray(mask)).tolist() != [0, 255]:
    raise ValueError('Foundation ownership must be binary')
  native = NativeFoundation(config, directory, mask.size)
  if ImageChops.difference(mask, native).getbbox():
    raise ValueError('Foundation differs from the union of native terrain tile masks')
  width, height = mask.size
  owned = np.asarray(mask) != 0
  # The vertical extrusion of the square permits buildings above the rear edge,
  # but never pixels below the front ground edge or outside the site horizontally.
  last_row = np.where(owned, np.arange(height)[:, None], -1).max(axis=0)
  prism = Image.fromarray(np.where(np.arange(height)[:, None] <= last_row, 255, 0).astype('uint8'))
  texture = Image.open(directory / config['paving'])
  preview = Image.new('RGB', ((width + 32) * 2, (height + 48) * 2), (49, 55, 50))
  draw = ImageDraw.Draw(preview)
  records = []
  registration = config.get('structure_registration')
  reference = None
  if registration:
    entry = config['views'][registration['reference_view']]
    original = Image.open(directory / entry['original']).convert('RGBA')
    source = Image.open(directory / entry.get('foreground', entry['original'])).convert('RGBA')
    if source.size != original.size:
      raise ValueError('Reference cutout changed canvas size')
    corners, slopes = ViewGroundCorners(original, entry, True)
    reference = CleanCivicAlpha(Foreground(source, corners, slopes, mask.size, height,
        config.get('content_inset_pixels', 0)))
  height_reference = None
  if config.get('align_building_height', False):
    entry = config['views'][0]
    source = Image.open(directory / entry['original']).convert('RGBA')
    corners, slopes = ViewGroundCorners(source, entry, True)
    height_reference = Foreground(source, corners, slopes, mask.size, height, config.get('content_inset_pixels', 0))
  for view, entry in enumerate(config['views']):
    if config.get('ground_excludes_foreground') and 'foreground' not in entry:
      raise ValueError('Separated painted ground requires an explicit foreground layer')
    if not keep_ground and 'foreground' not in entry:
      raise ValueError('Foreground-only sprites require an explicit extracted foreground')
    original = Image.open(directory / entry['original']).convert('RGBA')
    source = Image.open(directory / entry.get('foreground', entry['original'])).convert('RGBA')
    if original.size != source.size:
      raise ValueError(f'View {view}: cutout changed canvas size; register it before baking')
    corners, slopes = ViewGroundCorners(original, entry)
    inset = config.get('content_inset_pixels', 0)
    foreground_corners, foreground_slopes = (ViewGroundCorners(original, entry, True)
        if 'foreground_ground_corners' in entry else (corners, slopes))
    foreground = Foreground(source, foreground_corners, foreground_slopes, mask.size, height, inset)
    artwork_scale = config.get('artwork_scale',1)
    if artwork_scale != 1:
      # Offline uniform scaling keeps the plot's front corner fixed and preserves doorway/window proportions.
      center_x, center_y = width/2, height
      foreground = foreground.convert('RGBa').transform(mask.size,Image.Transform.AFFINE,
          (1/artwork_scale,0,center_x*(1-1/artwork_scale),
              0,1/artwork_scale,center_y*(1-1/artwork_scale)),Image.Resampling.BICUBIC).convert('RGBA')
    height_registration = None
    if 'target_roof_height' in config:
      foreground, height_registration = RegisterBuildingScale(foreground, config['target_roof_height'],
          config.get('roof_width_pixels', width//16))
    elif height_reference is not None:
      foreground, height_registration = RegisterBuildingHeight(foreground, height_reference,
          config.get('roof_width_pixels', 0))
    if registration:
      foreground = RegisterStructure(foreground, entry, registration, reference)
    before = np.asarray(foreground.getchannel('A'))
    outside = (before >= 128) & (np.asarray(prism) == 0)
    if config.get('reject_opaque_trim', False) and outside.any():
      edge = config.get('edge_registration_pixels',0)
      allowed = prism.filter(ImageFilter.MaxFilter(2*edge+1)) if edge else prism
      structural_overflow = outside & (np.asarray(allowed)==0)
      if config.get('retain_foreground_ground'):
        # Authored landmark paving is cut to the authoritative tile union, like
        # the shared ground pass. Upper architecture must still fit its site.
        structural_overflow &= np.arange(height)[:,None] < height-width//2
      if structural_overflow.any():
        raise ValueError(f'View {view}: opaque artwork exceeds site; repair its ground registration before baking')
    foreground.putalpha(ImageChops.multiply(foreground.getchannel('A'), prism))
    ground = Ground(texture, mask, view)
    if config.get('retain_painted_ground', False):
      if config.get('ground_excludes_foreground'):
        original.putalpha(ImageChops.subtract(original.getchannel('A'), source.getchannel('A')))
      painted = Foreground(source if config.get('retain_foreground_ground') else original,
          corners, slopes, mask.size, height, inset)
      painted.putalpha(ImageChops.multiply(painted.getchannel('A'), mask))
      # Missing rounded/truncated source corners still need the exact square mask.
      # Match the small edge fill to the painting instead of inserting a gray patch.
      corner_color = config.get('corner_fill_color')
      if corner_color is not None:
        ground = Image.new('RGBA', mask.size, (*corner_color, 255))
        ground.putalpha(mask)
      else:
        colors = np.asarray(painted)
        r, g, b, a = (colors[:, :, channel].astype(float) for channel in range(4))
        paving = (a > 192) & (r >= g) & (g >= b) & (g > 125) & (b > 90) & (r - b < 80)
        if not paving.any():
          raise ValueError(f'View {view}: no painted paving available for the missing-corner fill')
        tint = np.median(colors[:, :, :3][paving], axis=0)
        fill = np.asarray(ground).copy()
        average = np.asarray(texture.convert('RGB')).mean(axis=(0, 1))
        fill[:, :, :3] = np.clip(fill[:, :, :3] * (tint / average), 0, 255).astype('uint8')
        ground = Image.fromarray(fill)
      ground = Image.alpha_composite(ground, painted)
    foreground = CleanCivicAlpha(foreground)
    final = CleanCivicAlpha(Image.alpha_composite(ground, foreground) if keep_ground else foreground)
    for name, image in ((f'foreground-{view}.png', foreground), (f'ground-{view}.png', ground),
        (f'view-{view}.png', final)):
      pixels = np.asarray(image).copy()
      pixels[pixels[:, :, 3] == 0] = 0
      Image.fromarray(pixels).save(directory / name)
    ground_alpha = np.asarray(ground.getchannel('A'))
    final_alpha = np.asarray(final.getchannel('A'))
    errors = int(np.count_nonzero(ground_alpha != np.asarray(mask)))
    overflow = int(np.count_nonzero((final_alpha != 0) & (np.asarray(prism) == 0)))
    if errors or overflow:
      raise ValueError(f'View {view}: foundation ownership failed')
    x, y = (view % 2) * (width + 32) + 16, (view // 2) * (height + 48) + 32
    preview.paste(final, (x, y), final)
    columns, rows = config['footprint_tiles']
    draw.text((x, y - 20), f'{view * 90} deg - {columns}x{rows}',
        fill=(231, 231, 215))
    records.append(dict(degrees=view * 90, file=f'view-{view}.png', baseline=height,
        canvas=list(mask.size), source_ground_corners=corners, source_front_slopes=slopes,
        ground_owned_pixels=int(owned.sum()), ground_mask_mismatches=errors,
        ground_source='authored painting' if config.get('retain_painted_ground', False) else 'paving texture',
        ground_in_sprite=keep_ground,
        native_terrain_union_mismatches=0,
        sprite_pixels_outside_site_prism=overflow,
        foreground_opaque_pixels_trimmed=int(outside.sum()),
        content_inset_pixels=inset,
        foreground_opaque_pixels=int(np.count_nonzero(before >= 128))))
    if 'foreground_ground_corners' in entry:
      records[-1].update(source_foreground_corners=foreground_corners, foreground_front_slopes=foreground_slopes)
    bounds = final.getchannel('A').getbbox()
    records[-1]['occupied_bounds'] = list(bounds)
    records[-1]['occupied_height'] = bounds[3] - bounds[1]
    if height_registration is not None:
      records[-1]['height_registration'] = height_registration
    if registration:
      bottom = registration['shared_silhouette_bottom']
      mismatch = ImageChops.difference(foreground.getchannel('A').crop((0, 0, width, bottom)),
          reference.getchannel('A').crop((0, 0, width, bottom)))
      if mismatch.getbbox():
        raise ValueError(f'View {view}: registered upper silhouette differs from the reference')
      records[-1]['structure_registration'] = dict(reference_view=registration['reference_view'],
          source_rows=entry['structure_rows'], target_rows=registration['rows'],
          source_x_bounds=entry['structure_x_bounds'], target_x_bounds=registration['x_bounds'],
          shared_silhouette_bottom=bottom, shared_upper_alpha_mismatches=0)
    print(f'View {view * 90}: mask mismatches={errors}, outside site={overflow}, '
        f'foreground edge cleanup={int(outside.sum())} pixels')
  preview.save(directory / 'final-rotations-preview.png')
  (directory / 'bake-result.json').write_text(json.dumps(records, indent=2) + '\n')


def RegisterArtworkForeground(panel, reference, ignored):
  """Fit uniform scale/translation using original architectural colours, never vegetation bounds.

  Registration is an offline conversion. FFT colour matching also works for sparse lattice
  supports and for facades previously hidden by plants; runtime only draws the resulting sprite.
  """
  original_size = panel.size
  best = (float('-inf'), 1., 0., 0.)
  for target_width, radius, step in ((128, .4, .025), (256, .03, .0025)):
    detail = target_width / panel.width
    size = (target_width, max(1,round(panel.height*detail)))
    shape = tuple(1 << (2*length-1).bit_length() for length in reversed(size))
    matching = panel.copy()
    matching.putalpha(ImageChops.multiply(matching.getchannel('A'),ImageChops.invert(ignored)))
    a = np.asarray(reference.resize(size,Image.Resampling.BILINEAR).convert('RGBa')).astype(float)[:,:,:3]/255
    fa = [np.fft.rfft2(a[:,:,channel],s=shape) for channel in range(3)]
    fa2 = np.fft.rfft2((a*a).sum(axis=2),s=shape)
    center = best[1]
    candidate = (float('-inf'),center,0.,0.)
    xs = np.arange(-round(size[0]*.35),round(size[0]*.35)+1)
    ys = np.arange(-round(size[1]*.35),round(size[1]*.35)+1)
    for scale in np.arange(center-radius,center+radius+step/2,step):
      scaled = matching.resize((max(1,round(size[0]*scale)),max(1,round(size[1]*scale))),
          Image.Resampling.BILINEAR).convert('RGBa')
      pixels = np.asarray(scaled).astype(float)/255
      b, weights = pixels[:,:,:3], pixels[:,:,3]
      spectrum = 2*sum(fa[channel]*np.conj(np.fft.rfft2(b[:,:,channel],s=shape)) for channel in range(3))
      spectrum -= fa2*np.conj(np.fft.rfft2(weights,s=shape))
      scores = (np.fft.irfft2(spectrum,s=shape)-(b*b).sum())/max(1,weights.sum())
      region = scores[np.ix_(ys%shape[0],xs%shape[1])]
      iy,ix = np.unravel_index(region.argmax(),region.shape)
      score = region[iy,ix]
      if score > candidate[0]:
        x,y = int(xs[ix]),int(ys[iy])
        def Fraction(axis):
          previous = scores[(y-(axis==0))%shape[0],(x-(axis==1))%shape[1]]
          following = scores[(y+(axis==0))%shape[0],(x+(axis==1))%shape[1]]
          divisor = previous-2*score+following
          return float(np.clip(.5*(previous-following)/divisor,-.5,.5)) if divisor else 0.
        candidate = (float(score),float(scale),(x+Fraction(1))/detail,(y+Fraction(0))/detail)
    best = candidate
  _,scale,dx,dy = best
  aligned = panel.convert('RGBa').transform(original_size,Image.Transform.AFFINE,
      (1/scale,0,-dx/scale,0,1/scale,-dy/scale),Image.Resampling.BICUBIC).convert('RGBA')
  return aligned, dict(scale=scale,offset=[dx,dy],colour_error=max(0,-best[0]),
      method='masked architectural colour registration')


def ExtractAirfieldMarkings(directory, config):
  """Colour-key existing runway paint onto shared asphalt, preserving exact connected edges."""
  for view,entry in enumerate(config['views']):
    image = Image.open(directory/entry['original']).convert('RGBA')
    pixels = np.asarray(image).copy()
    rgb = pixels[:,:,:3].astype(int)
    white = rgb.min(axis=2)>150
    yellow = (rgb[:,:,0]>160) & (rgb[:,:,1]>120) & (rgb[:,:,2]<100)
    keep = white|yellow
    if config['kind'] == 230:
      keep |= (rgb.max(axis=2)<55) & (pixels[:,:,3]>=192)
    pixels[:,:,3] = np.where(keep,pixels[:,:,3],0)
    pixels[pixels[:,:,3]==0] = 0
    filename = f'ground-separated-{view}.png'
    Image.fromarray(pixels).save(directory/filename)
    entry['foreground'] = filename
  config.update(omit_ground_in_sprite=True,retain_painted_ground=False,reject_opaque_trim=True,
      edge_registration_pixels=2)
  (directory/'bake.json').write_text(json.dumps(config,indent=2)+'\n')
  Bake(directory)
  path = directory/'sprites.json'
  spec = json.loads(path.read_text())
  spec.update(ground='asphalt',status='installed in native landscape pack; separate exact surface markings')
  path.write_text(json.dumps(spec,indent=2)+'\n')


def SourceSmokeMask(directory, config, view, corners, slopes, size):
  """Exclude painted plume inserts from registration; shared animated plumes replace them."""
  mask = Image.new('L',size)
  if config['kind'] not in (202,203,207): return mask
  spec = json.loads((directory.parent/'power-smoke/smoke.json').read_text())
  plant = next(item for item in spec['kinds'] if item['kind'] == config['kind'])
  width,height = Image.open(directory/config['mask']).size
  inset = config.get('content_inset_pixels',0)
  inset_scale = (width-2*inset)/width
  left,front,right = corners
  vertical_scale = width/(right[0]-left[0])/(slopes[0]-slopes[1])

  def Point(x,y):
    x = width/2+(x-width/2)/inset_scale
    y = height-width/4+(y-height+width/4)/inset_scale
    p,q,origin,slope,local = (left,front,height-width/4,.5,x) if x<width/2 else \
        (front,right,height,-.5,x-width/2)
    return (p[0]+(q[0]-p[0])*local/(width/2),
        (q[1]-p[1])*local/(width/2)+(y-origin-slope*local)/vertical_scale+p[1])

  draw = ImageDraw.Draw(mask)
  for emitter in plant['views'][view]:
    x,y = emitter['x'],emitter['y']
    a,b = (-55,145) if emitter['type']==1 else (-180,24) if emitter['flipped'] else (-24,180)
    rectangle = (x+a,y-170,x+b,y+16)
    x0,y0,x1,y1 = rectangle
    draw.polygon([Point(x0,y0),Point(x0,y1),Point(x1,y1),Point(x1,y0)],fill=255)
  return mask


def ExtractCivicForeground(directory):
  """Register an ImageGen ground-removal sheet to the original paintings, offline only."""
  registration = json.loads((directory / 'ground-separation-panels.json').read_text())
  config_path = directory / 'bake.json'
  config = json.loads(config_path.read_text())
  if config['kind'] in (221,222,230):
    ExtractAirfieldMarkings(directory,config)
    return
  site = json.loads((directory/'site.json').read_text())
  if site['ground'] == 'painted' and config['kind'] in (13,213):
    for view,entry in enumerate(config['views']):
      filename = registration['source_views'][view]
      if filename == entry['original']: entry.pop('foreground',None)
      else: entry['foreground'] = filename
    config.update(omit_ground_in_sprite=False,retain_painted_ground=True)
    config_path.write_text(json.dumps(config,indent=2)+'\n')
    Bake(directory)
    manifest_path = directory/'sprites.json'
    manifest = json.loads(manifest_path.read_text())
    manifest.update(ground='painted',status='installed in native landscape pack; authored park ground')
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
    return
  source_path = directory / 'ground-separation-foreground.png'
  source = Image.open(source_path).convert('RGBA')
  original_size = tuple(registration['canvas'])
  source_size = source.size
  # Extract each quadrant without stretching its geometry. The returned canvas may be
  # resized or repacked; only uniform registration against the original artwork is allowed.
  splits = []
  solid = np.asarray(source.getchannel('A')) >= 128
  for axis,length in ((0,source.width),(1,source.height)):
    candidates = np.flatnonzero(solid.sum(axis=axis)==0)
    candidates = candidates[(candidates>length*.35) & (candidates<length*.65)]
    splits.append(int(min(candidates,key=lambda x:abs(x-length/2))) if len(candidates) else length//2)
  x,y = splits
  rectangles = [(0,0,x,y),(x,0,source.width,y),(0,y,x,source.height),(x,y,source.width,source.height)]
  checks, panels = [], []
  for view, rectangle in enumerate(rectangles):
    entry = config['views'][view]
    original = Image.open(directory / entry['original']).convert('RGBA')
    reference = Image.open(directory / registration['source_views'][view]).convert('RGBA')
    cell = source.crop(rectangle)
    scale = min(original.width/cell.width,original.height/cell.height)
    cell = cell.convert('RGBa').resize((round(cell.width*scale),round(cell.height*scale)),
        Image.Resampling.LANCZOS).convert('RGBA')
    panel = Image.new('RGBA',original.size)
    panel.alpha_composite(cell,((original.width-cell.width)//2,(original.height-cell.height)//2))
    if reference.size != original.size:
      raise ValueError(f'View {view}: original painting and extracted foreground have different canvases')
    corners, slopes = GroundCorners(original, entry['plinth_pixels'])
    rear = max(1, round(corners[0][1] + corners[2][1] - corners[1][1]))
    rgba = np.asarray(reference).astype(int)
    green = (rgba[:,:,1] > rgba[:,:,0]*1.2) & (rgba[:,:,1] > rgba[:,:,2]*1.2)
    vegetation = Image.fromarray(np.where(green,255,0).astype('uint8'))
    vegetation = vegetation.filter(ImageFilter.MaxFilter(2*max(2,original.width//64)+1))
    green = np.asarray(vegetation)!=0
    expected = Image.fromarray(np.where((rgba[:,:,3] >= 192) & ~green, 255, 0).astype('uint8'))
    expected = expected.crop((0,0,original.width,rear))
    envelope = reference.getchannel('A').point(lambda a: 255 if a >= 192 else 0).crop(
        (0,0,original.width,rear))
    smoke_mask = SourceSmokeMask(directory,config,view,corners,slopes,original.size)
    smoke = smoke_mask.crop((0,0,original.width,rear))
    expected = ImageChops.multiply(expected,ImageChops.invert(smoke))
    envelope = ImageChops.lighter(envelope,smoke)
    if config['kind'] in (221,222,230,231):
      # Surface markings and a parked aircraft sit below the rear ground corner.
      # Their bright paint is the registration landmark; plain asphalt is removed.
      rear = original.height
      bright = rgba[:,:,:3].min(axis=2)>150
      yellow = (rgba[:,:,0]>160) & (rgba[:,:,1]>120) & (rgba[:,:,2]<100)
      expected = Image.fromarray(np.where((rgba[:,:,3]>=192) & (bright|yellow),255,0).astype('uint8'))
      envelope = reference.getchannel('A').point(lambda a:255 if a>=192 else 0)
      reference = reference.copy()
      reference.putalpha(expected)
    tolerance = max(2,round(original.width / (config['footprint_tiles'][0]*128)*2))
    dilation = 2*tolerance+1

    def Placement(image):
      actual = image.getchannel('A').point(lambda a: 255 if a >= 192 else 0).crop(
          (0,0,original.width,rear))
      a, b = np.asarray(expected)!=0, np.asarray(actual)!=0
      coverage = np.count_nonzero(a & (np.asarray(actual.filter(ImageFilter.MaxFilter(dilation)))!=0))/a.sum() \
          if a.any() else 1.
      excess = np.count_nonzero(b & (np.asarray(envelope.filter(ImageFilter.MaxFilter(dilation)))==0))/b.sum() \
          if b.any() else 0.
      return float(coverage), float(excess), actual

    coverage, excess, actual = Placement(panel)
    alignment = None
    if expected.getbbox() and panel.getchannel('A').getbbox():
      aligned, alignment = RegisterArtworkForeground(panel,reference,smoke_mask)
      coverage, excess, _ = Placement(aligned)
      panel = aligned
    if config.get('preserve_upper_source_pixels'):
      # A broad stadium roof is already approved artwork. Keep its original
      # pixels; the generated edit supplies the separated site details below it.
      panel.paste(reference.crop((0,0,original.width,rear)),(0,0))
      coverage, excess, _ = Placement(panel)
    checks.append(dict(view=view,upper_structure_coverage=coverage,upper_structure_excess=excess,
        alignment=alignment))
    substantial_upper_structure = np.count_nonzero(np.asarray(expected)) >= original.width*original.height*.005
    if substantial_upper_structure and (coverage < .95 or excess > .05) or \
        not substantial_upper_structure and alignment and alignment['colour_error'] > .16:
      raise ValueError(f'View {view}: ground removal moved or changed the structure: {checks[-1]}')
    panels.append(panel)
  backup = directory / 'ground-separation-bake.json'
  if not backup.exists(): backup.write_text(config_path.read_text())
  for view, panel in enumerate(panels):
    filename = f'ground-separated-{view}.png'
    panel.save(directory / filename)
    config['views'][view]['foreground'] = filename
  authored = site['ground'] == 'painted'
  config.update(omit_ground_in_sprite=not authored,retain_painted_ground=authored,
      retain_foreground_ground=authored,reject_opaque_trim=True,edge_registration_pixels=2)
  config_path.write_text(json.dumps(config,indent=2)+'\n')
  Bake(directory)
  manifest_path = directory / 'sprites.json'
  manifest = json.loads(manifest_path.read_text())
  manifest['ground'] = json.loads((directory/'site.json').read_text())['ground']
  manifest['status'] = 'installed in native landscape pack; authored ground' if authored else \
      'installed in native landscape pack; separate ground'
  manifest['ground_separation'] = 'ground-separation-result.json'
  manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
  result = dict(source_sheet_sha256=hashlib.sha256((directory/registration['sheet']).read_bytes()).hexdigest(),
      source_canvas=list(source_size),registered_canvas=list(original_size),checks=checks)
  (directory/'ground-separation-result.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('directory', type=Path, help='building directory containing bake.json')
  parser.add_argument('--split-foreground', action='store_true')
  args = parser.parse_args()
  if args.split_foreground: ExtractCivicForeground(args.directory)
  else: Bake(args.directory)
