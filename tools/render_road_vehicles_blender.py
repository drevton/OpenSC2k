#!/usr/bin/env python3
"""Original road vehicles with a shared orthographic map camera and ground anchor.

Run explicitly with Blender; normal game builds only pack the baked sprites.
blender --background --python tools/render_road_vehicles_blender.py -- --samples 32
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

sys.path.insert(0, str(Path(__file__).resolve().parent))
from strip_png_metadata import StripPngMetadata

ROOT = Path(__file__).resolve().parents[1] / 'assets/landscape/vehicles/blender'
MODELS = [
    ('hatchback', .380, .175, .127, -.39, .26),
    ('sedan', .425, .182, .127, -.29, .25),
    ('wagon', .445, .185, .136, -.44, .25),
    ('van', .445, .185, .148, -.45, .30),
    ('pickup', .460, .190, .140, -.04, .29),
    ('suv', .440, .190, .147, -.43, .27),
    ('coupe', .430, .180, .113, -.31, .25),
]
MODEL_GEOMETRY = {
  'hatchback': (.0325, .085, -.32, .31, -.25, .12),
  'sedan': (.0325, .087, -.32, .32, -.15, .10),
  'wagon': (.0330, .090, -.33, .32, -.36, .10),
  'van': (.0355, .099, -.34, .33, -.38, .19),
  'pickup': (.0355, .094, -.34, .32, -.015, .16),
  'suv': (.0360, .098, -.33, .32, -.35, .13),
  'coupe': (.0330, .084, -.32, .32, -.12, .085),
}
DIAGONAL = math.degrees(math.atan(.5))
ANGLES = []
for start, end, count in [(-90, -DIAGONAL, 8), (-DIAGONAL, 0, 4), (0, DIAGONAL, 4),
    (DIAGONAL, 90, 8), (90, 180 - DIAGONAL, 8), (180 - DIAGONAL, 180, 4),
    (180, 180 + DIAGONAL, 4), (180 + DIAGONAL, 270, 8)]:
  ANGLES += [start + (end - start) * i / count for i in range(count)]
STEERING = [0, -35, 35]


def Material(name, rgb, metallic=0, roughness=.4, emission=0):
  material = bpy.data.materials.new(name)
  material.diffuse_color = (*rgb, 1)
  material.use_nodes = True
  shader = material.node_tree.nodes.get('Principled BSDF')
  shader.inputs['Base Color'].default_value = (*rgb, 1)
  shader.inputs['Metallic'].default_value = metallic
  shader.inputs['Roughness'].default_value = roughness
  if emission:
    shader.inputs['Emission Color'].default_value = (*rgb, 1)
    shader.inputs['Emission Strength'].default_value = emission
  return material


def Mesh(name, vertices, faces, materials, indices=None, bevel=0):
  mesh = bpy.data.meshes.new(name)
  mesh.from_pydata(vertices, [], faces)
  mesh.update()
  obj = bpy.data.objects.new(name, mesh)
  bpy.context.collection.objects.link(obj)
  for material in materials: mesh.materials.append(material)
  if indices:
    for face, material in zip(mesh.polygons, indices): face.material_index = material
  if bevel:
    modifier = obj.modifiers.new('Rounded manufactured edges', 'BEVEL')
    modifier.width, modifier.segments = bevel, 3
    obj.modifiers.new('Weighted surface normals', 'WEIGHTED_NORMAL')
  return obj


def Box(name, center, size, material, bevel=.003):
  bpy.ops.mesh.primitive_cube_add(size=1, location=center)
  obj = bpy.context.object
  obj.name, obj.dimensions = name, size
  bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
  obj.data.materials.append(material)
  if bevel:
    modifier = obj.modifiers.new('Soft edge', 'BEVEL')
    modifier.width, modifier.segments = bevel, 3
    obj.modifiers.new('Weighted normals', 'WEIGHTED_NORMAL')
  return obj


def Line(name, points, material, radius=.0016):
  curve = bpy.data.curves.new(name, 'CURVE')
  curve.dimensions, curve.bevel_depth, curve.bevel_resolution = '3D', radius, 2
  spline = curve.splines.new('POLY')
  spline.points.add(len(points) - 1)
  for point, coordinate in zip(spline.points, points): point.co = (*coordinate, 1)
  obj = bpy.data.objects.new(name, curve)
  bpy.context.collection.objects.link(obj)
  curve.materials.append(material)
  return obj


def Cylinder(name, center, radius, depth, material):
  bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=radius, depth=depth,
      location=center, rotation=(math.pi / 2, 0, 0))
  obj = bpy.context.object
  obj.name = name
  obj.data.materials.append(material)
  modifier = obj.modifiers.new('Rounded tyre or rim lip', 'BEVEL')
  modifier.width, modifier.segments = .0025, 3
  for polygon in obj.data.polygons: polygon.use_smooth = True
  obj.modifiers.new('Weighted normals', 'WEIGHTED_NORMAL')
  return obj


def Profile(points, x):
  # Monotone cubic sections keep bonnets, shoulders and roofs smoothly curved
  # without overshooting a roof or dipping below the wheel clearance.
  slopes = [(b[1] - a[1]) / (b[0] - a[0]) for a, b in zip(points, points[1:])]
  tangents = [slopes[0]]
  for left, right in zip(slopes, slopes[1:]):
    tangents.append(0 if left * right <= 0 else 2 * left * right / (left + right))
  tangents.append(slopes[-1])
  for i, (a, b) in enumerate(zip(points, points[1:])):
    if x <= b[0] or i == len(points) - 2:
      t = max(0, min(1, (x - a[0]) / (b[0] - a[0])))
      return ((2*t**3 - 3*t*t + 1) * a[1] + (t**3 - 2*t*t + t) * (b[0] - a[0]) * tangents[i]
          + (-2*t**3 + 3*t*t) * b[1] + (t**3 - t*t) * (b[0] - a[0]) * tangents[i+1])
  return points[-1][1]


def Surface(name, point, u0, u1, v0, v1, material, steps=24, across=12):
  vertices = [point(u0 + (u1-u0)*i/steps, v0 + (v1-v0)*j/across)
      for i in range(steps+1) for j in range(across+1)]
  faces = [(i*(across+1)+j, (i+1)*(across+1)+j, (i+1)*(across+1)+j+1, i*(across+1)+j+1)
      for i in range(steps) for j in range(across)]
  obj = Mesh(name, vertices, faces, [material])
  for polygon in obj.data.polygons: polygon.use_smooth = True
  return obj


def BuildCar(spec, materials):
  name, length, width, height, rear, front = spec
  paint, glass, rubber, alloy, trim, headlamp, tail, indicator = materials
  existing = set(bpy.context.scene.objects)
  root = bpy.data.objects.new(name, None)
  bpy.context.collection.objects.link(root)
  # Model-specific belt heights and axle/roof positions are proportions, not
  # silhouette fitting. All types keep the same world scale and lane anchor.
  radius, belt, axle_rear, axle_front, roof_rear, roof_front = MODEL_GEOMETRY[name]
  rear_axle, front_axle = axle_rear * length, axle_front * length
  square_back = name in ('wagon', 'van', 'suv')
  rear_top = belt * (.94 if square_back else .88)
  front_top = belt * .83
  top_profile = [(-.5, rear_top), (-.43, belt-.002), (-.30, belt),
      (.12, belt), (.34, belt-.004), (.46, front_top+.006), (.5, front_top)]
  end_width = .88 if square_back else .84
  breadth_profile = [(-.5, end_width), (-.46, .965), (-.29, 1),
      (.22, 1), (.45, .96), (.5, .84)]
  def BodyTop(x): return Profile(top_profile, x / length)
  def HalfWidth(x): return width / 2 * Profile(breadth_profile, x / length)
  def BedDepth(x):
    if name != 'pickup': return 0
    u = x / length
    return .019 * min(1, max(0, (u+.46)/.025)) * min(1, max(0, (-.045-u)/.025))
  vertices = []
  sections = 112
  for step in range(sections + 1):
    x = (-.5 + step / sections) * length
    top, half, bed = BodyTop(x), HalfWidth(x), BedDepth(x)
    # Keep the uncut skin smooth; the wheel wells are cut afterwards. Bending
    # every lower section upwards at an arch would pinch the whole side panel.
    ring = [(-.80, .025), (-.96, .035), (-1, top-.023), (-.985, top-.012),
        (-.92, top-.004), (-.79, top), (-.72, top+.001-bed), (-.40, top+.003-bed),
        (0, top+.004-bed), (.40, top+.003-bed), (.72, top+.001-bed), (.79, top),
        (.92, top-.004), (.985, top-.012), (1, top-.023), (.96, .035), (.80, .025)]
    vertices += [(x, y * half, z) for y, z in ring]
  count = len(ring)
  faces = [tuple(reversed(range(count))), tuple(range(sections*count, (sections+1)*count))]
  for section in range(sections):
    for j in range(count):
      faces.append((section*count+j, (section+1)*count+j,
          (section+1)*count+(j+1)%count, section*count+(j+1)%count))
  body = Mesh(name + ' formed body panels', vertices, faces, [paint, trim])
  for polygon in body.data.polygons: polygon.use_smooth = polygon.index >= 2
  arch_radius = radius + .003
  for axle in (rear_axle, front_axle):
    for side in (-1, 1):
      bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=arch_radius, depth=.048,
          location=(axle, side * (width/2-.006), radius), rotation=(math.pi/2, 0, 0))
      cutter = bpy.context.object
      cutter.data.materials.append(paint)
      cutter.data.materials.append(trim)
      for polygon in cutter.data.polygons: polygon.material_index = 1
      bpy.context.view_layer.objects.active = body
      modifier = body.modifiers.new('Recessed wheel well', 'BOOLEAN')
      modifier.operation, modifier.solver, modifier.object = 'DIFFERENCE', 'EXACT', cutter
      bpy.ops.object.modifier_apply(modifier=modifier.name)
      bpy.data.objects.remove(cutter, do_unlink=True)
  def BodySide(x, z, side, offset=0):
    top = BodyTop(x)
    contour = [(.025, .80), (.035, .96), (top-.023, 1), (top-.012, .985), (top-.004, .92)]
    return (x, side * (HalfWidth(x) * Profile(contour, z) + offset), z)
  lower_rear, lower_front = rear * length, front * length
  upper_rear, upper_front = roof_rear * length, roof_front * length
  roof_profile = [(lower_rear, BodyTop(lower_rear)-.004), (upper_rear, height-.004),
      (upper_rear+.010, height), (upper_front-.010, height), (upper_front, height-.003),
      (lower_front, BodyTop(lower_front)-.004)]
  roof_width = [(lower_rear, width*.440), (upper_rear, width*.350),
      (upper_front, width*.345), (lower_front, width*.435)]
  def Roof(x, v, lift=0):
    return (x, v*Profile(roof_width, x), Profile(roof_profile, x)+.004*(1-v*v)+lift)
  Surface('Integrated curved roof and pillars', Roof, lower_rear, lower_front, -1, 1, paint, 64, 20)
  for label, x0, x1 in [('Rear windscreen', lower_rear+.004, upper_rear-.003),
      ('Front windscreen', upper_front+.003, lower_front-.005)]:
    Surface(label+' seal', lambda x,v: Roof(x,v,.0004), x0, x1, -.91, .91, trim)
    Surface(label+' glass', lambda x,v: Roof(x,v,.0008), x0+.0012, x1-.0012, -.87, .87, glass)
  for side in (-1, 1):
    def Side(x, t, lift=0):
      edge = Roof(x, side)
      base = HalfWidth(x) * .96
      y = base*(1-t) + abs(edge[1])*t + .0008*math.sin(math.pi*t)
      bottom = BodyTop(x)-.009
      return (x, side*(y+lift), bottom+(edge[2]-bottom)*t)
    Surface('Blended cabin flank', Side, lower_rear, lower_front, 0, 1, paint, 64, 12)
    span = lower_front-lower_rear
    division = lower_rear + span*(.28 if name=='coupe' else .55)
    splits = [lower_rear+.010, division, lower_front-.012]
    if name in ('van', 'wagon', 'suv'):
      splits = [lower_rear+.009, lower_rear+span*.30, division, lower_front-.012]
    if name == 'pickup': splits = [lower_rear+.008, lower_front-.012]
    for a, b in zip(splits, splits[1:]):
      Surface('Side window seal', lambda x,t: Side(x,t,.0004), a+.0015,b-.0015,.16,.90,trim)
      Surface('Side glass', lambda x,t: Side(x,t,.0008), a+.0028,b-.0028,.21,.86,glass)
    doors = [lower_rear+.015, division, lower_front-.006]
    if name in ('coupe', 'pickup'): doors = [lower_rear+.012, lower_front-.006]
    for door in doors:
      top, bottom = BodyTop(door), .036
      for axle in (rear_axle, front_axle):
        distance = abs(door-axle)
        if distance < arch_radius:
          bottom = max(bottom, radius+math.sqrt(arch_radius**2-distance**2)+.001)
      if bottom < top-.007:
        Line('Door shut line', [BodySide(door, bottom+(top-.007-bottom)*i/12, side, .0002)
            for i in range(13)], trim, .00032)
    handles = [division+.014, lower_rear+span*.22]
    if name in ('coupe', 'pickup'): handles = [lower_rear+.026]
    for handle_x in handles:
      position = BodySide(handle_x, BodyTop(handle_x)-.018, side, .0007)
      Box('Door handle', position, (.012,.0018,.0024), alloy, .0007)
    Box('Door mirror', (lower_front-.012,side*width*.505,BodyTop(lower_front)+.001),
        (.014,.010,.007),paint,.003)
    Line('Rocker moulding', [BodySide(-length*.38+length*.76*i/40,.033,side,.0003)
        for i in range(41)],trim,.0008)

  for x in (rear_axle,front_axle):
    for side in (-1,1):
      y = side*(width/2-.011)
      before_wheel = set(bpy.context.scene.objects)
      # A rounded tyre shoulder and a recessed rim avoid the exposed toy-wheel look.
      bpy.ops.mesh.primitive_torus_add(major_segments=40,minor_segments=12,
          location=(x,y,radius),rotation=(math.pi/2,0,0),
          major_radius=radius-.006,minor_radius=.006)
      tyre=bpy.context.object
      tyre.name='Rounded radial tyre'
      tyre.data.materials.append(rubber)
      for polygon in tyre.data.polygons: polygon.use_smooth=True
      Cylinder('Tyre sidewall',(x,y,radius),radius-.004,.015,rubber)
      face=y+side*.008
      Cylinder('Recessed alloy rim',(x,face,radius),radius*.64,.0015,alloy)
      Cylinder('Wheel dish',(x,face+side*.001,radius),radius*.50,.001,trim)
      for spoke in range(7):
        angle=spoke*math.tau/7
        Line('Slim alloy spoke',[(x,face+side*.0017,radius),
            (x+radius*.54*math.cos(angle),face+side*.0017,radius+radius*.54*math.sin(angle))],alloy,.0011)
      Cylinder('Wheel centre',(x,face+side*.002,radius),radius*.19,.0015,alloy)
      if x == front_axle:
        parts = set(bpy.context.scene.objects) - before_wheel
        pivot = bpy.data.objects.new('Front steering pivot', None)
        bpy.context.collection.objects.link(pivot)
        pivot.location = (x, y, radius)
        for part in parts:
          part.parent = pivot
          part.location -= Vector((x, y, radius))
      # Rolled fender lips follow each actual cutout, with a dark inner wheel well.
      for material, r, y_offset, thickness in [(trim,arch_radius-.001,-.001,.0014),
          (paint,arch_radius+.0007,.0003,.0014)]:
        points=[]
        for i in range(33):
          angle=math.pi*i/32
          px=x+r*math.cos(angle)
          half=width/2*Profile(breadth_profile,px/length)
          points.append((px,side*(half+y_offset),radius+r*math.sin(angle)))
        Line('Rolled wheel arch',points,material,thickness)

  # Front and rear fittings sit on the actual closed end panels. Their
  # heights follow each body's nose/deck, so they cannot float above it.
  for end in (-1, 1):
    end_top = BodyTop(end*length*.5)
    face_x = end*length*.501
    def Bumper(v, t):
      return (face_x+end*.0007*(1-v*v), v*width*.385, .033+t*.009)
    Surface('Body coloured bumper face', Bumper, -1, 1, 0, 1, paint, 32, 4)
    Line('Bumper separation', [(face_x, v*width*.38, .044) for v in (-1, 0, 1)],trim,.0005)
    Box('Registration plate',(face_x+end*.001,0,.048),(.0015,.026,.007),alloy,.0006)
    lamp_z = end_top-.014
    for side in (-1, 1):
      lamp_y = side*width*.255
      Box('Inset lamp housing',(face_x,lamp_y,lamp_z),(.002,width*.225,.013),trim,.0015)
      Box('Headlamp lens' if end==1 else 'Tail lamp lens',
          (face_x+end*.0012,lamp_y,lamp_z),(.0015,width*.205,.010),
          headlamp if end==1 else tail,.0015)
      Box('Amber indicator', (face_x+end*.002, side*width*.330, lamp_z),
          (.0015,width*.050,.009), indicator,.001)
      if end==-1:
        Box('Reverse lamp',(face_x-.002,side*width*.185,lamp_z),
            (.0015,width*.040,.007),headlamp,.001)
    if end==1:
      Box('Radiator grille',(face_x+.001,0,lamp_z),(.002,width*.235,.011),trim,.001)
      for z in (lamp_z-.003, lamp_z+.003):
        Line('Grille slat',[(face_x+.002,-width*.10,z),(face_x+.002,width*.10,z)],alloy,.00045)
      Box('Lower air intake',(face_x+.001,0,.036),(.002,width*.35,.005),trim,.001)
  if name in ('wagon','suv'):
    for side in (-1,1):
      Line('Low roof rail',[(upper_rear+.012,side*width*.285,height+.004),
          (upper_front-.008,side*width*.285,height+.004)],trim,.0018)
  if name=='pickup':
    floor = belt-.014
    for strip in range(-3,4):
      Line('Load bed floor rib',[(-length*.42,strip*width*.078,floor),
          (-length*.075,strip*width*.078,floor)],trim,.0007)
    for side in (-1,1):
      Line('Bed rail cap',[(-length*.43,side*width*.40,belt+.001),
          (-length*.055,side*width*.40,belt+.001)],trim,.0013)
  for obj in set(bpy.context.scene.objects)-existing:
    if obj!=root and obj.parent is None: obj.parent=root
  return root


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--samples', type=int, default=32)
  parser.add_argument('--review', action='store_true', help='Large front, side and rear studio views')
  parser.add_argument('--pitch', type=float, default=0, help='Degrees of body pitch, around the ground anchor')
  parser.add_argument('--heading', type=float,
      help='Render one custom screen-space heading instead of the 48 game views')
  parser.add_argument('--model', choices=[model[0] for model in MODELS])
  parser.add_argument('--output', type=Path, help='Separate directory for custom angle/pitch studies')
  args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
  preview = (args.review or args.model is not None or args.heading is not None or
      args.pitch != 0 or args.output is not None)
  output = args.output or (Path('/tmp/sc2k-road-vehicle-review') if args.review else
      Path('/tmp/sc2k-road-vehicle-preview') if preview else ROOT)
  output.mkdir(parents=True, exist_ok=True)
  bpy.context.preferences.filepaths.save_version = 0
  bpy.ops.object.select_all(action='SELECT')
  bpy.ops.object.delete(use_global=False)
  materials = [Material('Recolourable body paint', (.8, .008, .38), .42, .24),
      Material('Opaque tinted glass', (.075, .105, .125), .45, .14),
      Material('Tyre rubber', (.016, .019, .022), 0, .65),
      Material('Silver alloy', (.55, .59, .63), .75, .24), Material('Dark trim', (.022, .027, .032), .15, .42),
      Material('Headlights', (.65, .70, .74), .35, .20), Material('Tail lights', (.55, .012, .008), .1, .3),
      Material('Indicator lens', (.75, .27, .025), .1, .26)]
  roots = [BuildCar(model, materials) for model in MODELS]
  scene = bpy.context.scene
  scene.render.engine = 'CYCLES'
  scene.cycles.device, scene.cycles.samples = 'CPU', args.samples
  scene.cycles.use_denoising = True
  scene.render.film_transparent = True
  scene.render.resolution_x = scene.render.resolution_y = 256
  scene.render.resolution_percentage = 100
  scene.render.image_settings.file_format, scene.render.image_settings.color_mode = 'PNG', 'RGBA'
  scene.render.use_persistent_data = True
  scene.view_settings.view_transform = 'Standard'
  scene.world.use_nodes = True
  scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.65, .70, .80, 1)
  scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = .55
  for name, location, energy, size in [('Key', (-2, -3, 5), 240, 3), ('Fill', (4, 1, 4), 100, 4)]:
    bpy.ops.object.light_add(type='AREA', location=location)
    light = bpy.context.object
    light.name, light.data.energy, light.data.shape, light.data.size = name, energy, 'DISK', size
    light.rotation_euler = (-light.location).to_track_quat('-Z', 'Y').to_euler()
  bpy.ops.object.camera_add(location=(6, 6, 6 * math.sqrt(2 / 3)))
  camera = bpy.context.object
  camera.rotation_euler = (-camera.location).to_track_quat('-Z', 'Y').to_euler()
  camera.data.type, camera.data.ortho_scale = 'ORTHO', 64 / (128 / math.sqrt(2))
  camera.location += camera.rotation_euler.to_quaternion() @ Vector((0, 16 / (128 / math.sqrt(2)), 0))
  scene.camera = camera
  bpy.context.view_layer.update()
  # Freeze both the origin and world-axis projection, rather than fitting a
  # silhouette after rendering. This also guards the handedness of the headings.
  for coordinate, expected in [((0, 0, 0), (32, 48)), ((1, 0, 0), (-32, 80)), ((0, 1, 0), (96, 80))]:
    point = world_to_camera_view(scene, camera, Vector(coordinate))
    actual = point.x * 64, (1 - point.y) * 64
    if any(abs(a - b) > .001 for a, b in zip(actual, expected)):
      raise ValueError(f'Camera registration changed: {coordinate}: {actual} != {expected}')
  if args.review:
    # A separate studio camera exposes proportion, panel and attachment errors
    # at useful size. The registered map camera above remains unchanged.
    scene.render.resolution_x, scene.render.resolution_y = 960, 600
    camera.location = (3, 3, 2.52)
    camera.rotation_euler = (Vector((0, 0, .075))-camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.ortho_scale = .56
    materials[0].node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (.09, .19, .29, 1)
  for root in roots: root.hide_render = True
  # Children do not inherit hide_render from an empty; explicitly select the visible model.
  all_objects = [(root, list(root.children_recursive)) for root in roots]
  for root, children in all_objects:
    for child in children: child.hide_render = True
  for root, children in all_objects:
    for child in children:
      child.hide_render = root != roots[0]
      child.hide_set(root != roots[0])
  bpy.ops.wm.save_as_mainfile(filepath=str(output / 'vehicles.blend'))
  for root, children in all_objects:
    for child in children: child.hide_render = True
  frames = []
  for model, (root, children) in enumerate(all_objects):
    if args.model and args.model != MODELS[model][0]: continue
    root.hide_render = False
    for child in children: child.hide_render = False
    headings = [0, -45, 180] if args.review else ANGLES if args.heading is None else [args.heading]
    pivots = [part for part in children if part.type == 'EMPTY' and part.name.startswith('Front steering pivot')]
    for steering, turn in enumerate([0] if preview else STEERING):
      for pivot in pivots: pivot.rotation_euler.z = math.radians(turn)
      for view, angle in enumerate(headings):
        sx, sy = math.cos(math.radians(angle)), math.sin(math.radians(angle))
        u, v = (2 * sy - sx) / 2, (2 * sy + sx) / 2
        yaw = math.radians(angle) if args.review else math.atan2(v, u)
        root.rotation_euler = (0, math.radians(args.pitch), yaw)
        slot = (model * len(STEERING) + steering) * len(ANGLES) + view
        name = f'car-{slot:04d}.png' if args.heading is None else f'custom-{model}-{angle:g}.png'
        if args.review: name = f"{MODELS[model][0]}-{('front', 'side', 'rear')[view]}.png"
        scene.render.filepath = str(output / name)
        bpy.ops.render.render(write_still=True)
        StripPngMetadata(Path(scene.render.filepath))
        frames.append(dict(model=model, steering=steering, view=view, file=name, screen_angle=angle,
            sha256=hashlib.sha256((output / name).read_bytes()).hexdigest()))
      for pivot in pivots: pivot.rotation_euler.z = 0
    for child in children: child.hide_render = True
  if not preview:
    for index, (root, children) in enumerate(all_objects):
      root.rotation_euler = (0, 0, 0)
      for child in children:
        child.hide_render = index != 0
        child.hide_set(index != 0)
    bpy.ops.wm.save_as_mainfile(filepath=str(output / 'vehicles.blend'))
    (ROOT / 'manifest.json').write_text(json.dumps(dict(canvas=64, baseline=48, scale_divisor=4,
        ground_anchor=[32, 48], camera_elevation=30, pitch=args.pitch, models=MODELS,
        views=len(ANGLES), steering_angles=STEERING,
        front_axle_offsets=[MODEL_GEOMETRY[model[0]][3] * model[1] for model in MODELS],
        rear_axle_offsets=[-MODEL_GEOMETRY[model[0]][2] * model[1] for model in MODELS],
        frames=frames), indent=2) + '\n')


if __name__ == '__main__': main()
