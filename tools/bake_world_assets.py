#!/usr/bin/env python3
"""Bake authored world details and replacement catalog slots; reads no SimCity installation.

Blender renders, ImageGen paintings and pinned OpenGFX2 trains are development sources.
Runtime uses only the normal landscape pack. Trains reuse retired creature-piece slots.
"""
import hashlib
import json
import math
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageOps
import numpy as np
from pack_landscape_assets import row_runs

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/landscape/world'
CATALOG=ROOT/'assets/landscape/opengfx2/sprites.json'


def Clean(image):
  image=image.convert('RGBA')
  alpha=image.getchannel('A').point(lambda a:0 if a<=12 else 255 if a>=240 else a)
  image.putalpha(alpha)
  return image


def Downsample(image, size):
  color=image.convert('RGB').resize(size,Image.Resampling.LANCZOS)
  alpha=image.getchannel('A').resize(size,Image.Resampling.BOX)
  color.putalpha(alpha)
  return Clean(color)


def RubbleGround(variant):
  from build_landscape_assets import ReadGroundBoundaries128
  image=Clean(Image.open(OUT/'sources/rubble-ground.png'))
  image=image.crop(image.getchannel('A').point(lambda a:255 if a>32 else 0).getbbox())
  if variant&1:image=ImageOps.mirror(image)
  if variant&2:image=ImageOps.flip(image)
  image=Downsample(image,(128,64))
  # Register the full surface at the same anchor and pixel ownership as terrain.
  # Brown soil fills the painting's edge coverage before the binary diamond mask.
  canvas=Image.new('RGBA',(128,128),(100,72,45,255))
  canvas.alpha_composite(image,(0,64))
  canvas.putalpha(Image.fromarray(ReadGroundBoundaries128()[0].astype('uint8')*255))
  # Keep the shared full-tile base, with a shallow debris heap rising above it.
  heap=Clean(Image.open(OUT/'sources/rubble.png'))
  heap=heap.crop(heap.getchannel('A').point(lambda a:255 if a>32 else 0).getbbox())
  if variant&1:heap=ImageOps.mirror(heap)
  heap=Downsample(heap,(112,round(heap.height*112/heap.width)))
  soil=Image.new('RGBA',heap.size,(100,72,45,255))
  soil.alpha_composite(heap)
  soil.putalpha(heap.getchannel('A').point(lambda a:255 if a>=128 else 0))
  canvas.alpha_composite(soil,(8,104-heap.height))
  return canvas


def RadioactiveGround():
  from build_landscape_assets import GroundProjection128, ReadGroundBoundaries128
  source=Clean(Image.open(OUT/'sources/waste.png'))
  source=source.crop(source.getchannel('A').point(lambda a:255 if a>32 else 0).getbbox())
  scale=min(112/source.width,52/source.height)
  source=Downsample(source,(round(source.width*scale),round(source.height*scale)))
  soil=Image.open(ROOT/'assets/landscape/opengfx2/sprites/0542.png').convert('RGBA')
  flat=ImageOps.colorize(ImageOps.grayscale(soil),(43,35,22),(132,117,65)).convert('RGBA')
  flat.alpha_composite(source,((128-source.width)//2,70))
  # Bake source coverage into opaque dirt. Runtime runs contain RGB and skips,
  # never per-pixel alpha. Each prepared grade is still a single surface sprite.
  pixels=np.asarray(flat.convert('RGB'))
  areas=ReadGroundBoundaries128()
  variants=[]
  for shape in range(14):
    u,v,_=GroundProjection128(shape)
    x=np.clip(np.floor(64+64*(v-u)).astype(int),0,127)
    y=np.clip(np.floor(64+32*(u+v)).astype(int),0,127)
    image=Image.fromarray(pixels[y,x],'RGB').convert('RGBA')
    image.putalpha(Image.fromarray(areas[shape].astype('uint8')*255))
    image.save(OUT/f'waste-ground-{shape:02}.png',optimize=True)
    variants.append(image)
  return variants


def SubwayRailPortal(direction):
  # Use the ordinary rail gauge and tile projection. The exposed end is south,
  # east, north or west, matching the stored subway-to-rail tile variant.
  detail=4
  image=Image.new('RGBA',(128*detail,128*detail))
  draw=ImageDraw.Draw(image)
  faces=[]
  def World(u,v,z):
    for _ in range(direction):u,v=v,1-u
    return u,v,z
  layer=0
  def Face(points,color):
    points=[World(*p) for p in points]
    depth=sum(u+v for u,v,z in points)/len(points)
    faces.append((layer,depth,points,color))
  def Floor(v):return -10*(1-v)/.75
  # Recessed ballast, with stone grain shared with the ordinary railway source.
  material=np.asarray(Image.open(ROOT/'assets/landscape/rails/ballast-material-master.png').convert('RGB').resize((32,36)))
  for row in range(36):
    v=.25+row*.75/36;end=v+.75/36
    for col in range(32):
      u=.18+col*.64/32;right=u+.64/32
      color=material[row%material.shape[0],col%material.shape[1]]
      shade=.45+.55*(v-.25)/.75
      Face([(u,v,Floor(v)),(right,v,Floor(v)),(right,end,Floor(end)),(u,end,Floor(end))],
          tuple(int(c*shade) for c in color))
  # Sleepers and running faces descend continuously into the dark portal.
  layer=1
  for n in range(8):
    v=.29+n*.095;end=v+.027
    Face([(.255,v,Floor(v)+.3),(.745,v,Floor(v)+.3),
        (.745,end,Floor(end)+.3),(.255,end,Floor(end)+.3)],(101,80,56))
  for u in (.355,.645):
    for n in range(24):
      v=.25+n*.75/24;end=v+.75/24
      shade=.4+.6*(v-.25)/.75
      Face([(u-.012,v,Floor(v)+.7),(u+.012,v,Floor(v)+.7),
          (u+.012,end,Floor(end)+.7),(u-.012,end,Floor(end)+.7)],
          tuple(int(c*shade) for c in (197,195,181)))
  layer=2
  for u,outer,color in ((.18,.11,(105,108,102)),(.82,.89,(155,155,140))):
    Face([(u,.25,Floor(.25)),(u,1,0),(u,1,3),(u,.25,3)],color)
    Face([(outer,.25,3),(outer,1,3),(u,1,3),(u,.25,3)],(190,188,169))
    Face([(outer,.25,0),(outer,1,0),(outer,1,3),(outer,.25,3)],(121,123,113))
  # A low concrete lintel and dark opening clearly distinguish this from rubble.
  Face([(.18,.25,-10),(.82,.25,-10),(.82,.25,5),(.18,.25,5)],(23,27,28))
  Face([(.11,.20,5),(.89,.20,5),(.89,.31,5),(.11,.31,5)],(194,191,173))
  Face([(.11,.31,1),(.89,.31,1),(.89,.31,5),(.11,.31,5)],(124,128,121))
  for layer,depth,points,color in sorted(faces,key=lambda face:face[:2]):
    draw.polygon([(round((64+64*(v-u))*detail),round((64+32*(u+v)-z)*detail))
        for u,v,z in points],fill=(*color,255))
  return Downsample(image,(128,128))


def UnpackDemolition():
  data=(ROOT/'assets/landscape/opengfx2/demolition.dat').read_bytes()
  magic,count=struct.unpack_from('<8sI',data)
  if magic!=b'SC2KLAN2':raise ValueError('Unexpected demolition pack')
  at=32; frames=[]
  for _ in range(count):
    ident,w,h,baseline,scale,x,size=struct.unpack_from('<5IiI',data,at);at+=28
    image=Image.new('RGBA',(w,h))
    for y,start,kind,length,pixels in row_runs(data[at:at+size],h):
      if kind==0:continue
      if kind not in (1,2):raise ValueError('Demolition must use native RGB')
      stride=3 if kind==1 else 4
      for i in range(length):
        rgb=tuple(pixels[i*stride:i*stride+3]);alpha=255 if kind==1 else pixels[i*stride+3]
        image.putpixel((start+i,y),(*rgb,alpha))
    frames.append((image,baseline,scale,x));at+=size
  return frames


def Symbol(kind):
  image=Image.new('RGBA',(128,128));d=ImageDraw.Draw(image)
  if kind=='power':
    d.polygon([(74,8),(39,68),(62,68),(48,118),(94,49),(70,49)],fill=(255,195,35),outline=(88,55,9),width=4)
  elif kind in ('positive','negative'):
    color=(65,164,97,190) if kind=='positive' else (202,71,58,190)
    d.polygon([(64,64),(127,96),(64,127),(0,96)],fill=color)
    d.line([(46,96),(82,96)],fill=(245,246,227),width=5)
    if kind=='positive':d.line([(64,78),(64,113)],fill=(245,246,227),width=5)
  return image


def SoilPipe(shape, pipes=True):
  base=Image.open(ROOT/'assets/landscape/opengfx2/sprites/0542.png').convert('RGBA')
  gray=ImageOps.grayscale(base)
  image=ImageOps.colorize(gray,(28,26,24),(145,130,103)).convert('RGBA');image.putalpha(base.getchannel('A'))
  if not pipes:return image
  image=image.resize((512,512),Image.Resampling.NEAREST);d=ImageDraw.Draw(image)
  center=(256,384);ends=[(128,320),(384,320),(384,448),(128,448)]
  masks=(10,5,5,10,5,10,12,6,3,9,13,14,7,11,15)
  mask=masks[shape%15]
  for side,end in enumerate(ends):
    if not mask&(1<<side):continue
    d.line([center,end],fill=(29,45,54),width=29)
    d.line([(x,y-4) for x,y in (center,end)],fill=(106,150,171),width=21)
    d.line([(x,y-9) for x,y in (center,end)],fill=(184,205,203),width=5)
  return Downsample(image,(128,128))


def main():
  OUT.mkdir(parents=True,exist_ok=True)
  original=json.loads(CATALOG.read_text());base={r['id']:r for r in original}
  records={};provenance={}
  def Add(ident,image,baseline=None,scale=4,offset=0,origin='Project artwork'):
    image=Clean(image);file=OUT/f'{ident:04}.png';image.save(file,optimize=True)
    records[ident]=dict(id=ident,file=f'../world/{file.name}',baseline=image.height if baseline is None else baseline,
        scale_divisor=scale,offset_x=offset,civic=True)
    provenance[ident]=origin
  def Alias(ident,entry,origin='Authored or OpenGFX2 replacement'):
    records[ident]=dict(entry,id=ident,civic=True);provenance[ident]=origin
  Add(0,Image.new('RGBA',(128,128)),128,origin='Intentionally empty: unused catalog slot')
  for ident in [*range(400,450),*range(478,490),385]:
    Alias(ident,records[0],'Removed obsolete palette traffic or composite creature part')
  for ident in range(1,5):
    Add(ident,RubbleGround(ident-1),128,
        origin='Original ImageGen full-tile rubble painting baked onto opaque brown ground')
    records[ident]['opaque']=True
  Add(5,RadioactiveGround()[0],128,
      origin='Original ImageGen low waste canisters baked onto opaque contaminated ground')
  for ident in range(108,112):
    Add(ident,SubwayRailPortal(ident-108),128,
        origin='Authored recessed rail ramp and concrete subway portal; native rail gauge and projection')
  # Whole-tile power catalog fallbacks now borrow the independently authored tower components.
  for ident in [*range(14,29),92]:Alias(ident,base[4245+(ident-14)%14], 'Original project lattice-tower raster')
  for ident,road in [(67,29),(68,30),(71,44),(72,45),(79,73),(80,74)]:
    Alias(ident,base[road], 'Native transport surface; the renderer composes authored power components')
  for ident in range(305,354):
    Add(ident,SoilPipe(ident-319,ident>=319),128,origin='Authored compatibility underground ground or pipe')
  for ident in range(450,468):Add(ident,SoilPipe(ident-450),128,origin='Authored compatibility utility overlay')
  records[92]['baseline']-=32
  colors=[(53,132,65),(53,92,187),(182,150,46),(178,95,46),(119,135,142)]
  for ident,color in enumerate(colors,354):
    image=Image.new('RGBA',(512,512));d=ImageDraw.Draw(image)
    d.line([(256,256),(508,384),(256,508),(0,384),(256,256)],fill=(*color,255),width=8)
    Add(ident,Downsample(image,(128,128)),128,origin='Authored antialiased zone boundary')
  source=json.loads((OUT/'blender/manifest.json').read_text())
  mapping={'airplane':359,'helicopter':364,'ship':369,'engine':374,'coach':478,
      'sailboat':380,'bulldozer':390,'drawbridge-closed':88,'drawbridge-open':89,'crowd':493,'rescue':495,'crawler':490}
  for frame in source['frames']:
    model,view=frame['model'],frame['view']
    if frame.get('rotor_frame',0) or model in ('engine','coach'):continue
    if model=='rescue' and view!=2:continue
    ident=495 if model=='rescue' else mapping[model]+view
    image=Image.open(OUT/'blender'/frame['file']).convert('RGBA')
    image=Downsample(image,tuple(frame['canvas']))
    bbox=image.getchannel('A').getbbox()
    if not bbox:raise ValueError(f'Empty vehicle render {frame["file"]}')
    if bbox[0]==0 or bbox[1]==0 or bbox[2]==image.width or bbox[3]==image.height:
      raise ValueError(f'Clipped vehicle render {frame["file"]}: {bbox}')
    # Preserve one common world origin and pixel pitch, never fit per-heading silhouettes.
    half=max(frame['anchor'][0]-bbox[0],bbox[2]-frame['anchor'][0])+2
    box=(frame['anchor'][0]-half,bbox[1]-2,frame['anchor'][0]+half,max(bbox[3]+2,frame['anchor'][1]))
    baseline=frame['anchor'][1]-box[1]
    if ident in (88,89):baseline-=32
    Add(ident,image.crop(box),baseline,origin='Original Blender model, registered orthographic render')
  for model,views,first in [('helicopter',range(5),400),('rescue',[2],440)]:
    for bank,view in enumerate(views):
      phases=sorted((f for f in source['frames'] if f['model']==model and f['view']==view),
          key=lambda f:f.get('rotor_frame',0))
      if [f.get('rotor_frame',0) for f in phases]!=list(range(8)):
        raise ValueError(f'Missing rotor phases: {model} view {view}')
      images=[Downsample(Image.open(OUT/'blender'/f['file']).convert('RGBA'),tuple(f['canvas'])) for f in phases]
      boxes=[image.getchannel('A').getbbox() for image in images]
      anchor=phases[0]['anchor']
      half=max(max(anchor[0]-box[0],box[2]-anchor[0]) for box in boxes)+2
      box=(anchor[0]-half,min(b[1] for b in boxes)-2,anchor[0]+half,max(max(b[3]+2 for b in boxes),anchor[1]))
      for phase,image in enumerate(images):
        Add(first+bank*8+phase,image.crop(box),anchor[1]-box[1],
            origin='Original Blender model; eight registered main and tail rotor phases')
  Alias(379,records[380],'Original sailing boat; retained event wake variant')
  # Dispatch markers reuse project-owned toolbar paintings, without a second art style.
  icons=(ROOT/'assets/ui/toolbar/icons.dat').read_bytes()
  magic,count,headers,variant_size,header_offset=struct.unpack_from('<8sIIII',icons)
  if magic!=b'SC2KICO3':raise ValueError('Invalid native icon pack')
  for ident,index in [(382,82),(383,83),(384,84)]:
    reference=struct.unpack_from('<H',icons,24+index*2)[0]
    size,offset,length=struct.unpack_from('<III',icons,header_offset+((reference&32767)-1)*12)
    image=Image.new('RGBA',(size,size))
    for y,x,kind,count,pixels in row_runs(icons[offset:offset+length],size):
      if kind==0:continue
      if kind not in (1,2):raise ValueError('Interface artwork must use fixed RGB')
      stride=3 if kind==1 else 4
      for i in range(count):
        color=tuple(pixels[i*stride:i*stride+3])+(255 if kind==1 else pixels[i*stride+3],)
        image.putpixel((x+i,y),color)
    if reference&32768:image=ImageOps.mirror(image)
    Add(ident,Downsample(image,(48,48)),48,origin='Project dispatch toolbar artwork')
  Add(386,Downsample(Symbol('power'),(24,24)),24,origin='Authored electricity warning')
  demolition=UnpackDemolition()
  for ident,frame in [(387,17),(388,20),(389,24)]:
    image,baseline,scale,offset=demolition[frame]
    Add(ident,image,baseline,scale,0,origin='OpenGFX2 GPL-2.0 explosion frame')
  for ident,frame in enumerate((2,6,10,14),392):
    image=Image.open(ROOT/f'assets/landscape/civic/power-smoke/smoke-{frame:02}.png').convert('RGBA')
    gray=ImageOps.grayscale(image);dust=ImageOps.colorize(gray,(63,54,41),(164,148,118)).convert('RGBA')
    dust.putalpha(image.getchannel('A'));Add(ident,dust,110,origin='Original ImageGen smoke, dust material bake')
  strip=Image.open(OUT/'sources/fire-strip.png').convert('RGBA')
  cell=strip.width//4
  for frame in range(4):
    image=Downsample(strip.crop((frame*cell,0,(frame+1)*cell,strip.height)),(128,round(strip.height*128/cell)))
    canvas=Image.new('RGBA',(128,192));canvas.alpha_composite(image)
    Add(396+frame,canvas,144,origin='Original ImageGen four-phase fire animation')
  for n in range(8):
    image=Image.new('RGBA',(128,128));d=ImageDraw.Draw(image)
    color=(207-18*n,214-17*n,231-10*n,255)
    d.polygon([(64,64),(127,96),(64,127),(0,96)],fill=color)
    Add(468+n,image,128,origin='Authored data-overlay color scale')
  Add(476,Symbol('positive'),128,origin='Authored growth indicator')
  Add(477,Symbol('negative'),128,origin='Authored decline indicator')
  Alias(492,base[270],'OpenGFX2 water surface')
  records[492]['civic']=False
  smoke=Image.open(ROOT/'assets/landscape/civic/power-smoke/smoke-08.png').convert('RGBA')
  Add(496,smoke,110,origin='Original ImageGen pollution plume')
  tornado=Clean(Image.open(OUT/'sources/tornado.png'))
  box=tornado.getchannel('A').point(lambda a:255 if a>24 else 0).getbbox();tornado=tornado.crop(box)
  tornado=Downsample(tornado,(192,256))
  for frame in range(3):
    Add(497+frame,ImageOps.mirror(tornado) if frame==1 else tornado,250,origin='Original ImageGen storm funnel')
  trains=json.loads((OUT/'opengfx2-trains.json').read_text())
  for entry in trains['records']:
    Alias(entry['id'],entry,'OpenGFX2 SH 125 locomotive and passenger coach, GPL-2.0; see opengfx2-trains.json')
  (OUT/'catalog.json').write_text(json.dumps([records[k] for k in sorted(records)],indent=2)+'\n')
  (OUT/'provenance.json').write_text(json.dumps(dict(
      sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((OUT/'sources').glob('*.png'))},
      slots={str(k):provenance[k] for k in sorted(provenance)},
      imports={'opengfx2-trains':'opengfx2-trains.json'}),indent=2)+'\n')
  # Normal-size development review, never packaged.
  ids=[1,2,3,4,5,359,360,361,362,363,364,367,370,374,375,377,478,479,380,390,382,383,384,386,
      387,392,396,397,398,399,490,491,493,495,496,497]
  preview=Image.new('RGB',(960,math.ceil(len(ids)/6)*200),(105,111,101));d=ImageDraw.Draw(preview)
  for i,ident in enumerate(ids):
    entry=records[ident]
    image=Image.open(CATALOG.parent/entry['file']).convert('RGBA')
    image=image.crop(image.getchannel('A').getbbox())
    image.thumbnail((148,170),Image.Resampling.LANCZOS);x=i%6*160+(160-image.width)//2;y=i//6*200+22
    preview.paste(image,(x,y),image);d.text((i%6*160+5,i//6*200+4),str(ident),fill='white')
  preview.save(OUT/'preview.png')
  rubble=Image.new('RGBA',(512,320),(105,111,101,255))
  for diagonal in range(7):
    for u in range(4):
      v=diagonal-u
      if 0<=v<4:
        tile=Image.open(OUT/f'{1+(u+v*3)%4:04}.png').convert('RGBA')
        rubble.alpha_composite(tile,(192+(u-v)*64,(u+v)*32))
  rubble.save(OUT/'rubble-preview.png',optimize=True)
  print(f'Baked {len(records)} catalog slots from authored artwork and OpenGFX2 sources')


if __name__=='__main__':main()
