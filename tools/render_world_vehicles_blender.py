#!/usr/bin/env python3
"""Author original aircraft, ships, trains and machinery with one registered map camera.

Explicit authoring only: Blender renders checked-in sources; normal builds use the PNGs.
No original game meshes, textures, silhouettes, palettes or recordings are read.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from strip_png_metadata import StripPngMetadata
from render_road_vehicles_blender import Material, Box, Mesh, Cylinder, Line

ROOT = Path(__file__).resolve().parents[1] / 'assets/landscape/world/blender'


def Sphere(name, at, size, material):
  bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=1, location=at)
  obj = bpy.context.object
  obj.name, obj.scale = name, size
  obj.data.materials.append(material)
  for face in obj.data.polygons: face.use_smooth = True
  return obj


def Beam(name, a, b, radius, material):
  delta = Vector(b) - Vector(a)
  bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=radius, depth=delta.length,
      location=(Vector(a) + Vector(b)) / 2)
  obj = bpy.context.object
  obj.name = name
  obj.rotation_euler = delta.to_track_quat('Z', 'Y').to_euler()
  obj.data.materials.append(material)
  for face in obj.data.polygons: face.use_smooth = True
  return obj


def Profile(name, sections, material):
  vertices, faces, steps = [], [], 40
  for x, width, bottom, top in sections:
    for i in range(steps):
      angle = math.tau * i / steps
      vertices.append((x, width * math.cos(angle), (top + bottom) / 2 + (top - bottom) / 2 * math.sin(angle)))
  for ring in range(len(sections) - 1):
    for i in range(steps):
      faces.append((ring * steps + i, ring * steps + (i + 1) % steps,
          (ring + 1) * steps + (i + 1) % steps, (ring + 1) * steps + i))
  faces += [tuple(reversed(range(steps))), tuple((len(sections) - 1) * steps + i for i in range(steps))]
  obj = Mesh(name, vertices, faces, [material])
  for face in obj.data.polygons: face.use_smooth = True
  return obj


def Wing(name, points, material, thickness=.012):
  n = len(points)
  normal=(Vector(points[1])-Vector(points[0])).cross(Vector(points[2])-Vector(points[0])).normalized()
  vertices = [tuple(Vector(point)+normal*offset) for offset in (-thickness/2, thickness/2) for point in points]
  faces = [tuple(reversed(range(n))), tuple(range(n, 2*n))]
  faces += [(i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n)]
  return Mesh(name, vertices, faces, [material], bevel=.003)


def Aircraft(m):
  Profile('Tapered pressurized fuselage', [(-.67,.006,.17,.19),(-.52,.045,.10,.25),(-.28,.086,.08,.28),
      (.32,.086,.08,.28),(.53,.063,.105,.25),(.66,.015,.16,.20),(.68,.001,.18,.19)], m['white'])
  for side in (-1,1):
    Wing('Swept main wing',
        [(.18,side*.06,.13),(-.14,side*.59,.115),(-.30,side*.61,.11),(-.18,side*.07,.13)],m['silver'])
    Wing('Horizontal stabilizer',
        [(-.43,side*.025,.23),(-.57,side*.24,.25),(-.67,side*.24,.25),(-.60,side*.02,.22)],m['blue'])
    engine=Profile('Underslung engine',
        [(-.10,.045,.02,.11),(.04,.052,.015,.115),(.12,.043,.025,.105)],m['silver'])
    engine.location.y=side*.25
    Sphere('Dark engine intake',(.115,side*.25,.065),(.009,.034,.034),m['black'])
    for x in (-.30,-.20,-.10,0,.10,.20,.30,.40):
      Sphere('Passenger window',(x,side*.084,.205),(.017,.003,.013),m['glass'])
    Line('Blue fuselage pinstripe',[(-.45,side*.072,.15),(.42,side*.075,.15)],m['blue'],.007)
    Sphere('Navigation lamp',(-.24,side*.59,.12),(.015,.01,.01),m['red'] if side<0 else m['green'])
  Wing('Vertical tail',[(-.42,0,.25),(-.57,0,.51),(-.65,0,.51),(-.63,0,.20)],m['blue'],.016)
  for side in (-1,1):
    Sphere('Cockpit glazing',(.49,side*.035,.235),(.070,.033,.016),m['glass'])


def Helicopter(m, rescue=False):
  paint=m['orange'] if rescue else m['red']
  Sphere('Rounded helicopter cabin',(.07,0,.23),(.245,.12,.13),paint)
  Sphere('Curved cockpit glazing',(.185,0,.265),(.135,.113,.087),m['glass'])
  Box('Cabin roof',(.01,0,.337),(.23,.15,.025),m['white'],.012)
  Profile('Tapered tail boom',[(-.62,.015,.25,.29),(-.15,.045,.19,.28)],paint)
  Wing('Tail fin',[(-.58,0,.26),(-.59,0,.47),(-.68,0,.44),(-.65,0,.25)],m['white'])
  for side in (-1,1):
    Line('Landing skid',[(-.23,side*.15,.035),(.20,side*.15,.035),(.27,side*.15,.065)],m['silver'],.009)
    for x in (-.12,.13): Beam('Skid brace',(x,side*.07,.15),(x,side*.15,.035),.006,m['silver'])
    Sphere('Rear cabin window',(-.08,side*.113,.25),(.055,.006,.055),m['glass'])
  Beam('Rotor mast',(0,0,.32),(0,0,.40),.012,m['steel'])
  rotor=bpy.data.objects.new('Main rotor',None); bpy.context.collection.objects.link(rotor)
  for angle in (.2,.2+math.pi):
    x,y=math.cos(angle),math.sin(angle)
    blade=Wing('Main rotor blade',[(x*.035-y*.012,y*.035+x*.012,.405),(x*.46-y*.020,y*.46+x*.020,.405),
        (x*.46+y*.020,y*.46-x*.020,.405),(x*.035+y*.012,y*.035-x*.012,.405)],m['steel'],.004)
    blade.parent=rotor
  Beam('Tail rotor spindle',(-.61,-.035,.31),(-.61,.035,.31),.005,m['steel'])
  tail=bpy.data.objects.new('Tail rotor',None); bpy.context.collection.objects.link(tail)
  tail.location=(-.61,-.04,.31)
  blade=Line('Tail rotor blade',[(0,0,-.08),(0,0,.08)],m['steel'],.007); blade.parent=tail


def Train(m, coach=False):
  for side in (-1,1):
    for axle in (-.28,-.20,.20,.28):
      Cylinder('Rail wheel',(axle,side*.10,.045),.043,.025,m['black'])
    Box('Bogie sill',(-.24,side*.102,.065),(.20,.025,.04),m['steel'],.008)
    Box('Bogie sill',(.24,side*.102,.065),(.20,.025,.04),m['steel'],.008)
  Box('Underframe',(0,0,.095),(.82,.225,.06),m['steel'],.012)
  Box('Rounded passenger body' if coach else 'Locomotive body',(0,0,.205),(.84,.235,.23),m['silver'],.035)
  Box('Blue waist stripe',(0,0,.155),(.845,.24,.035),m['blue'],.008)
  if coach:
    for side in (-1,1):
      for x in (-.27,-.15,-.03,.09,.21,.31):
        Box('Passenger glazing',(x,side*.117,.25),(.081,.008,.070),m['glass'],.012)
      for x in (-.37,.37):
        Box('End door',(x,side*.12,.205),(.047,.006,.15),m['white'],.007)
    Box('Roof ventilation',(0,0,.331),(.38,.12,.025),m['steel'],.01)
  else:
    for side in (-1,1):
      Box('Cab side window',(.29,side*.117,.26),(.11,.008,.075),m['glass'],.01)
      for x in (-.27,-.17,-.07,.03):
        Box('Cooling grille',(x,side*.119,.245),(.065,.006,.08),m['steel'],.004)
        for offset in (-.02,0,.02):
          Line('Vent slat',[(x+offset,side*.124,.21),(x+offset,side*.124,.28)],m['black'],.002)
    Box('Cab front glazing',(.415,0,.25),(.009,.17,.07),m['glass'],.012)
    for y in (-.065,.065): Sphere('Headlamp',(.422,y,.19),(.006,.014,.011),m['lamp'])
    for x in (-.21,.02): Box('Roof fan',(x,0,.333),(.15,.14,.025),m['steel'],.015)
  for x in (-.46,.46): Beam('Coupler',(x*.9,0,.095),(x,0,.095),.014,m['steel'])


def Ship(m, sail=False):
  if sail:
    Profile('Sailing hull',[(-.38,.005,.04,.11),(-.22,.10,.015,.13),(.16,.115,.02,.13),(.39,.002,.08,.13)],m['white'])
    Beam('Mast',(0,0,.12),(0,0,.79),.006,m['steel'])
    Wing('Mainsail',[(.02,.005,.18),(.02,.005,.75),(-.29,.005,.20)],m['white'],.002)
    Wing('Jib',[(.04,-.005,.72),(.35,-.005,.16),(.04,-.005,.18)],m['cream'],.002)
    Line('Standing rigging',[(-.29,0,.14),(0,0,.79),(.35,0,.14)],m['steel'],.002)
    return
  Profile('Ocean freighter hull',
      [(-.76,.012,.03,.15),(-.62,.15,.005,.21),(.35,.19,.005,.22),(.68,.12,.05,.22),(.81,.002,.16,.22)],m['blue'])
  Box('Cargo deck',(0,0,.20),(1.26,.30,.025),m['steel'],.02)
  Box('Aft deckhouse',(-.47,0,.31),(.28,.25,.20),m['white'],.015)
  Box('Bridge glazing',(-.44,0,.40),(.24,.26,.04),m['glass'],.006)
  Box('Bridge roof',(-.45,0,.44),(.32,.29,.025),m['white'],.01)
  for x in (-.14,.10,.34):
    for side in (-1,1):
      Box('Shipping container',(x,side*.075,.30),(.21,.13,.15),m['orange'] if x<0 else m['green'],.006)
      for offset in (-.07,-.035,0,.035,.07):
        Line('Container corrugation',[(x+offset,side*.141,.23),(x+offset,side*.141,.37)],m['steel'],.0015)
  Box('Funnel',(-.65,0,.39),(.08,.09,.17),m['red'],.012)
  Beam('Radio mast',(-.46,0,.45),(-.46,0,.64),.004,m['steel'])
  for side in (-1,1): Line('Deck rail',[(-.71,side*.15,.27),(.55,side*.15,.27)],m['white'],.003)


def Bulldozer(m):
  for side in (-1,1):
    Box('Track belt',(0,side*.14,.065),(.43,.085,.115),m['black'],.045)
    for x in (-.15,-.05,.05,.15): Cylinder('Track wheel',(x,side*.184,.065),.037,.008,m['steel'])
    for x in (-.18,-.12,-.06,0,.06,.12,.18): Box('Track tread',(x,side*.14,.014),(.025,.09,.014),m['steel'],.003)
  Box('Engine hood',(.10,0,.15),(.24,.19,.14),m['yellow'],.025)
  Box('Cabin',(-.09,0,.25),(.17,.18,.24),m['yellow'],.015)
  Box('Front windscreen',(.0,0,.27),(.01,.155,.13),m['glass'],.012)
  for side in (-1,1):
    Box('Side window',(-.085,side*.091,.27),(.12,.008,.13),m['glass'],.012)
    Beam('Blade arm',(-.05,side*.10,.10),(.31,side*.13,.07),.014,m['yellow'])
  Box('Dozer blade',(.32,0,.09),(.045,.42,.17),m['silver'],.025)
  Beam('Exhaust pipe',(.15,.07,.20),(.15,.07,.35),.009,m['steel'])


def Drawbridge(m, opened=False):
  for x in (-.49,.49):
    for side in (-1,1):
      Box('Concrete pier',(x,side*.22,.10),(.16,.14,.28),m['silver'],.015)
    Box('Abutment',(x,0,.22),(.16,.58,.08),m['steel'],.008)
  angle=math.radians(60) if opened else 0
  def Point(p):
    x,y,z=p; distance=x+.43
    return (-.43+distance*math.cos(angle)-(z-.265)*math.sin(angle),y,
        .265+distance*math.sin(angle)+(z-.265)*math.cos(angle))
  deck=Box('Moving steel deck',Point((0,0,.25)),(.86,.48,.06),m['steel'],.008)
  deck.rotation_euler.y=-angle
  surface=Box('Asphalt carriageway',Point((0,0,.284)),(.86,.40,.015),m['black'],.003)
  surface.rotation_euler.y=-angle
  for side in (-1,1):
    for x in (-.38,-.19,0,.19,.38):
      Beam('Railing post',Point((x,side*.23,.28)),Point((x,side*.23,.39)),.007,m['silver'])
    Beam('Top handrail',Point((-.42,side*.23,.39)),Point((.42,side*.23,.39)),.007,m['silver'])
    Beam('Side girder',Point((-.42,side*.23,.24)),Point((.42,side*.23,.24)),.022,m['blue'])
  for x in (-.3,-.1,.1,.3):
    mark=Box('Center marking',Point((x,0,.295)),(.10,.022,.004),m['white'],.001)
    mark.rotation_euler.y=-angle
  for side in (-1,1):
    Box('Hinge housing',(-.44,side*.27,.28),(.16,.10,.17),m['blue'],.012)
    Beam('Hydraulic ram',(-.51,side*.27,.13),Point((-.10,side*.23,.23)),.02,m['silver'])


def Crowd(m):
  skin=Material('Skin',(.48,.29,.19),0,.65)
  for i in range(12):
    x=(i%4-1.5)*.12; y=(i//4-1)*.14
    coat=m[['blue','red','cream','green','orange'][i%5]]
    Sphere('Coat',(x,y,.09),(.035,.025,.045),coat)
    Sphere('Head',(x,y,.157),(.023,.022,.026),skin)
    for side in (-1,1):
      Beam('Leg',(x+side*.016,y,.06),(x+side*.020,y,.01),.011,m['steel'])
      Beam('Arm',(x+side*.03,y,.115),(x+side*.05,y,.07),.009,coat)
    if i%5==0:
      Beam('Placard pole',(x+.05,y,.08),(x+.05,y,.26),.003,m['steel'])
      Box('Blank placard',(x+.05,y,.24),(.10,.008,.07),m['cream'],.003)


def Crawler(m):
  # An original industrial quadruped, with a cabin and articulated steel legs.
  Sphere('Armored crawler carapace',(0,0,1.45),(.70,.53,.45),m['steel'])
  Sphere('Copper top plating',(-.08,0,1.68),(.56,.46,.26),m['bronze'])
  Sphere('Observation canopy',(.49,0,1.54),(.30,.39,.25),m['glass'])
  for side in (-1,1):
    for x in (-.48,.42):
      knee=(x*1.65,side*.85,.72)
      foot=(x*2.0,side*1.1,.08)
      Beam('Hydraulic upper leg',(x,side*.38,1.4),knee,.10,m['bronze'])
      Beam('Telescopic lower leg',knee,foot,.075,m['steel'])
      Sphere('Knee joint',knee,(.15,.15,.15),m['silver'])
      Box('Wide stabilizer foot',foot,(.33,.32,.13),m['black'],.05)
      Beam('Actuator piston',(x,side*.42,1.23),(knee[0],knee[1],knee[2]+.18),.035,m['silver'])
  for side in (-1,1): Sphere('Amber warning lamp',(.6,side*.27,1.37),(.035,.06,.035),m['orange'])
  for i in range(5): Box('Roof cooling fin',(-.35+i*.13,0,1.9),(.055,.42,.035),m['silver'],.008)


def main():
  parser=argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--samples',type=int,default=32)
  parser.add_argument('--model',help='Regenerate just this model')
  args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
  ROOT.mkdir(parents=True,exist_ok=True)
  bpy.context.preferences.filepaths.save_version=0
  bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
  m={name:Material(name,color,metallic,roughness) for name,color,metallic,roughness in [
      ('white',(.82,.85,.86),.2,.3),('cream',(.88,.83,.67),0,.55),('silver',(.46,.51,.55),.65,.3),
      ('steel',(.18,.22,.25),.65,.4),('black',(.023,.028,.032),.1,.6),('glass',(.027,.075,.105),.55,.17),
      ('blue',(.035,.16,.31),.45,.3),('red',(.52,.055,.035),.3,.3),('green',(.08,.26,.18),.3,.4),
      ('orange',(.82,.25,.035),.2,.4),('yellow',(.83,.55,.06),.25,.4),('lamp',(.93,.86,.57),.2,.2),
      ('bronze',(.36,.23,.12),.6,.35)]}
  roots=[]
  for name,builder in [('airplane',Aircraft),('helicopter',Helicopter),('ship',Ship),
      ('engine',Train),('coach',lambda m:Train(m,True)),('sailboat',lambda m:Ship(m,True)),
      ('bulldozer',Bulldozer),('drawbridge-closed',Drawbridge),
      ('drawbridge-open',lambda m:Drawbridge(m,True)),('crowd',Crowd),
      ('rescue',lambda m:Helicopter(m,True)),('crawler',Crawler)]:
    before=set(bpy.context.scene.objects)
    root=bpy.data.objects.new(name,None); bpy.context.collection.objects.link(root)
    builder(m)
    children=list(set(bpy.context.scene.objects)-before-{root})
    for child in children:
      if child.parent is None: child.parent=root
      child.hide_render=True
    roots.append((name,root,children))
  scene=bpy.context.scene
  scene.render.engine='CYCLES'; scene.cycles.samples=args.samples; scene.cycles.use_denoising=True
  scene.render.film_transparent=True
  scene.render.image_settings.file_format='PNG'; scene.render.image_settings.color_mode='RGBA'
  scene.render.resolution_percentage=100; scene.render.use_persistent_data=True
  scene.view_settings.view_transform='Standard'
  scene.world.use_nodes=True; scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.65,.70,.80,1)
  scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.6
  for at,energy,size in [((-3,-4,6),450,4),((4,1,4),180,5)]:
    bpy.ops.object.light_add(type='AREA',location=at); light=bpy.context.object
    light.data.energy,light.data.shape,light.data.size=energy,'DISK',size
    light.rotation_euler=(-light.location).to_track_quat('-Z','Y').to_euler()
  bpy.ops.object.camera_add(); camera=bpy.context.object; scene.camera=camera; camera.data.type='ORTHO'
  camera.data.sensor_fit='HORIZONTAL'
  previous=ROOT/'manifest.json'
  frames=[]
  if args.model and previous.exists():
    frames=[f for f in json.loads(previous.read_text())['frames'] if f['model']!=args.model]
  for name,root,children in roots:
    if args.model and name!=args.model: continue
    for child in children: child.hide_render=False
    size=(512,640) if name=='crawler' else (256,256)
    anchor=(256,460) if name=='crawler' else (128,208)
    supersample=2
    camera.location=(6,6,6*math.sqrt(2/3)); camera.rotation_euler=(-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=size[0]/(128/math.sqrt(2))
    camera.data.shift_y=(anchor[1]-size[1]/2)/size[0]
    scene.render.resolution_x,scene.render.resolution_y=size[0]*supersample,size[1]*supersample
    headings=[225,180,135,90,45] if name in ('airplane','helicopter','ship','rescue') else [0,90,45,0,90]
    if name=='sailboat': headings=[0,90]
    if name in ('bulldozer','crowd'): headings=[0,90]
    if name=='crawler': headings=[45,135]
    if name.startswith('drawbridge'): headings=[0]
    for view,yaw in enumerate(headings):
      pitch=math.radians(14) if name in ('engine','coach') and view>=3 else 0
      root.rotation_euler=(0,pitch,math.radians(yaw))
      phases=8 if name=='helicopter' or (name=='rescue' and view==2) else 1
      rotor=next((child for child in children if child.name.startswith('Main rotor') and child.type=='EMPTY'),None)
      tail=next((child for child in children if child.name.startswith('Tail rotor') and child.type=='EMPTY'),None)
      for phase in range(phases):
        if rotor: rotor.rotation_euler.z=math.pi*phase/8
        if tail: tail.rotation_euler.y=math.pi*phase/4
        file=f'{name}-{view}.png' if not phase else f'{name}-{view}-{phase:02}.png'
        scene.render.filepath=str(ROOT/file)
        bpy.ops.render.render(write_still=True)
        StripPngMetadata(Path(scene.render.filepath))
        frames.append(dict(model=name,view=view,file=file,canvas=list(size),anchor=list(anchor),
            supersample=supersample,scale_divisor=4,yaw=yaw,pitch=math.degrees(pitch),rotor_frame=phase,
            sha256=hashlib.sha256((ROOT/file).read_bytes()).hexdigest()))
      if rotor: rotor.rotation_euler.z=0
      if tail: tail.rotation_euler.y=0
    for child in children: child.hide_render=True
  bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'world-vehicles.blend'))
  (ROOT/'manifest.json').write_text(json.dumps(dict(camera_elevation=30,tile_width=128,frames=frames),indent=2)+'\n')


if __name__=='__main__': main()
