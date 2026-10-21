#!/usr/bin/env python3
"""Author editable vector artwork for the native toolbar.

Coordinates are vector units, never a source bitmap. After this script, run
node tools/generate_map_icons.cjs to rasterize directly to 128px premultiplied
ARGB assets. Primary order follows CityTopbarButton; tools follow CityCanvasTool.
"""
from pathlib import Path
import re
import math

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/ui/toolbar'
INK = '#11151b'
WHITE = '#fffdf1'
SILVER = '#cedbe7'
DARK = '#374453'
YELLOW = '#ffcf00'
GREEN = '#53bd18'
BLUE = '#128fef'
RED = '#ed351b'
BROWN = '#a45b28'


def Shade(color, factor):
  rgb = [int(color[i:i+2], 16) for i in (1, 3, 5)]
  return '#' + ''.join(f'{min(255, int(c*factor)):02x}' for c in rgb)


class VectorDraw:
  """Small SVG authoring surface, with smooth geometry at the final resolution."""
  gradients = {}

  def __init__(self):
    self.elements = []

  def Paint(self, fill, stroke=None, width=1):
    if fill:
      if fill not in self.gradients:
        rgb = [int(fill[i:i+2], 16) for i in (1, 3, 5)]
        light = '#' + ''.join(f'{min(255, int(c*.65+255*.35)):02x}' for c in rgb)
        dark = Shade(fill, .52)
        self.gradients[fill] = (f'paint{len(self.gradients)}', light, fill, dark)
      name, _, _, _ = self.gradients[fill]
      color = f'url(#{name})'
    else:
      color = 'none'
    return f'fill="{color}" stroke="{stroke or "none"}" stroke-width="{width:g}"'

  def rectangle(self, bounds, fill=None, outline=None, width=1):
    x,y,right,bottom = bounds
    self.elements.append(f'<rect x="{x:g}" y="{y:g}" width="{right-x:g}" height="{bottom-y:g}" '
        f'rx=".35" {self.Paint(fill,outline,width)}/>')
    if fill and outline and right-x > 4 and bottom-y > 4:
      self.line([(x+1,y+1),(right-1,y+1)], WHITE, .9)
      self.line([(x+1,y+1),(x+1,bottom-1)], Shade(fill, 1.35), .8)
      self.line([(x+1,bottom-1),(right-1,bottom-1),(right-1,y+1)], Shade(fill, .45), .9)

  def polygon(self, points, fill=None, outline=None):
    coords = ' '.join(f'{x:g},{y:g}' for x,y in points)
    self.elements.append(f'<polygon points="{coords}" {self.Paint(fill,outline)}/>')

  def ellipse(self, bounds, fill=None, outline=None, width=1):
    x,y,right,bottom = bounds
    self.elements.append(f'<ellipse cx="{(x+right)/2:g}" cy="{(y+bottom)/2:g}" '
        f'rx="{(right-x)/2:g}" ry="{(bottom-y)/2:g}" {self.Paint(fill,outline,width)}/>')
    if fill and outline and right-x > 6 and bottom-y > 6:
      self.arc((x+1,y+1,right-1,bottom-1),195,285,WHITE,.9)
      self.arc((x+1,y+1,right-1,bottom-1),15,95,Shade(fill,.4),1)

  def line(self, points, fill=None, width=1):
    coords = ' '.join(f'{x:g},{y:g}' for x,y in points)
    self.elements.append(f'<polyline points="{coords}" fill="none" stroke="{fill}" '
        f'stroke-width="{width*.8:g}"/>')

  def ArcPath(self, bounds, start, end):
    x,y,right,bottom=bounds
    cx,cy,rx,ry=(x+right)/2,(y+bottom)/2,(right-x)/2,(bottom-y)/2
    a,b=math.radians(start),math.radians(end)
    first=(cx+rx*math.cos(a),cy+ry*math.sin(a))
    last=(cx+rx*math.cos(b),cy+ry*math.sin(b))
    d=f'M{first[0]:g} {first[1]:g} A{rx:g} {ry:g} 0 {int((end-start)%360>180)} 1 {last[0]:g} {last[1]:g}'
    return d,cx,cy

  def arc(self, bounds, start, end, fill, width=1):
    path,_,_=self.ArcPath(bounds,start,end)
    self.elements.append(f'<path d="{path}" fill="none" stroke="{fill}" stroke-width="{width*.8:g}"/>')

  def pieslice(self, bounds, start, end, fill, outline=None, width=1):
    path,cx,cy=self.ArcPath(bounds,start,end)
    self.elements.append(f'<path d="{path} L{cx:g} {cy:g} Z" {self.Paint(fill,outline,width)}/>')


def Line(points, color=INK, width=2):
  draw.line(points, fill=color, width=width)


def Box(bounds, fill, outline=INK, width=1):
  draw.rectangle(bounds, fill=fill, outline=outline, width=width)


def Poly(points, fill, outline=INK):
  draw.polygon(points, fill=fill, outline=outline)


def Oval(bounds, fill, outline=INK, width=1):
  draw.ellipse(bounds, fill=fill, outline=outline, width=width)


def Arrow(x, y, direction='up', color=YELLOW):
  if direction == 'up':
    Poly([(x, y), (x-7, y+8), (x-3, y+8), (x-3, y+17), (x+3, y+17), (x+3, y+8), (x+7, y+8)], color)
  elif direction == 'down':
    Poly([(x, y+17), (x-7, y+9), (x-3, y+9), (x-3, y), (x+3, y), (x+3, y+9), (x+7, y+9)], color)
  else:
    Poly([(x+15,y+4),(x+8,y-3),(x+8,y+1),(x,y+1),(x,y+7),(x+8,y+7),(x+8,y+11)],color)


def Cross(x, y, color=WHITE, size=6):
  Poly([(x-2,y-size),(x+2,y-size),(x+2,y-2),(x+size,y-2),(x+size,y+2),
        (x+2,y+2),(x+2,y+size),(x-2,y+size),(x-2,y+2),(x-size,y+2),
        (x-size,y-2),(x-2,y-2)],color)


def Bolt(x=22, y=4, color=YELLOW):
  Poly([(x,y),(x-10,y+17),(x-2,y+17),(x-8,y+31),(x+12,y+10),(x+3,y+10),(x+9,y)],color)


def Drop(x=20, y=6, color=BLUE):
  Poly([(x,y),(x-8,y+13),(x-8,y+21),(x-4,y+26),(x+4,y+26),(x+8,y+21),(x+8,y+13)],color)
  Line([(x-4,y+15),(x-4,y+21),(x-1,y+23)],WHITE,2)


def Tile(fill=GREEN):
  Poly([(3,24),(20,15),(37,24),(20,34)],fill)
  Line([(3,25),(3,29),(20,38),(37,29),(37,25)],BROWN,2)


def Building(color=BLUE, style='flat', tall=False):
  Poly([(2,30),(19,21),(38,30),(21,39)],'#696f72')
  Line([(3,30),(21,38),(37,30)],SILVER,1)
  top=3 if tall else 8
  Poly([(7,top+6),(21,top),(33,top+6),(19,top+13)],SILVER)
  Poly([(7,top+6),(19,top+13),(19,33),(7,27)],color)
  Poly([(19,top+13),(33,top+6),(33,27),(19,33)],Shade(color,.68))
  Line([(8,top+7),(18,top+13),(18,31)],Shade(color,1.6),1)
  Line([(8,top+5),(21,top+1),(31,top+6)],WHITE,1)
  Poly([(13,top+5),(21,top+2),(27,top+5),(19,top+9)],DARK)
  for yy in range(top+11,27,4):
    for xx in [10,14]:
      Box((xx,yy,xx+2,yy+2.5),INK,None)
      Line([(xx,yy),(xx+1,yy)],'#b9f5ff',.7)
    for xx in [23,28]:
      Box((xx,yy+1,xx+2,yy+3.5),INK,None)
      Line([(xx,yy+1),(xx+1,yy+1)],'#68d5ff',.7)
  Line([(8,27),(18,32),(32,26)],Shade(color,.4),1)
  if style=='roof':
    Poly([(5,top+7),(20,top-4),(35,top+7),(19,top+14)],RED)
    Poly([(20,top-4),(35,top+7),(19,top+14)],Shade(RED,.65))
    for step in range(3):
      Line([(9+step*3,top+5-step*2),(20+step*3,top+11-step*2)],'#ff8b46',.7)
    Line([(5,top+7),(20,top-4),(35,top+7)],'#ffb470',1)
  if style=='columns':
    for xx in [10,16,23,29]: Box((xx,18,xx+2,30),SILVER)
    Poly([(5,16),(20,7),(35,16)],WHITE)
    Box((5,31,35,34),SILVER)


def Badge(symbol, color=BLUE):
  Poly([(24,19),(37,19),(36,31),(30,37),(24,31)],color)
  if symbol=='+': Cross(30,26,WHITE,4)
  elif symbol=='fire': Flame(30,22,6)
  elif symbol=='star':
    Poly([(30,21),(32,25),(36,25),(33,28),(34,32),(30,30),(26,32),(27,28),(24,25),(28,25)],YELLOW)
  elif symbol=='bars':
    for x in [27,30,33]: Line([(x,22),(x,31)],WHITE,1)


def Flame(x=20,y=5,size=15):
  Poly([(x,y),(x+size//2,y+size//2),(x+size//3,y+size),(x,y+size+4),
        (x-size//2,y+size),(x-size//2,y+size//2),(x-2,y+size//3)],RED)
  Poly([(x,y+size//2),(x+size//4,y+size),(x,y+size+2),(x-size//4,y+size)],YELLOW)


def Road(kind='road'):
  Box((3,1,37,39),INK)
  Box((5,1,35,37),'#354454',None)
  for i in range(24):
    x=8+(i*11)%24; y=3+(i*7)%33
    Line([(x,y),(x+1,y)],'#667384',.6)
  Line([(6,2),(6,37)],SILVER,2); Line([(34,2),(34,37)],SILVER,2)
  Line([(7,2),(7,37)],WHITE,.7)
  Line([(5,38),(35,38)],'#69788b',1)
  if kind=='highway':
    Line([(18,2),(18,37)],YELLOW,1.5)
    Line([(22,2),(22,37)],YELLOW,1.5)
    for x in [12,28]:
      for y in [4,14,24,34]: Line([(x,y),(x,min(y+5,37))],WHITE,1.3)
  else:
    for y in [3,13,23,33]: Box((18.5,y,21.5,y+5),WHITE,None)
  if kind=='tunnel':
    draw.pieslice((1,0,39,34),180,360,fill=BROWN,outline=INK,width=2)
    draw.pieslice((7,6,33,30),180,360,fill=INK)
    Box((1,17,7,28),BROWN); Box((33,17,39,28),BROWN)
    draw.arc((3,2,37,32),190,350,fill='#edbd7f',width=1.5)
  if kind=='onramp':
    Line([(2,34),(8,30),(13,23),(14,2)],INK,9)
    Line([(2,34),(8,30),(13,23),(14,2)],SILVER,7)
    Line([(2,34),(8,30),(13,23),(14,2)],DARK,5)
    Line([(2,34),(8,30),(13,23),(14,2)],WHITE,1)


def Rail(subway=False, connection=False):
  if subway or connection:
    Poly([(2,35),(2,15),(8,4),(30,4),(37,15),(37,35)],SILVER)
    Poly([(8,34),(8,17),(13,10),(26,10),(31,17),(31,34)],INK)
    for y in [24,29,34,38]: Line([(11,y),(29,y)],BROWN,3)
    for x in [13,27]: Line([(x,22),(x,39)],SILVER,3)
    if subway:
      Box((10,1,29,13),GREEN)
      Line([(15,4),(15,10),(24,10),(24,4)],WHITE,2)
    else: Arrow(28,12,'up',YELLOW)
    return
  Box((3,1,37,39),'#393f42')
  for y in range(3,38,5):
    Box((5,y,35,y+3),BROWN)
    Line([(6,y+.7),(34,y+.7)],'#edb26a',.7)
  for x in [11,28]:
    Box((x-2,1,x+2,39),INK)
    Box((x-1,1,x+1,39),SILVER,None)
    Line([(x-1,2),(x-1,38)],WHITE,.8)
    for y in range(4,38,5): Line([(x-3,y),(x+3,y)],SILVER,.7)


def Tree(x=20,y=2):
  Box((x-2,y+20,x+2,y+32),BROWN)
  Poly([(x,y),(x-11,y+17),(x-6,y+17),(x-14,y+24),(x+13,y+24),(x+6,y+16),(x+11,y+16)],GREEN)
  Poly([(x,y+1),(x+11,y+16),(x+6,y+16),(x+13,y+24),(x+1,y+24)],'#237413',None)
  for dx,dy,length in [(-2,7,4),(-3,11,6),(-5,15,5),(-5,20,8),(3,20,4)]:
    Line([(x+dx,y+dy),(x+dx-length,y+dy+2)],'#a5e92e',1)
  Line([(x,y+3),(x-7,y+16),(x-3,y+16),(x-10,y+22)],'#ddff6c',1)


def Plant(kind):
  if kind in ['wind','solar','hydro','microwave']:
    if kind=='wind':
      Box((18,14,21,35),WHITE); Oval((15,12,24,21),SILVER)
      for pts in [[(19,16),(18,2),(22,2),(22,16)],[(19,16),(3,23),(5,27),(20,19)],[(19,16),(29,31),(33,28),(22,16)]]: Poly(pts,WHITE)
    elif kind=='solar':
      Poly([(4,10),(31,4),(37,27),(11,34)],BLUE)
      for j in range(1,4): Line([(4+j*7,10-j*2),(11+j*7,34-j*2)],WHITE,1)
      for j in range(1,4): Line([(4+j*2,10+j*6),(31+j*2,4+j*6)],WHITE,1)
      Line([(15,33),(15,37),(30,37)],SILVER,2)
    elif kind=='hydro':
      for y in [24,29,34]: Line([(2,y),(9,y-2),(15,y+1),(25,y-2),(37,y)],BLUE,3)
      Poly([(4,4),(32,4),(29,23),(8,23)],SILVER)
      for x in [10,17,24]: Box((x,7,x+3,27),BLUE)
    else:
      Box((18,23,21,35),SILVER); Box((9,34,30,37),DARK)
      draw.pieslice((4,2,32,30),25,205,fill=WHITE,outline=INK,width=2)
      Line([(19,17),(32,6)],DARK,2); Oval((30,3,35,8),YELLOW)
    return
  Building(DARK)
  if kind in ['coal','oil','gas']:
    Box((8,3,14,22),BROWN); Box((9,3,13,5),RED)
    if kind=='coal':
      for x,y in [(25,28),(30,26),(34,30),(22,32)]: Poly([(x,y-4),(x+4,y-2),(x+3,y+3),(x-3,y+2)],'#3b3d40')
    elif kind=='oil':
      Oval((24,23,36,29),SILVER); Box((24,26,36,35),DARK); Oval((24,31,36,37),DARK)
      Line([(24,29),(36,29)],SILVER,1)
    else:
      Box((20,14,37,32),WHITE); Oval((20,10,37,19),WHITE)
      Line([(23,20),(34,20)],RED,3)
      Flame(11,23,13)
  elif kind=='nuclear':
    Poly([(5,29),(8,18),(7,5),(21,5),(20,18),(25,29)],SILVER)
    Oval((7,3,21,8),DARK)
    Oval((24,22,38,36),YELLOW)
    for angle in [(26,24,31,28),(32,25,36,29),(29,30,33,34)]: Box(angle,INK)
  elif kind=='fusion':
    Oval((8,8,32,28),BLUE,width=2); Oval((12,12,28,24),INK)
    for x in [10,16,22,28]: Line([(x,8),(x,28)],WHITE,2)
    draw.elements.append('<g transform="translate(17 19) scale(.55)">')
    Bolt()
    draw.elements.append('</g>')


def Paper(kind='paper'):
  Poly([(7,3),(27,3),(34,10),(34,36),(7,36)],WHITE)
  Poly([(27,3),(27,10),(34,10)],SILVER)
  if kind=='graph':
    Box((9,8,31,32),'#172738')
    for y in [13,19,25]: Line([(10,y),(30,y)],'#42718a',.6)
    for x in [15,21,27]: Line([(x,9),(x,31)],'#42718a',.6)
    Line([(10,28),(15,19),(21,24),(25,14),(30,11)],YELLOW,1.8)
    Line([(10,23),(16,26),(22,16),(30,20)],RED,1.8)
  else:
    Box((11,8,23,11),RED,None)
    for y in [16,21,26,31]: Line([(11,y),(29,y)],DARK,1)
    if kind=='report': Box((14,1,27,6),BROWN)


def Magnify(plus=None):
  Oval((3,2,28,27),INK); Oval((5,4,26,25),BLUE); Oval((7,6,24,23),'#91c7dd')
  Line([(24,24),(35,35)],INK,7); Line([(25,25),(35,35)],BROWN,4)
  Line([(8,12),(10,8),(15,7)],WHITE,2)
  if plus is not None:
    Line([(9,14),(21,14)],YELLOW,3)
    if plus: Line([(15,8),(15,20)],YELLOW,3)


def Zone(kind, dense=False):
  color={'residential':GREEN,'commercial':BLUE,'industrial':YELLOW}[kind]
  Tile(color)
  if dense:
    for x,y,h in [(5,14,14),(15,8,22),(26,12,17)]:
      Poly([(x+8,y+2),(x+11,y),(x+11,y+h-2),(x+8,y+h)],Shade(color,.55))
      Box((x,y,x+8,y+h),color); Poly([(x,y),(x+4,y-3),(x+11,y),(x+8,y+3)],SILVER)
      for yy in range(y+5,y+h-1,4):
        for xx in [x+2,x+5]:
          Box((xx,yy,xx+1.5,yy+2),INK,None)
          Line([(xx,yy),(xx+1,yy)],'#f9f9ac',.6)
  else:
    for x,y in [(6,16),(21,10)]:
      Box((x,y,x+11,y+15),color); Poly([(x-2,y),(x+5,y-6),(x+13,y)],WHITE)
      Box((x+4,y+7,x+7,y+15),DARK)
      Box((x+1,y+3,x+3,y+6),'#dcf4ff')
      Box((x+8,y+3,x+10,y+6),'#dcf4ff')


def Water(kind):
  if kind=='pipes':
    Line([(4,25),(20,25),(20,7)],INK,10); Line([(4,25),(20,25),(20,7)],BLUE,7)
    Line([(3,22),(17,22),(17,7)],WHITE,1)
    for x,y in [(5,25),(20,8),(20,18)]: Box((x-4,y-2,x+4,y+2),SILVER)
  elif kind=='water_tower':
    for x in [10,28]: Line([(x,17),(x-3 if x<20 else x+3,37)],DARK,3)
    Line([(10,25),(29,34),(10,34),(29,25)],SILVER,2)
    Box((7,9,32,20),WHITE); Oval((7,3,32,14),RED); Oval((7,15,32,24),RED)
  elif kind=='water_pump':
    Box((3,33,38,38),DARK)
    Line([(4,26),(12,26),(12,20),(22,20)],INK,9)
    Line([(4,25),(12,25),(12,19),(22,19)],SILVER,6)
    Line([(4,23),(10,23)],WHITE,1)
    Box((18,11,35,31),BLUE)
    Oval((17,17,37,36),BLUE); Oval((23,23,31,31),SILVER)
    Box((23,5,30,13),SILVER); Oval((18,1,35,7),RED)
    Line([(21,3),(32,3)],'#ffb274',1)
  else:
    Building(BLUE)
    Oval((3,23,21,34),SILVER); Oval((5,25,19,32),BLUE)
    if kind=='desalination': Drop(30,17,WHITE)
    else: Line([(7,28),(15,28)],WHITE,2)


def Tool(name):
  if 'residential' in name or 'commercial' in name or 'industrial' in name:
    Zone(name.replace('dense_',''),'dense_' in name)
  elif name in ['road','highway','tunnel','onramp']: Road(name)
  elif name in ['rail','subway','subway_rail_connection']: Rail(name!='rail',name=='subway_rail_connection')
  elif name in ['coal','oil','gas','nuclear','hydro','wind','solar','microwave','fusion']: Plant(name)
  elif name=='power_lines':
    Poly([(6,38),(17,1),(23,1),(35,38),(29,38),(20,8),(12,38)],SILVER)
    for y,w in [(9,13),(19,17)]:
      Box((20-w,y,20+w,y+3),DARK)
      Line([(21-w,y+.7),(19+w,y+.7)],WHITE,1)
      for x in [22-w,18+w]:
        Box((x-2,y+3,x+2,y+8),'#66bbdb')
        Line([(x-2,y+5),(x+2,y+5)],WHITE,.7)
    Line([(12,35),(27,26),(16,16),(23,7)],DARK,2)
    Line([(29,35),(14,26),(24,16),(17,7)],WHITE,1)
    Box((3,36,15,39),DARK); Box((26,36,38,39),DARK)
  elif name in ['pipes','water_tower','water_pump','water_treatment','desalination']: Water(name)
  elif name in ['demolish','dezone']:
    if name=='dezone':
      Tile(); Line([(9,20),(31,32)],RED,4); Line([(10,33),(30,20)],RED,4)
    else:
      # A round steel bomb with a lit fuse, matching the in-game demolition symbol.
      draw.elements.append('<path d="M27 12 Q29 2 35 5" fill="none" stroke="#11151b" stroke-width="4"/>')
      draw.elements.append('<path d="M27 12 Q29 2 35 5" fill="none" stroke="#c59a59" stroke-width="2"/>')
      Poly([(22,13),(26,7),(32,11),(28,17)],DARK)
      Line([(25,10),(29,12)],SILVER,2)
      Oval((3,12,32,39),'#555d63',INK,1.6)
      draw.arc((6,15,28,35),200,285,WHITE,2.4)
      draw.arc((7,17,29,37),10,85,'#20252b',2.5)
      Poly([(35,1),(36,4),(39,3),(37,6),(39,8),(35,7),(34,10),(33,6),(30,5),(34,4)],RED)
      Oval((33,3,37,7),YELLOW,None)
      Line([(35,3),(35,6)],WHITE,1.3)
  elif name in ['raise_terrain','lower_terrain','level_terrain']:
    Tile(); Poly([(5,19),(18,6),(32,20),(20,28)],BROWN)
    if name=='level_terrain': Line([(7,12),(31,12)],YELLOW,4)
    else: Arrow(29,3,'up' if name=='raise_terrain' else 'down')
  elif name=='trees': Tile(); Tree()
  elif name=='water': Tile(BLUE); Drop(20,3)
  elif name=='sign':
    Box((18,18,22,37),BROWN); Box((3,4,36,23),GREEN)
    Line([(10,19),(15,8),(20,19)],INK,2); Line([(12,15),(18,15)],INK,2)
    Line([(25,8),(25,19),(31,19)],INK,2)
  elif name=='query': Magnify()
  elif name=='center':
    Oval((6,6,32,32),BLUE); Oval((12,12,26,26),WHITE)
    for a,b in [((19,0),(19,13)),((19,25),(19,39)),((0,19),(13,19)),((25,19),(39,19))]: Line([a,b],YELLOW,3)
  elif name in ['seaport','marina']:
    for y in [28,34,38]: Line([(2,y),(8,y-2),(15,y+1),(24,y-2),(37,y)],BLUE,2)
    Poly([(2,23),(38,23),(31,33),(10,33)],SILVER)
    Line([(6,25),(34,25)],'#183867',2)
    if name=='seaport':
      Box((8,14,32,23),RED); Box((13,6,27,14),WHITE); Box((16,1,22,7),DARK)
      for x in [15,21]: Box((x,8,x+3,11),BLUE,None)
    else: Poly([(18,4),(18,23),(32,23)],WHITE); Line([(17,3),(17,27)],BROWN,2)
  elif name=='airport':
    Poly([(19,2),(23,3),(24,16),(37,25),(37,29),(24,25),(24,32),(29,35),(29,38),(20,35),
          (11,38),(11,35),(16,32),(16,25),(3,29),(3,25),(16,16),(17,3)],WHITE)
    Box((18,5,22,9),BLUE)
  elif name in ['small_park','big_park','forest_arcology']:
    Tile(); Tree(13,4)
    if name!='small_park': Tree(28,0)
    if name=='forest_arcology': Building(GREEN,tall=True); Tree(28,3)
  elif name=='stadium':
    Oval((2,7,38,35),SILVER); Oval((6,11,34,31),DARK); Oval((10,14,30,28),GREEN)
    Line([(20,15),(20,27)],WHITE,1); Box((11,18,14,23),WHITE); Box((26,18,29,23),WHITE)
  elif name=='zoo':
    Tile(); Box((6,7,33,31),BROWN)
    for x in [8,14,20,26,32]: Line([(x,6),(x,32)],SILVER,2)
    Oval((16,14,28,25),YELLOW); Oval((14,12,22,20),BROWN)
  elif name=='bus_depot':
    Building(SILVER)
    Box((4,23,31,33),YELLOW)
    for x in [7,14,21]: Box((x,25,x+4,28),BLUE)
    Oval((7,31,12,36),INK); Oval((24,31,29,36),INK)
  elif name=='rail_depot':
    Building(RED)
    Poly([(2,9),(18,1),(37,9),(21,17)],DARK)
    Oval((15,11,23,19),WHITE); Line([(19,12),(19,15),(22,16)],INK,1)
    for y in [32,37]: Line([(1,y),(39,y)],SILVER,2)
  elif name=='subway_station':
    Poly([(3,17),(24,9),(38,19),(17,32)],DARK)
    for y in [20,24,28]: Line([(10,y),(22,y-4)],SILVER,2)
    Line([(3,27),(3,13),(17,7),(17,20)],GREEN,3)
    Box((21,2,37,15),GREEN)
    Line([(25,5),(25,11),(33,11),(33,5)],WHITE,2)
  elif name.startswith('dispatch_'):
    color={'dispatch_police':BLUE,'dispatch_fire':RED,'dispatch_military':GREEN}[name]
    Poly([(4,20),(11,12),(27,12),(34,21),(36,30),(4,30)],color)
    Box((12,14,25,20),WHITE); Box((15,9,23,12),RED)
    Oval((7,27,14,35),INK); Oval((26,27,33,35),INK)
    Arrow(15,1,'right')
  elif name in ['police','fire','hospital','prison']:
    Building(BLUE if name=='police' else RED if name=='fire' else SILVER)
    Badge({'police':'star','fire':'fire','hospital':'+','prison':'bars'}[name],BLUE if name=='police' else RED if name=='fire' else GREEN if name=='prison' else RED)
  elif name in ['school','college','library','museum','city_hall','mayors_house']:
    Building(BROWN if name in ['school','college','library'] else SILVER,
             'columns' if name in ['museum','city_hall'] else 'roof',name=='college')
    if name=='school': Box((21,2,23,15),DARK); Poly([(23,2),(34,2),(31,7),(23,7)],YELLOW)
    if name=='library':
      Poly([(20,23),(27,21),(34,23),(34,35),(27,33),(20,35)],WHITE); Line([(27,22),(27,33)],BROWN,1)
  elif name=='statue':
    Poly([(3,32),(20,25),(37,32),(20,39)],SILVER); Box((10,26,30,34),DARK)
    Poly([(17,11),(24,11),(28,28),(12,28)],'#399b53')
    Line([(19,14),(16,26)],'#9de4a1',1.5)
    Line([(21,16),(24,27)],'#155b34',2)
    Oval((16,2,25,12),'#53bc73')
    Line([(23,15),(31,6)],INK,5)
    Line([(23,14),(31,5)],'#53bc73',3.5)
    Oval((29,1,36,8),YELLOW)
  elif name=='llama_dome':
    draw.pieslice((3,4,37,38),180,360,fill=SILVER,outline=INK,width=2)
    Box((3,21,37,34),DARK); Oval((13,11,26,25),YELLOW)
    Line([(16,12),(15,6),(18,10),(23,6),(23,13)],YELLOW,3)
  elif 'arcology' in name:
    Building(GREEN if name=='forest_arcology' else BLUE,tall=True)
    if name=='launch_arcology': Arrow(29,2,'up',WHITE)
    elif name=='darco_arcology': Oval((11,4,28,16),RED)
    else: Poly([(7,12),(21,2),(33,12)],YELLOW)
  else: raise ValueError(name)


def Main(name):
  mapping={'bulldoze':'demolish','landscape':'trees',
    'commercial':'dense_commercial','industrial':'dense_industrial','rail':'rail',
    'ports':'seaport',
    'public_services':'police','recreation':'big_park','rewards':'statue','emergency':'dispatch_fire'}
  if name in mapping: Tool(mapping[name]); return
  if name=='residential':
    # The consolidated Zoning button combines the three established zone colors.
    Tile('#496447')
    for x,top,color in [(3,14,GREEN),(15,3,BLUE),(27,10,YELLOW)]:
      Box((x,top,x+10,33),color)
      Line([(x+2,top+2),(x+8,top+2)],WHITE,1)
      for y in range(top+5,31,5):
        for col in [x+2,x+6]: Box((col,y,col+2,y+2),DARK,None)
  elif name=='roads':
    # Road and railway share one Transport category, both drawn upright.
    Box((2,1,19,39),DARK)
    for x in [4,17]: Line([(x,2),(x,38)],SILVER,1.6)
    for y in [4,14,24,34]: Box((9,y,12,y+5),WHITE,None)
    Box((22,1,38,39),'#393f42')
    for y in range(3,38,5): Box((23,y,37,y+3),BROWN)
    for x in [26,34]:
      Line([(x,1),(x,39)],INK,4)
      Line([(x,1),(x,39)],SILVER,2.4)
      Line([(x-.6,2),(x-.6,38)],WHITE,.8)
  elif name=='power':
    # Utilities: distinct electricity and water symbols share the full button.
    Bolt(12,2,RED)
    Line([(18,4),(7,18),(14,18),(8,31)],'#ffb5a2',1.3)
    Poly([(29,5),(21,20),(20,27),(22,34),(28,38),(35,36),(39,30),(38,23)],BLUE)
    draw.arc((23,19,37,35),140,260,fill='#b5efff',width=1.8)
  elif name=='education':
    Building('#ead6b0','roof')
    Box((18,1,20,15),DARK)
    Poly([(20,1),(35,1),(32,5),(35,9),(20,9)],YELLOW)
    Line([(22,2),(32,2)],WHITE,1)
    Badge('star',BLUE)
  elif name=='water':
    # Metal tap, red handle and blue drop, matching the construction symbol.
    Line([(5,22),(5,15),(27,15),(33,20),(33,25)],INK,10)
    Line([(5,22),(5,15),(27,15),(33,20),(33,25)],SILVER,7)
    Line([(4,20),(4,14),(26,14),(31,18)],WHITE,1.2)
    Box((2,19,8,28),SILVER)
    Box((17,8,22,16),SILVER)
    Box((10,4,30,8),RED)
    Line([(12,5),(28,5)],'#ffc0a1',1.1)
    Poly([(33,26),(29,32),(29,36),(33,38),(37,35),(36,32)],BLUE)
    Line([(32,31),(31,34)],WHITE,1)
  elif name=='pause':
    for x in [5,23]:
      Box((x,2,x+11,38),YELLOW,width=2)
      Box((x+3,5,x+7,34),'#ffe96d',None)
      Line([(x+2,4),(x+2,35)],WHITE,1)
  elif name=='speed':
    for x in [1,20]:
      Poly([(x,2),(x+18,20),(x,38)],YELLOW)
      Line([(x+2,6),(x+2,34)],WHITE,1.5)
      Line([(x+3,34),(x+15,21)],'#9c5c00',1.5)
  elif name=='file':
    Box((3,2,37,38),'#174ed1',width=2)
    Box((8,3,31,16),SILVER); Box((9,22,31,37),WHITE)
    Box((24,4,29,14),INK); Box((26,5,28,13),DARK,None)
    Box((12,24,28,26),RED,None)
    for y in [29,32,35]: Line([(12,y),(28,y)],'#657d91',.8)
  elif name=='settings':
    points=[]
    for tooth in range(8):
      for offset,radius in [(-15,14),(-12,19),(12,19),(15,14)]:
        a=math.radians(tooth*45+offset)
        points.append((20+radius*math.cos(a),20+radius*math.sin(a)))
    Poly(points,SILVER)
    Oval((8,8,32,32),SILVER,width=1.5)
    Oval((12,12,28,28),DARK,width=1.5); Oval((15,15,25,25),'#647c94')
    Line([(10,18),(12,13),(17,10)],WHITE,1)
  elif name=='budget':
    for x,y,count in [(2,28,2),(15,20,4),(26,10,6)]:
      for i in range(count):
        Oval((x,y-i*3,x+12,y-i*3+7),YELLOW)
        Line([(x+2,y-i*3+4),(x+2,y-i*3+5)],WHITE,1)
      Oval((x+3,y-(count-1)*3+1,x+9,y-(count-1)*3+4),'#ffe879',None)
    Arrow(6,7,'right',GREEN)
  elif name=='ordinances': Building(SILVER,'columns')
  elif name=='graphs': Paper('graph')
  elif name=='reports':
    # Meet: an advisor portrait, distinct from reports and construction buildings.
    Poly([(1,39),(3,29),(11,24),(29,24),(37,29),(39,39)],BLUE)
    Poly([(10,25),(18,32),(15,39),(6,28)],SILVER)
    Poly([(30,25),(22,32),(25,39),(34,28)],SILVER)
    Poly([(17,30),(23,30),(22,34),(24,39),(16,39),(18,34)],RED)
    Box((15,21,25,28),'#cf8d56',None)
    Oval((8,2,32,28),'#f0ba80')
    Poly([(8,14),(7,8),(11,2),(20,0),(29,3),(33,9),(31,15),(27,7),(19,5),(12,8)],BROWN)
    Line([(12,13),(17,13)],INK,1.3)
    Line([(23,13),(28,13)],INK,1.3)
    Oval((13,14,16,16),INK,None)
    Oval((24,14,27,16),INK,None)
    Line([(20,14),(18,20),(22,20)],'#a76134',1)
    Line([(15,23),(20,25),(25,23)],INK,1)
    Line([(13,22),(18,21),(22,21),(27,22)],BROWN,2)
  elif name=='newspaper':
    Paper(); Box((10,7,31,11),INK,None)
    Box((10,15,20,25),BLUE)
    Poly([(11,24),(14,19),(17,21),(19,17),(19,24)],GREEN,None)
    for y in [15,18,21,24,28,31,34]:
      Line([(23,y),(31,y)],INK,.7)
      if y>=28: Line([(10,y),(20,y)],INK,.7)
  elif name=='map':
    Poly([(2,6),(13,3),(25,7),(38,3),(38,32),(25,36),(13,32),(2,36)],GREEN)
    Line([(13,4),(13,32),(25,36),(25,8)],BROWN,1)
    Line([(4,13),(12,16),(17,13),(23,20),(32,20),(36,25)],BLUE,4)
    Line([(6,29),(20,6)],YELLOW,2)
  elif name in ['zoom_in','zoom_out']: pass  # Rasterizer inserts the exact OpenGFX2 pixels.
  elif name=='rotate':
    draw.arc((4,4,35,35),40,290,fill=YELLOW,width=5)
    Poly([(20,1),(32,3),(26,13)],YELLOW)
    Tile(SILVER)
  elif name=='layers':
    for y,color in [(27,DARK),(19,BLUE),(11,GREEN)]: Poly([(3,y),(20,y-8),(37,y),(20,y+8)],color)
  elif name=='help':
    Oval((1,1,39,39),SILVER,width=1.5); Oval((3,3,37,37),'#0567ec',width=1.2)
    draw.arc((5,5,35,35),190,300,fill='#94edff',width=2)
    Line([(12,13),(15,9),(24,9),(28,13),(27,18),(21,23),(21,27)],INK,5)
    Line([(11,12),(14,8),(23,8),(27,12),(26,17),(20,22),(20,26)],WHITE,3.5)
    Box((18,30,23,35),INK,None); Box((18,29,22,33),WHITE,None)
  elif name=='switch':
    Arrow(18,3,'right',YELLOW)
    Poly([(3,29),(11,22),(11,26),(35,26),(35,32),(11,32),(11,36)],BLUE)
    Line([(5,29),(10,25)],WHITE,1)
  else: raise ValueError(name)


if __name__ == '__main__':
  tools=re.search(r'typedef enum CityCanvasTool \{\n  CITY_CANVAS_TOOL_RESIDENTIAL.*?\} CityCanvasTool;',
      (ROOT/'src/ui/tools/types.h').read_text(),re.S).group()
  tool_names=[n.lower() for n in re.findall(r'CITY_CANVAS_TOOL_(\w+) = \d+',tools) if n!='COUNT']
  main=re.search(r'typedef enum CityTopbarButton \{\n  CITY_TOPBAR_PAUSE.*?\} CityTopbarButton;',
      (ROOT/'src/ui/menus/menu.h').read_text(),re.S).group()
  main_names=[n.lower() for n in re.findall(r'CITY_TOPBAR_(\w+)',main)
      if n not in ('COUNT', 'ROTATE_COUNTERCLOCKWISE')]
  icons=[]
  for name,primary in [(n,True) for n in main_names]+[(n,False) for n in tool_names]+[('disasters',False)]:
    draw=VectorDraw()
    if primary: Main(name)
    elif name=='disasters': Flame(20,3,26)
    else: Tool(name)
    icons.append(''.join(draw.elements))
  assert len(icons)==98
  disabled_names=['zoom_in', 'zoom_out', 'rewards']
  for name in disabled_names:
    draw=VectorDraw()
    Main(name)
    icons.append('<g opacity=".45" filter="url(#disabled)">'+''.join(draw.elements)+'</g>')
  draw=VectorDraw()
  Main('switch')
  icons.append(''.join(draw.elements))
  OUT.mkdir(parents=True,exist_ok=True)
  definitions=['<clipPath id="iconclip"><rect width="40" height="40"/></clipPath>']
  definitions.append('<filter id="disabled"><feColorMatrix type="saturate" values="0"/></filter>')
  for name,light,base,dark in VectorDraw.gradients.values():
    definitions.append(f'<linearGradient id="{name}" x2=".35" y2="1"><stop stop-color="{light}"/>'
        f'<stop offset=".4" stop-color="{base}"/>'
        f'<stop offset="1" stop-color="{dark}"/></linearGradient>')
  definitions.append('<filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">'
      '<feDropShadow dx=".35" dy=".7" stdDeviation=".15" flood-opacity=".75"/></filter>')
  svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="{len(icons)*40}" height="40" '
  svg+=f'viewBox="0 0 {len(icons)*40} 40"><defs>'+''.join(definitions)+'</defs>\n'
  for i,icon in enumerate(icons):
    svg+=f'<g transform="translate({i*40} 0)" stroke-linejoin="round" stroke-linecap="round" '
    svg+='clip-path="url(#iconclip)"><g filter="url(#shadow)">'+icon+'</g></g>\n'
  (OUT/'icons.svg').write_text(svg+'</svg>\n')
  names=main_names+tool_names+['disasters']+[name+'_disabled' for name in disabled_names]+['switch']
  (OUT/'icons.txt').write_text('\n'.join(f'{i:02} {name}' for i,name in enumerate(names))+'\n')
  print(f'Wrote {len(icons)} vector icons; rasterize with tools/generate_map_icons.cjs')
