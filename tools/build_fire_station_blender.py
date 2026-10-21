#!/usr/bin/env python3
"""Offline fire-station rotation guide. Blender is never a runtime dependency.

blender --background --python tools/build_fire_station_blender.py -- --samples 48 --detail 3
Coordinates and camera match the civic City Hall prototype: 30 units = 3 tiles.
The checked-in toolbar illustration supplies the original architectural design.
"""
import argparse
import json
import math
from pathlib import Path
import random
import sys

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from strip_png_metadata import StripPngMetadata

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'assets/landscape/civic/211-fire-station'
WIDTH, HEIGHT = 384, 448


def Material(name, color, metal=0, brick=False):
  material = bpy.data.materials.new(name)
  material.diffuse_color = (*color, 1)
  material.use_nodes = True
  nodes, links = material.node_tree.nodes, material.node_tree.links
  shader = nodes.get('Principled BSDF')
  shader.inputs['Base Color'].default_value = (*color, 1)
  shader.inputs['Roughness'].default_value = .58 if metal else .82
  shader.inputs['Metallic'].default_value = metal
  coordinates = nodes.new('ShaderNodeTexCoord')
  noise = nodes.new('ShaderNodeTexNoise')
  noise.inputs['Scale'].default_value = 11
  noise.inputs['Detail'].default_value = 3
  links.new(coordinates.outputs['Object'], noise.inputs['Vector'])
  ramp = nodes.new('ShaderNodeValToRGB')
  ramp.color_ramp.elements[0].color = tuple(c * .72 for c in color) + (1,)
  ramp.color_ramp.elements[1].color = tuple(min(1, c * 1.18) for c in color) + (1,)
  links.new(noise.outputs['Fac'], ramp.inputs[0])
  links.new(ramp.outputs[0], shader.inputs['Base Color'])
  bump = nodes.new('ShaderNodeBump')
  bump.inputs['Strength'].default_value = .22
  bump.inputs['Distance'].default_value = .045
  links.new(noise.outputs['Fac'], bump.inputs['Height'])
  links.new(bump.outputs[0], shader.inputs['Normal'])
  if brick:
    # Each wall gets its own projected UVs, so courses remain horizontal on all four sides.
    texture = nodes.new('ShaderNodeTexBrick')
    texture.inputs['Color1'].default_value = (*color, 1)
    texture.inputs['Color2'].default_value = tuple(c * .64 for c in color) + (1,)
    texture.inputs['Mortar'].default_value = (.34, .22, .13, 1)
    texture.inputs['Scale'].default_value = 1
    texture.inputs['Mortar Size'].default_value = .018
    texture.inputs['Brick Width'].default_value = .72
    texture.inputs['Row Height'].default_value = .28
    links.new(coordinates.outputs['UV'], texture.inputs['Vector'])
    links.new(texture.outputs['Color'], shader.inputs['Base Color'])
    links.new(texture.outputs['Fac'], bump.inputs['Height'])
  return material


def Mesh(name, vertices, faces, material, bevel=0):
  mesh = bpy.data.meshes.new(name)
  mesh.from_pydata(vertices, [], faces)
  mesh.materials.append(material)
  obj = bpy.data.objects.new(name, mesh)
  bpy.context.collection.objects.link(obj)
  uv = mesh.uv_layers.new(name='Wall courses')
  for face in mesh.polygons:
    axis = max(range(3), key=lambda i: abs(face.normal[i]))
    axes = (1, 2) if axis == 0 else (0, 2) if axis == 1 else (0, 1)
    for loop in face.loop_indices:
      p = mesh.vertices[mesh.loops[loop].vertex_index].co
      uv.data[loop].uv = (p[axes[0]], p[axes[1]])
  if bevel:
    modifier = obj.modifiers.new('Dressed edges', 'BEVEL')
    modifier.width, modifier.segments = bevel, 2
  return obj


def Box(name, center, size, material, bevel=.025):
  x, y, z = center
  a, b, c = (v / 2 for v in size)
  vertices = [(x + dx, y + dy, z + dz) for dz in (-c, c) for dy in (-b, b) for dx in (-a, a)]
  return Mesh(name, vertices,
      [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)],
      material, bevel)


def Line(name, points, radius, material):
  curve = bpy.data.curves.new(name, 'CURVE')
  curve.dimensions, curve.bevel_depth, curve.bevel_resolution = '3D', radius, 2
  spline = curve.splines.new('POLY')
  spline.points.add(len(points) - 1)
  for p, coordinate in zip(spline.points, points):
    p.co = (*coordinate, 1)
  obj = bpy.data.objects.new(name, curve)
  bpy.context.collection.objects.link(obj)
  curve.materials.append(material)
  return obj


def Cylinder(name, center, radius, depth, material, normal=(0, 0, 1), top=None, sides=16):
  rotation = Vector((0, 0, 1)).rotation_difference(Vector(normal))
  top = radius if top is None else top
  vertices = [tuple(Vector(center) + rotation @ Vector((r * math.cos(i * 2 * math.pi / sides),
      r * math.sin(i * 2 * math.pi / sides), z)))
      for z, r in ((-depth / 2, radius), (depth / 2, top)) for i in range(sides)]
  faces = [tuple(range(sides - 1, -1, -1)), tuple(range(sides, sides * 2))]
  faces += [(i, (i + 1) % sides, (i + 1) % sides + sides, i + sides) for i in range(sides)]
  return Mesh(name, vertices, faces, material)


class Station:
  def __init__(self):
    self.brick = Material('Vivid terracotta brick', (.47, .115, .045), brick=True)
    self.trim = Material('Warm ivory dressed stone', (.78, .64, .43))
    self.slate = Material('Blue charcoal slate', (.065, .095, .13))
    self.edge = Material('Slate ridge caps', (.12, .15, .18))
    self.glass = Material('Cool blue window glass', (.025, .085, .12), .25)
    self.dark = Material('Deep garage and window recesses', (.013, .017, .02))
    self.red = Material('Fire engine vermilion enamel', (.68, .018, .008), .2)
    self.metal = Material('Brushed aluminum equipment', (.48, .52, .53), .72)
    self.gold = Material('Brass fittings', (.66, .39, .06), .7)
    self.paving = Material('Warm sandstone paving', (.43, .4, .32))
    self.soil = Material('Planting soil', (.065, .037, .015))
    self.wood = Material('Tree bark', (.13, .065, .021))
    self.leaves = [Material('Leaf ' + str(i), c) for i, c in enumerate(
        ((.03, .12, .008), (.12, .28, .012), (.24, .37, .025), (.06, .2, .018)))]
    self.rng = random.Random(211)

  def WallBox(self, name, axis, wall, along, z, width, depth, height, material, sign=1):
    center = (wall + sign * depth / 2, along, z) if axis == 0 else (along, wall + sign * depth / 2, z)
    size = (depth, width, height) if axis == 0 else (width, depth, height)
    return Box(name, center, size, material)

  def Arch(self, name, axis, wall, along, bottom, width, height, sign=1, garage=False):
    radius, spring = width / 2, bottom + height - width / 2
    def Point(u, z, depth=0):
      return (wall + sign * depth, along + u, z) if axis == 0 else (along + u, wall + sign * depth, z)
    outline = [Point(-radius, bottom), Point(radius, bottom)]
    outline += [Point(radius * math.cos(a * math.pi / 16), spring + radius * math.sin(a * math.pi / 16))
        for a in range(17)]
    Mesh(name + ' recess', outline, [tuple(range(len(outline)))], self.dark)
    for i in range(16):
      a, b = i * math.pi / 16, (i + .94) * math.pi / 16
      vertices = [Point(r * math.cos(t), spring + r * math.sin(t), d)
          for d in (.025, .22) for r, t in ((radius, a), (radius + .25, a),
              (radius + .25, b), (radius, b))]
      Mesh(name + ' arch voussoir', vertices,
          [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (3, 2, 6, 7)], self.trim, .015)
    for u in (-radius - .12, radius + .12):
      self.WallBox(name + ' jamb', axis, wall, along + u, (bottom + spring) / 2,
          .24, .23, spring - bottom, self.trim, sign)
    if not garage:
      self.WallBox(name + ' glass', axis, wall + sign * .015, along, bottom + (height - .35) / 2,
          width - .18, .025, height - .35, self.glass, sign)
      for u in (-width / 4, 0, width / 4):
        self.WallBox(name + ' mullion', axis, wall + sign * .06, along + u, bottom + height * .42,
            .055, .055, height * .84, self.trim, sign)
      self.WallBox(name + ' transom', axis, wall + sign * .07, along, spring - .1,
          width, .07, .08, self.trim, sign)
      self.WallBox(name + ' sill', axis, wall, along, bottom - .08, width + .6, .38, .18, self.trim, sign)
    else:
      for u in (-radius + .18, radius - .18):
        self.WallBox('Folded red bay door', axis, wall + sign * .08, along + u, bottom + 1.7,
            .3, .15, 3.4, self.red, sign)

  def Roof(self, x0, y0, x1, y1, eave, ridge):
    mid = (x0 + x1) / 2
    vertices = [(x0, y0, eave), (x1, y0, eave), (mid, y0, ridge),
        (x0, y1, eave), (x1, y1, eave), (mid, y1, ridge)]
    Mesh('Brick gable ends', vertices, [(0, 2, 1), (3, 4, 5)], self.brick)
    Mesh('Two fixed slate roof pitches', vertices, [(0, 3, 5, 2), (2, 5, 4, 1)], self.slate)
    for side in (x0, x1):
      for row in range(1, 12):
        t = row / 12
        x, z = side + (mid - side) * t, eave + (ridge - eave) * t
        Line('Slate courses', [(x, y0, z + .025), (x, y1, z + .025)], .025, self.edge)
      Line('Ivory gable bargeboard', [(side, y1 + .03, eave), (mid, y1 + .03, ridge)], .12, self.trim)
      Line('Rear gable bargeboard', [(side, y0 - .03, eave), (mid, y0 - .03, ridge)], .12, self.trim)
      Line('Cast iron gutter', [(side, y0, eave), (side, y1, eave)], .09, self.edge)
    for y in range(math.ceil(y0), math.floor(y1)):
      Cylinder('Ridge cap', (mid, y + .5, ridge + .08), .13, .96, self.edge, normal=(0, 1, 0))

  def Plant(self, x, y, size=1, tree=False):
    Box('Stone planter', (x, y, .23), (size * 2, size * 2, .3), self.trim)
    Box('Planter earth', (x, y, .4), (size * 1.8, size * 1.8, .08), self.soil)
    z = 3 if tree else .8
    if tree:
      Cylinder('Tree trunk', (x, y, 1.6), .12, 2.8, self.wood)
    for i in range(45 if tree else 20):
      a = self.rng.random() * math.tau
      r = self.rng.random() ** .5 * size
      h = self.rng.uniform(-.7, .7) * size
      bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=self.rng.uniform(.35, .6) * size,
          location=(x + math.cos(a) * r, y + math.sin(a) * r, z + h))
      obj = bpy.context.object
      obj.name = 'Irregular layered foliage'
      obj.scale.z = 1.25
      obj.data.materials.append(self.rng.choice(self.leaves))
      for p in obj.data.polygons:
        p.use_smooth = True

  def HipRoof(self):
    # Lower attached wing intersects the taller gable; this asymmetry defines the reference.
    corners = [(-3.5, -9.6, 10.8), (7.6, -9.6, 10.8), (7.6, 7.6, 10.8), (-3.5, 7.6, 10.8)]
    crown = [(-1.1, -6.6, 13.4), (4.6, -6.6, 13.4), (4.6, 4.6, 13.4), (-1.1, 4.6, 13.4)]
    Mesh('Lower wing hipped slate roof', corners + crown,
        [(i, (i + 1) % 4, (i + 1) % 4 + 4, i + 4) for i in range(4)] + [(4, 5, 6, 7)], self.slate)
    for i in range(4):
      Line('Wing hip and copper gutter', [corners[i], crown[i]], .07, self.edge)
      Line('Wing eave', [corners[i], corners[(i + 1) % 4]], .11, self.trim)

  def Engine(self):
    Box('Engine chassis', (-2, 10.6, .85), (2.8, 5.8, .45), self.dark)
    Box('Red equipment body', (-2, 9.3, 1.9), (2.75, 3.1, 2), self.red, .1)
    Box('Engine cab', (-2, 12, 2), (2.8, 2.3, 2.2), self.red, .15)
    Box('Cab windscreen', (-2, 13.17, 2.48), (2.4, .06, .84), self.glass, .05)
    Box('Windscreen divider', (-2, 13.23, 2.48), (.06, .07, .85), self.metal)
    Box('Chrome bumper', (-2, 13.35, .95), (3, .28, .25), self.metal)
    Box('Radiator grille', (-2, 13.22, 1.43), (1.5, .08, .52), self.dark)
    for x in (-3.15, -.85):
      Cylinder('Headlamp', (x, 13.28, 1.48), .18, .1, self.trim, normal=(0, 1, 0))
    for x in (-3.45, -.55):
      for y in (8.7, 12):
        Cylinder('Rubber wheel', (x, y, .8), .64, .38, self.dark, normal=(1, 0, 0))
        Cylinder('Wheel hub', (x + (-.21 if x < -2 else .21), y, .8), .33, .05,
            self.metal, normal=(1, 0, 0))
      Box('Cab side glass', (x, 12, 2.45), (.04, 1.6, .82), self.glass)
      Box('Equipment shutters', (x, 9.3, 1.85), (.05, 2.6, 1.05), self.metal)
      Box('White safety stripe', (x, 10.5, 1.15), (.06, 5, .17), self.trim)
    for x in (-2.75, -1.25):
      Line('Roof ladder rail', [(x, 7.8, 3.22), (x, 12, 3.22)], .06, self.metal)
    for i in range(12):
      y = 7.9 + i * .35
      Line('Roof ladder rung', [(-2.75, y, 3.22), (-1.25, y, 3.22)], .04, self.metal)
    for x in (-2.9, -1.1):
      Cylinder('Red emergency beacon', (x, 12.4, 3.3), .19, .28, self.red)

  def Build(self):
    # Individual paving slabs stop inside the exact site: no square plinth outside the mask.
    for x in range(-14, 14, 2):
      for y in range(-14, 14, 2):
        if y >= 7 or x <= -12 or x >= 8 or y <= -10:
          Box('Forecourt paving slab', (x + 1, y + 1, .08), (1.97, 1.97, .16), self.paving, .012)
    Box('Main red brick station', (-2, -1, 5.4), (18, 16, 10.5), self.brick)
    for z, height, extra in ((.6, .5, .15), (6.15, .24, .2), (10.5, .28, .35)):
      Box('Main carved string course', (-2, -1, z), (18 + extra, 16 + extra, height), self.trim)
    for x, width in ((-7, 5.8), (.1, 4.5), (5.2, 4.5)):
      self.Arch('Engine bay', 1, 7.03, x, .2, width, 5.2, garage=True)
    for x in (-9, -5, -.6, 2.3, 5.2):
      self.Arch('Upper front window', 1, 7.04, x, 7, 1.65, 2.6)
    for x in (-8, -2, 4):
      self.Arch('Upper rear window', 1, -9.04, x, 7, 1.65, 2.6, sign=-1)
      self.Arch('Rear ground window', 1, -9.04, x, 1.4, 1.65, 3.4, sign=-1)
    for axis, wall, sign in ((0, -11.04, -1), (0, 7.04, 1)):
      for y in (-6.5, -2, 3):
        for z in (1.4, 7):
          self.Arch('Side window', axis, wall, y, z, 1.65, 2.6, sign)
    self.Roof(-11.6, -9.6, -2.4, 7.6, 10.8, 16.1)
    self.HipRoof()
    # Circular front/rear attic windows share the same gable geometry.
    for y in (7.64, -9.64):
      Cylinder('Gable oculus surround', (-7, y, 12.5), .66, .15, self.trim, normal=(0, 1, 0))
      Cylinder('Gable oculus glass', (-7, y + (.09 if y > 0 else -.09), 12.5), .45, .04,
          self.glass, normal=(0, 1, 0))
    Box('Brick chimney', (3.4, -1, 14.1), (1.1, 1.1, 3), self.brick)
    Box('Chimney cap', (3.4, -1, 15.65), (1.4, 1.4, .22), self.trim)
    Box('Chimney dark opening', (3.4, -1, 15.78), (.8, .8, .03), self.dark)
    # Hose tower at rear right: never moved independently when rotating.
    Box('Hose tower', (9.7, -7.5, 10.9), (4.8, 4.8, 21.5), self.brick)
    for z in (.55, 6.2, 10.9, 16, 21.5):
      Box('Tower limestone belt', (9.7, -7.5, z), (5.05, 5.05, .24), self.trim)
    for axis, wall, along, sign in ((0, 12.12, -7.5, 1), (0, 7.28, -7.5, -1),
        (1, -5.08, 9.7, 1), (1, -9.92, 9.7, -1)):
      for z in (2, 7, 12, 17.3):
        self.Arch('Tower arched vent', axis, wall, along, z, 1.25, 2.6, sign)
    corners = [(x, y, 21.8) for x, y in ((6.9, -10.3), (12.5, -10.3), (12.5, -4.7), (6.9, -4.7))]
    Mesh('Tower pyramid slate roof', corners + [(9.7, -7.5, 25)],
        [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)], self.slate)
    for p in corners:
      Line('Tower hip cap', [p, (9.7, -7.5, 25)], .08, self.edge)
    Cylinder('Brass tower finial', (9.7, -7.5, 25.35), .12, .7, self.gold, top=.025)
    before = set(bpy.context.scene.objects)
    self.Engine()
    for obj in set(bpy.context.scene.objects) - before:
      obj.location.x += 2.1
    for x, y, size, tree in ((-13, -11.8, 1.3, True), (12.5, -12.3, 1.1, True),
        (11.8, 4.5, 1.5, True), (-12.5, 6, .7, False), (11.8, 10.5, .7, False)):
      self.Plant(x, y, size, tree)
    for x, y in ((-11, 12), (10, 12)):
      Cylinder('Lamp base', (x, y, .24), .22, .45, self.trim)
      Cylinder('Cast iron lamp post', (x, y, 1.9), .06, 3.4, self.edge)
      Cylinder('Frosted lantern', (x, y, 3.7), .23, .5, self.trim)
      Cylinder('Lantern cap', (x, y, 4), .3, .2, self.edge, top=.03)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--samples', type=int, default=48)
  parser.add_argument('--detail', type=int, default=3)
  parser.add_argument('--views', type=int, nargs='+', choices=range(4), default=list(range(4)))
  parser.add_argument('--foundation-guides', action='store_true',
      help='show the complete square ground plane as an Imagegen footprint constraint')
  args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
  bpy.ops.object.select_all(action='SELECT')
  bpy.ops.object.delete(use_global=False)
  station = Station()
  station.Build()
  if args.foundation_guides:
    Mesh('Complete square foundation: exactly three by three tiles',
        [(-15, -15, 0), (15, -15, 0), (15, 15, 0), (-15, 15, 0)],
        [(0, 1, 2, 3)], station.paving)
  root = bpy.data.objects.new('Fire station: one immutable plan, four quarter turns', None)
  bpy.context.collection.objects.link(root)
  for obj in list(bpy.context.scene.objects):
    if obj != root:
      obj.parent = root
  # Authoring coordinates put bays on +y. Swap axes once so the reference front view
  # shows bays on screen left and the rear hose tower on screen right, as in the icon.
  # Subsequent views are only rigid 90-degree turns of this same complete model.
  root.scale.x = -1
  root.rotation_euler.z = -math.pi / 2
  scene = bpy.context.scene
  scene.render.engine = 'CYCLES'
  scene.cycles.samples, scene.cycles.use_denoising = args.samples, True
  scene.render.threads_mode, scene.render.threads = 'FIXED', 6
  scene.render.resolution_x, scene.render.resolution_y = WIDTH * args.detail, HEIGHT * args.detail
  scene.render.resolution_percentage = 100
  scene.render.film_transparent = True
  scene.render.image_settings.file_format, scene.render.image_settings.color_mode = 'PNG', 'RGBA'
  scene.view_settings.view_transform = 'Standard'
  scene.view_settings.exposure = 0
  scene.world.use_nodes = True
  scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value = (.86, .91, 1, 1)
  scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value = .65
  bpy.ops.object.light_add(type='SUN', location=(-35, 10, 65))
  light = bpy.context.object
  light.data.energy, light.data.angle = 1.5, math.radians(10)
  light.data.color = (1, .93, .83)
  light.rotation_euler = Vector((35, -10, -65)).to_track_quat('-Z', 'Y').to_euler()
  pixels_per_metre = WIDTH / (30 * math.sqrt(2))
  target_z = (HEIGHT / 2 - WIDTH / 4) / (math.cos(math.pi / 6) * pixels_per_metre)
  target = Vector((0, 0, target_z))
  bpy.ops.object.camera_add(location=target + Vector((40, 40, 40 * math.sqrt(2 / 3))))
  camera = bpy.context.object
  camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
  camera.data.type, camera.data.ortho_scale = 'ORTHO', HEIGHT / pixels_per_metre
  scene.camera = camera
  OUTPUT.mkdir(parents=True, exist_ok=True)
  model_name = 'fire-station-foundation-guide.blend' if args.foundation_guides else 'fire-station.blend'
  bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / model_name))
  projections = []
  for view in args.views:
    root.rotation_euler.z = (view - 1) * math.pi / 2
    bpy.context.view_layer.update()
    points = []
    for point in ((-15, -15, 0), (-15, 15, 0), (15, 15, 0), (15, -15, 0)):
      p = world_to_camera_view(scene, camera, Vector(point))
      points.append([round(p.x * WIDTH, 5), round((1 - p.y) * HEIGHT, 5)])
    projections.append({'view': view, 'degrees': view * 90, 'site_corners': points})
    render_directory = ROOT / 'build/art-reference/civic-fire-station'
    render_directory.mkdir(parents=True, exist_ok=True)
    prefix = 'square-geometry' if args.foundation_guides else 'geometry'
    scene.render.filepath = str(render_directory / f'{prefix}-{view}.png')
    bpy.ops.render.render(write_still=True)
    StripPngMetadata(Path(scene.render.filepath))
    print('RENDERED', view, scene.render.filepath, flush=True)
  metadata_name = 'square-projection.json' if args.foundation_guides else 'projection.json'
  (OUTPUT / metadata_name).write_text(json.dumps(projections, indent=2) + '\n')


if __name__ == '__main__':
  main()
