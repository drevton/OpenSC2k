#!/usr/bin/env python3
"""Build and render original civic artwork offline in Blender.

blender --background --python tools/build_civic_blender.py -- --samples 48
The game consumes finished PNG sprite runs, never this model or Blender.
"""
import argparse
import math
from pathlib import Path
import random
import sys

import bpy
from mathutils import Matrix, Vector
from bpy_extras.object_utils import world_to_camera_view

sys.path.insert(0, str(Path(__file__).resolve().parent))
from strip_png_metadata import StripPngMetadata

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/landscape/civic/208-city-hall"
MATERIALS = ROOT / "assets/landscape/civic/materials"
WIDTH, HEIGHT = 384, 448
RNG = random.Random(208)


def Material(name, color, roughness=.7, metal=0, texture=None):
  material = bpy.data.materials.new(name)
  material.diffuse_color = (*color, 1)
  material.use_nodes = True
  nodes, links = material.node_tree.nodes, material.node_tree.links
  shader = nodes.get("Principled BSDF")
  shader.inputs['Base Color'].default_value = (*color, 1)
  shader.inputs['Roughness'].default_value = roughness
  shader.inputs['Metallic'].default_value = metal
  coordinates = nodes.new('ShaderNodeTexCoord')
  noise = nodes.new('ShaderNodeTexNoise')
  noise.inputs['Scale'].default_value = 12
  noise.inputs['Detail'].default_value = 3
  links.new(coordinates.outputs['Object'], noise.inputs['Vector'])
  bump = nodes.new('ShaderNodeBump')
  bump.inputs['Strength'].default_value = .18
  bump.inputs['Distance'].default_value = .035
  links.new(noise.outputs['Fac'], bump.inputs['Height'])
  links.new(bump.outputs['Normal'], shader.inputs['Normal'])
  if texture and texture.exists():
    image = nodes.new('ShaderNodeTexImage')
    image.image = bpy.data.images.load(str(texture), check_existing=True)
    image.projection = 'BOX'
    image.projection_blend = .12
    scale = nodes.new('ShaderNodeVectorMath')
    scale.operation = 'SCALE'
    scale.inputs['Scale'].default_value = .32
    geometry = nodes.new('ShaderNodeNewGeometry')
    links.new(geometry.outputs['Position'], scale.inputs[0])
    links.new(scale.outputs['Vector'], image.inputs['Vector'])
    links.new(image.outputs['Color'], shader.inputs['Base Color'])
  else:
    ramp = nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = tuple(c * .82 for c in color) + (1,)
    ramp.color_ramp.elements[1].color = tuple(min(1, c * 1.08) for c in color) + (1,)
    links.new(noise.outputs['Fac'], ramp.inputs[0])
    links.new(ramp.outputs[0], shader.inputs['Base Color'])
  return material


def Mesh(name, vertices, faces, material, bevel=0):
  mesh = bpy.data.meshes.new(name)
  mesh.from_pydata(vertices, [], faces)
  mesh.materials.append(material)
  obj = bpy.data.objects.new(name, mesh)
  bpy.context.collection.objects.link(obj)
  if bevel:
    modifier = obj.modifiers.new('Fine dressed edges', 'BEVEL')
    modifier.width = bevel
    modifier.segments = 2
  return obj


def Box(name, center, dimensions, material, bevel=.025):
  x, y, z = center
  a, b, c = (v / 2 for v in dimensions)
  vertices = [(x + dx, y + dy, z + dz) for dz in (-c, c) for dy in (-b, b) for dx in (-a, a)]
  faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
  return Mesh(name, vertices, faces, material, bevel)


def WallBox(name, axis, wall, along, z, width, depth, height, material, sign=1, bevel=.02):
  center = (wall + sign * depth / 2, along, z) if axis == 0 else (along, wall + sign * depth / 2, z)
  dimensions = (depth, width, height) if axis == 0 else (width, depth, height)
  return Box(name, center, dimensions, material, bevel)


def Line(name, points, radius, material):
  curve = bpy.data.curves.new(name, 'CURVE')
  curve.dimensions = '3D'
  curve.bevel_depth = radius
  curve.bevel_resolution = 2
  spline = curve.splines.new('POLY')
  spline.points.add(len(points) - 1)
  for p, coordinate in zip(spline.points, points):
    p.co = (*coordinate, 1)
  obj = bpy.data.objects.new(name, curve)
  bpy.context.collection.objects.link(obj)
  curve.materials.append(material)
  return obj


def Cylinder(name, center, radius, depth, material, vertices=24, normal=(0, 0, 1), top_radius=None):
  rotation = Vector((0, 0, 1)).rotation_difference(Vector(normal))
  top_radius = radius if top_radius is None else top_radius
  points = []
  for z, r in ((-depth / 2, radius), (depth / 2, top_radius)):
    for i in range(vertices):
      angle = i * 2 * math.pi / vertices
      points.append(tuple(Vector(center) + rotation @ Vector((r * math.cos(angle), r * math.sin(angle), z))))
  faces = [tuple(range(vertices - 1, -1, -1)), tuple(range(vertices, 2 * vertices))]
  faces += [(i, (i + 1) % vertices, (i + 1) % vertices + vertices, i + vertices) for i in range(vertices)]
  return Mesh(name, points, faces, material)


def Band(name, bounds, z, height, projection, material):
  x0, y0, x1, y1 = bounds
  Box(name, ((x0 + x1) / 2, (y0 + y1) / 2, z),
      (x1 - x0 + projection * 2, y1 - y0 + projection * 2, height), material)


def Window(axis, wall, along, z, width, height, sign=1):
  WallBox('Recessed dark window', axis, wall, along, z, width + .15, .08, height + .15, DARK, sign)
  WallBox('Glass', axis, wall + sign * .09, along, z, width, .025, height, GLASS, sign, 0)
  for dx in (-width / 2 - .09, width / 2 + .09):
    WallBox('Carved limestone jamb', axis, wall, along + dx, z, .16, .24, height + .38, STONE, sign)
  for dz in (-height / 2 - .12, height / 2 + .12):
    WallBox('Projecting window sill', axis, wall, along, z + dz, width + .42, .3, .18, TRIM, sign)
  WallBox('Bronze central mullion', axis, wall + sign * .13, along, z, .055, .05, height, BRONZE, sign, .008)
  for dz in (-.35, .45):
    WallBox('Window transom', axis, wall + sign * .13, along, z + dz, width, .05, .055, BRONZE, sign, .008)
  if int((along + 20) * 11 + z * 5) % 7 == 0:
    WallBox('Warm interior blind', axis, wall + sign * .115, along - width * .23,
        z - height * .17, width * .43, .01, height * .5, INTERIOR, sign, 0)


def Facade(bounds, floors, bays, window_width=1.35, window_height=2.3, public=False):
  x0, y0, x1, y1 = bounds
  for axis, wall, start, end, sign, count in (
      (0, x0, y0, y1, -1, bays), (0, x1, y0, y1, 1, bays),
      (1, y0, x0, x1, -1, 3), (1, y1, x0, x1, 1, 3)):
    positions = [start + (i + .5) * (end - start) / count for i in range(count)]
    for along in positions:
      for z in floors:
        if public and axis == 0 and sign == 1 and z < 3 and 4 < along < 8:
          continue
        Window(axis, wall, along, z, window_width, window_height, sign)
    for i in range(count + 1):
      along = start + i * (end - start) / count
      WallBox('Facade pilaster', axis, wall, along, 5.5, .32, .19, 9.9, TRIM, sign)


def PaintedMaterial(name, filename):
  material = bpy.data.materials.new(name)
  material.use_nodes = True
  nodes, links = material.node_tree.nodes, material.node_tree.links
  shader = nodes.get('Principled BSDF')
  image = nodes.new('ShaderNodeTexImage')
  image.image = bpy.data.images.load(str(MATERIALS / filename), check_existing=True)
  image.extension = 'EXTEND'
  links.new(image.outputs['Color'], shader.inputs['Base Color'])
  # A little ambient painted color retains the artwork's interior detail in shadow.
  links.new(image.outputs['Color'], shader.inputs['Emission Color'])
  shader.inputs['Emission Strength'].default_value = .45
  shader.inputs['Roughness'].default_value = .85
  return material


def PaintedWall(axis, wall, start, end, height, material, sign=1, uv=(0, 0, 1, 1)):
  wall += sign * .075
  if axis == 0:
    vertices = [(wall, start, 0), (wall, end, 0), (wall, end, height), (wall, start, height)]
  else:
    vertices = [(start, wall, 0), (end, wall, 0), (end, wall, height), (start, wall, height)]
  obj = Mesh('ImageGen painted facade', vertices, [(0, 1, 2, 3)], material)
  layer = obj.data.uv_layers.new(name='Facade artwork')
  u0, v0, u1, v1 = uv
  for loop, coordinate in zip(layer.data, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))):
    loop.uv = coordinate


def PaintedWing(bounds):
  x0, y0, x1, y1 = bounds
  # The front includes the public door in bay six. Other sides use window-only bays.
  # Insets discard the texture's transparent outer margin without changing the footprint.
  u0, v0, u1, v1 = 50 / 1791, 45 / 878, 1740 / 1791, 793 / 878
  span = u1 - u0
  PaintedWall(0, x1, y0, y1, 10.8, PAINTED_WING, uv=(u0, v0, u1, v1))
  split = y0 + (y1 - y0) * 4 / 7
  PaintedWall(0, x0, y0, split, 10.8, PAINTED_WING, -1, (u0, v0, u0 + span * 4 / 7, v1))
  PaintedWall(0, x0, split, y1, 10.8, PAINTED_WING, -1, (u0, v0, u0 + span * 3 / 7, v1))
  for wall, sign in ((y0, -1), (y1, 1)):
    PaintedWall(1, wall, x0, x1, 10.8, PAINTED_WING, sign, (u0, v0, u0 + span * 3 / 7, v1))


def PaintedBlock(bounds, height, material, uv):
  x0, y0, x1, y1 = bounds
  for axis, wall, start, end, sign in ((0, x0, y0, y1, -1), (0, x1, y0, y1, 1),
      (1, y0, x0, x1, -1), (1, y1, x0, x1, 1)):
    PaintedWall(axis, wall, start, end, height, material, sign, uv)


def HipRoof(name, bounds, bottom, top, crown, material):
  x0, y0, x1, y1 = bounds
  cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
  a = [(x0, y0, bottom), (x1, y0, bottom), (x1, y1, bottom), (x0, y1, bottom)]
  b = [(cx - crown / 2, cy - crown / 2, top), (cx + crown / 2, cy - crown / 2, top),
      (cx + crown / 2, cy + crown / 2, top), (cx - crown / 2, cy + crown / 2, top)]
  obj = Mesh(name, a + b, [(i, (i + 1) % 4, (i + 1) % 4 + 4, i + 4) for i in range(4)] + [(4, 5, 6, 7)], material)
  layer = obj.data.uv_layers.new(name='Copper panels aligned down each roof face')
  for face in obj.data.polygons:
    coordinates = ((.025, .04), (.115, .04), (.115, .96), (.025, .96))
    for loop, coordinate in zip(face.loop_indices, coordinates):
      layer.data[loop].uv = coordinate
  for i in range(4):
    length = (Vector(a[(i + 1) % 4]) - Vector(a[i])).length
    for step in range(round(length / .55) + 1):
      t = step / round(length / .55)
      start = Vector(a[i]).lerp(Vector(a[(i + 1) % 4]), t) + Vector((0, 0, .025))
      end = Vector(b[i]).lerp(Vector(b[(i + 1) % 4]), t) + Vector((0, 0, .025))
      Line('Copper standing seam', [start, end], .025, COPPER_EDGE)
    Line('Roof hip flashing', [a[i], b[i]], .06, COPPER_EDGE)


def Clock(axis, wall, along, sign):
  z = 20.8
  normal = (sign, 0, 0) if axis == 0 else (0, sign, 0)
  center = Vector((wall + sign * .14, along, z) if axis == 0 else (along, wall + sign * .14, z))
  horizontal = Vector((0, 1, 0) if axis == 0 else (1, 0, 0))
  up, outward = Vector((0, 0, 1)), Vector(normal)
  Cylinder('Carved clock surround', center, 1.32, .18, TRIM, 48, normal)
  Cylinder('Bronze clock rim', center + outward * .11, 1.17, .13, BRONZE, 48, normal)
  face = center + outward * .19
  Cylinder('Ivory clock face', face, 1.08, .08, IVORY, 48, normal)
  for i in range(12):
    angle = i * math.pi / 6
    direction = horizontal * math.sin(angle) + up * math.cos(angle)
    Line('Hour marker', [face + outward * .055 + direction * .84,
        face + outward * .055 + direction * .99], .033, DARK)
  for angle, length in ((math.pi / 3, .78), (-math.pi / 3, .56)):
    direction = horizontal * math.sin(angle) + up * math.cos(angle)
    Line('Clock hand', [face + outward * .075, face + outward * .075 + direction * length], .045, DARK)
  Cylinder('Clock spindle', face + outward * .09, .1, .06, BRONZE, 16, normal)


def Foliage(name, center, scale, material):
  bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=1, location=center)
  obj = bpy.context.object
  obj.name = name
  obj.scale = scale
  for v in obj.data.vertices:
    v.co *= RNG.uniform(.83, 1.15)
  for face in obj.data.polygons:
    face.use_smooth = True
  obj.data.materials.append(material)
  return obj


def Planter(x, y, width, depth, tree=False):
  Box('Planter stone rim', (x, y, .32), (width, depth, .64), STONE)
  Box('Planting soil', (x, y, .66), (width - .22, depth - .22, .08), SOIL, 0)
  LeafCloud('Fine mixed shrub foliage', (x, y, 1), (width * .44, depth * .42, .43), 180)
  if tree:
    Cylinder('Cypress trunk', (x, y, 1.3), .1, 1.2, WOOD, 8)
    LeafCloud('Dense irregular cypress', (x, y, 1.25), (.72, .72, 4), 420, True)


def LeafCloud(name, center, dimensions, count, cypress=False):
  vertices, faces, colors = [], [], []
  for _ in range(count):
    if cypress:
      height = RNG.random()
      radius = math.sin(math.pi * height ** .8) ** .65
      angle = RNG.random() * 2 * math.pi
      distance = radius * RNG.random() ** .3
      point = Vector((center[0] + math.cos(angle) * distance * dimensions[0],
          center[1] + math.sin(angle) * distance * dimensions[1], center[2] + height * dimensions[2]))
      scale = (.12, .12, .22)
    else:
      u, v = RNG.uniform(-1, 1), RNG.uniform(-1, 1)
      point = Vector((center[0] + u * dimensions[0], center[1] + v * dimensions[1],
          center[2] + RNG.random() * dimensions[2] * (1 - .35 * (u * u + v * v))))
      scale = (.12, .12, .1)
    first = len(vertices)
    vertices += [tuple(point + Vector((a * scale[0], b * scale[1], c * scale[2])))
        for a, b, c in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))]
    leaf_faces = ((0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4),
        (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5))
    faces += [tuple(first + i for i in face) for face in leaf_faces]
    colors += [RNG.randrange(len(LEAVES))] * len(leaf_faces)
  obj = Mesh(name, vertices, faces, LEAVES[0])
  for material in LEAVES[1:]:
    obj.data.materials.append(material)
  for face, color in zip(obj.data.polygons, colors):
    face.material_index = color
    face.use_smooth = True


def Lamp(x, y):
  Cylinder('Lamp plinth', (x, y, .2), .22, .4, TRIM)
  Cylinder('Bronze lamp post', (x, y, 1.7), .065, 3, BRONZE, 12)
  Cylinder('Lamp collar', (x, y, 3.1), .18, .14, BRONZE, 12)
  Foliage('Frosted lamp globe', (x, y, 3.4), (.23, .23, .3), IVORY)
  Cylinder('Lamp cap', (x, y, 3.68), .2, .15, BRONZE, 12, top_radius=0)


def BuildCityHall():
  wing = (-11, -11, -2, 11)
  council = (-2, -11, 10.5, 1.5)
  tower = (-3.4, -1.4, 1, 3)
  Box('Three-storey administrative wing', (-6.5, 0, 5.4), (9, 22, 10.8), STONE, .06)
  Box('Council chamber', (4.25, -4.75, 4.7), (12.5, 12.5, 9.4), STONE, .06)
  PaintedWing(wing)
  PaintedBlock(council, 9.4, PAINTED_COUNCIL, (35 / 1448, 25 / 1086, 1415 / 1448, 1046 / 1086))
  for bounds, floors in ((wing, (.35, 3.55, 6.95, 10.65)), (council, (.35, 4.7, 9.3))):
    for z in floors:
      Band('Continuous carved string course', bounds, z, .22 if z > 1 else .62, .14, TRIM)
    Band('Deep upper cornice', bounds, floors[-1] + .2, .22, .28, TRIM)

  # The dark roof is this one immutable rectangle in every camera view.
  Box('One rectangular slate roof', (-6.5, 0, 11.04), (8.6, 21.6, .12), SLATE, 0)
  for y in range(-10, 11):
    Line('Fine slate roof courses', [(-10.75, y, 11.105), (-2.25, y, 11.105)], .012, DARK)
  for x in range(-10, -2):
    Line('Slate panel joints', [(x, -10.7, 11.105), (x, 10.7, 11.105)], .009, DARK)
  for x in (-10.85, -2.15):
    Box('Wing parapet', (x, 0, 11.16), (.25, 22, .55), STONE)
    Box('Parapet coping', (x, 0, 11.47), (.4, 22.2, .14), TRIM)
  for y in (-10.85, 10.85):
    Box('Wing parapet', (-6.5, y, 11.16), (9, .25, .55), STONE)
    Box('Parapet coping', (-6.5, y, 11.47), (9.2, .4, .14), TRIM)
  for y in (-7, 7):
    Box('Fixed rooftop vent', (-8.7, y, 11.3), (1.4, 1.4, .8), STONE)
    Box('Vent copper cap', (-8.7, y, 11.75), (1.55, 1.55, .12), COPPER_EDGE)
  for y in (-4.7, 4.7):
    Box('Skylight kerb', (-5.8, y, 11.1), (1.4, 2, .3), BRONZE)
    Box('Skylight glazing', (-5.8, y, 11.29), (1.22, 1.82, .1), GLASS)
    Box('Skylight divider', (-5.8, y, 11.36), (.065, 1.9, .065), COPPER_EDGE)
  HipRoof('Copper council roof', (-2.35, -11.35, 10.85, 1.85), 9.85, 14.1, 2.1, COPPER)
  Box('Council roof lantern', (4.25, -4.75, 14.65), (1.65, 1.65, 1.1), STONE)
  for axis in range(2):
    for sign in (-1, 1):
      wall = (4.25 if axis == 0 else -4.75) + sign * .85
      along = -4.75 if axis == 0 else 4.25
      WallBox('Lantern opening', axis, wall, along, 14.7, .8, .04, .65, DARK, sign)
  HipRoof('Council cupola roof', (3.15, -5.85, 5.35, -3.65), 15.22, 16.2, .08, COPPER)

  Box('Single clock tower', (-1.2, .8, 12), (4.4, 4.4, 24), STONE, .045)
  PaintedBlock(tower, 24, PAINTED_TOWER, (300 / 932, 18 / 1688, 630 / 932, 1668 / 1688))
  for z in (.4, 10.9, 23.7, 24.05):
    Band('Tower carved cornice', tower, z, .25 if z < 23 else .3, .18 if z < 23 else .35, TRIM)
  for axis in range(2):
    for sign in (-1, 1):
      wall = (-1.2 if axis == 0 else .8) + sign * 2.2
      along = .8 if axis == 0 else -1.2
      Clock(axis, wall, along, sign)
      for offset in (-1.85, 1.85):
        WallBox('Tower corner pier', axis, wall, along + offset, 12, .32, .15, 22.8, TRIM, sign)
  HipRoof('Tower lower copper roof', (-4, -2, 1.6, 3.6), 24.3, 28, 3, COPPER)
  Cylinder('Octagonal belfry floor', (-1.2, .8, 28.1), 1.65, .3, TRIM, 8)
  Cylinder('Dark bronze bell', (-1.2, .8, 29.3), .65, 1.3, BRONZE, 16, top_radius=.4)
  for i in range(8):
    angle = i * math.pi / 4
    Cylinder('Open belfry column', (-1.2 + 1.35 * math.cos(angle), .8 + 1.35 * math.sin(angle), 29.55),
        .13, 2.6, STONE, 8)
  Cylinder('Belfry cornice', (-1.2, .8, 30.92), 1.72, .25, TRIM, 8)
  Cylinder('Octagonal copper spire', (-1.2, .8, 32.2), 1.9, 2.5, COPPER_EDGE, 8, top_radius=.1)
  Cylinder('Finial stem', (-1.2, .8, 34), .055, 1.5, BRONZE, 12)
  Foliage('Gold finial', (-1.2, .8, 34.85), (.18, .18, .18), GOLD)

  # Public entrance is unique. The opposite facade has a small service door.
  Box('Entrance canopy cornice', (-.8, 6.2, 3.55), (2.8, 4.1, .25), TRIM)
  Box('Patinated canopy roof', (-.8, 6.2, 3.73), (2.9, 4.2, .12), COPPER_EDGE)
  for y in (4.5, 7.9):
    Box('Entrance limestone column', (.3, y, 1.9), (.32, .32, 3.2), STONE)
  for i in range(6):
    height = .72 - i * .12
    Box('Broad entrance step', (.65 + i * .48, 6.2, height / 2), (.5, 3.65, height), TRIM)
  for y in (4.3, 8.1):
    Line('Bronze stair handrail', [(.65, y, 1.6), (3.1, y, .9)], .045, BRONZE)
    for x, z in ((.65, 1.6), (3.1, .9)):
      Line('Handrail upright', [(x, y, .15), (x, y, z)], .045, BRONZE)
  WallBox('Small rear service door', 0, -11, -5.5, 1.25, 1.25, .12, 2.5, WOOD, -1)
  Box('Service doorstep', (-11.4, -5.5, .12), (.7, 1.8, .24), TRIM)
  for x in (4, 6, 8):
    for y in (5.2, 7.2):
      Box('Forecourt paving tile', (x, y, .035), (1.97, 1.97, .07), PAVING, .008)
  for y in (-9.2, -6.8, 7.5, 9.9):
    Planter(-12.1, y, 1.25, 2.1, y in (-9.2, 9.9))
  for y in (-9.5, -5.2, -.7):
    Planter(11.65, y, 1.25, 2.7, True)
  for x, y in ((1.8, 10.2), (6.4, 10.2), (10.5, 4), (10.5, 9.8)):
    Planter(x, y, 2.5, 1.3, x == 10.5)
  Lamp(3.8, 3.6)
  Lamp(3.8, 8.8)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--samples', type=int, default=48)
  parser.add_argument('--detail', type=int, default=3)
  parser.add_argument('--views', type=int, nargs='+', default=[0, 1, 2, 3])
  parser.add_argument('--square-guides', action='store_true',
      help='Render clockwise geometry guides on an exact square site, with the preferred front layout')
  args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
  bpy.ops.object.select_all(action='SELECT')
  bpy.ops.object.delete(use_global=False)
  global STONE, TRIM, COPPER, COPPER_EDGE, SLATE, DARK, GLASS, BRONZE, WOOD, INTERIOR
  global IVORY, GOLD, SOIL, LEAVES, PAVING, BANNER, PAINTED_WING, PAINTED_COUNCIL, PAINTED_TOWER
  PAINTED_WING = PaintedMaterial('ImageGen detailed administrative elevations', 'facade-front.png')
  PAINTED_COUNCIL = PaintedMaterial('ImageGen carved council chamber', 'facade-council.png')
  PAINTED_TOWER = PaintedMaterial('ImageGen detailed clock tower stonework', 'facade-tower.png')
  STONE = Material('ImageGen limestone ashlar', (.66, .56, .41), texture=MATERIALS / 'limestone.png')
  TRIM = Material('Carved ivory limestone', (.58, .49, .38))
  COPPER = Material('ImageGen aged copper', (.19, .38, .33), .52, .3, MATERIALS / 'copper.png')
  copper_image = next(node for node in COPPER.node_tree.nodes if node.type == 'TEX_IMAGE')
  copper_image.projection = 'FLAT'
  copper_uv = COPPER.node_tree.nodes.new('ShaderNodeTexCoord')
  COPPER.node_tree.links.new(copper_uv.outputs['UV'], copper_image.inputs['Vector'])
  COPPER_EDGE = Material('Standing seam edges', (.27, .45, .39), .5, .35)
  SLATE = Material('Charcoal slate roofing', (.055, .068, .08), .85)
  DARK = Material('Deep window recess', (.012, .017, .02))
  GLASS = Material('Blue gray glass', (.035, .075, .09), .25, .25)
  BRONZE = Material('Dark aged bronze', (.095, .075, .04), .42, .65)
  WOOD = Material('Walnut doors', (.12, .065, .028))
  INTERIOR = Material('Warm interior shutters', (.46, .29, .11))
  IVORY = Material('Clock enamel and lamp glass', (.85, .82, .69), .45)
  GOLD = Material('Brass finials', (.64, .41, .11), .28, .7)
  SOIL = Material('Planter soil', (.07, .045, .025))
  PAVING = Material('Forecourt sandstone', (.57, .52, .42), .9)
  BANNER = Material('Deep blue civic banners', (.025, .09, .16), .95)
  LEAVES = [Material('Foliage ' + str(i), c) for i, c in enumerate(
      ((.025, .075, .012), (.07, .13, .022), (.12, .2, .038)))]
  BuildCityHall()
  root = bpy.data.objects.new('City Hall: one coherent building for every rotation', None)
  bpy.context.collection.objects.link(root)
  for obj in list(bpy.context.scene.objects):
    if obj != root:
      obj.parent = root
  if args.square_guides:
    Box('Exact three by three tile foundation guide', (0, 0, -.025), (30, 30, .05), PAVING, 0)
  scene = bpy.context.scene
  scene.render.engine = 'CYCLES'
  scene.cycles.samples = args.samples
  scene.cycles.use_denoising = True
  scene.render.threads_mode = 'FIXED'
  scene.render.threads = 6
  scene.render.resolution_x = WIDTH * args.detail
  scene.render.resolution_y = HEIGHT * args.detail
  scene.render.resolution_percentage = 100
  scene.render.film_transparent = True
  scene.render.image_settings.file_format = 'PNG'
  scene.render.image_settings.color_mode = 'RGBA'
  scene.view_settings.view_transform = 'Standard'
  scene.view_settings.exposure = .2
  scene.world.use_nodes = True
  scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value = (1, .97, .92, 1)
  scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value = .6
  bpy.ops.object.light_add(type='SUN', location=(-35, 10, 65))
  light = bpy.context.object
  light.name = 'Fixed screen-upper-left sunlight'
  light.data.energy = 1.2
  light.data.color = (1, .92, .8)
  light.data.angle = math.radians(12)
  light.rotation_euler = Vector((35, -10, -65)).to_track_quat('-Z', 'Y').to_euler()
  # 30-degree camera elevation gives +/-26.565-degree ground axes: exactly 2:1.
  pixels_per_metre = WIDTH / (30 * math.sqrt(2))
  target_z = (HEIGHT / 2 - WIDTH / 4) / (math.cos(math.pi / 6) * pixels_per_metre)
  target = Vector((0, 0, target_z))
  bpy.ops.object.camera_add(location=target + Vector((40, 40, 40 * math.sqrt(2 / 3))))
  camera = bpy.context.object
  camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
  camera.data.type = 'ORTHO'
  camera.data.ortho_scale = HEIGHT / pixels_per_metre
  scene.camera = camera
  OUTPUT.mkdir(parents=True, exist_ok=True)
  render_directory = ROOT / 'build/art-reference' / (
      'civic-square-guides' if args.square_guides else 'civic-city-hall')
  render_directory.mkdir(parents=True, exist_ok=True)
  bpy.ops.file.pack_all()
  if not args.square_guides:
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / 'city-hall.blend'))
  for view in args.views:
    if args.square_guides:
      # Reflect the base plan across x=y once to match the preferred front design.
      # All following angles are genuine clockwise rotations of that same plan.
      base = Matrix(((0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
      root.matrix_world = Matrix.Rotation(-view * math.pi / 2, 4, 'Z') @ base
    else:
      root.rotation_euler.z = view * math.pi / 2
    bpy.context.view_layer.update()
    for point in ((-15, -15, 0), (-15, 15, 0), (15, 15, 0), (15, -15, 0)):
      p = world_to_camera_view(scene, camera, Vector(point))
      print('FOOTPRINT', view, round(p.x * WIDTH, 5), round((1 - p.y) * HEIGHT, 5), flush=True)
    angle = view * 90 if args.square_guides else view
    scene.render.filepath = str(render_directory / f'view-{angle}.png')
    bpy.ops.render.render(write_still=True)
    StripPngMetadata(Path(scene.render.filepath))
    print('RENDERED', view, scene.render.filepath, flush=True)


if __name__ == '__main__':
  main()
