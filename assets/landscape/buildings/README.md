# Building artwork

Kinds 112–197 use original ImageGen paintings based on the Tile Info descriptions and the
functional/silhouette notes in `docs/building-artwork-visual-notes.md`. Source paintings and
their prompts are kept in each kind's directory. These are new designs, not reproductions of
stock sprites. `artwork.json` lists the actual baked sets and their status.

Approved single-view paintings alternate the original and its horizontal mirror across the
four directions. Their manifests set `mirror` for 90 and 270 degrees; the resource pack shares
the image bytes and stores the flip in its references. Authored directional artwork keeps its
own views. Smoke emitters follow the same mirroring when packed.

Finished and abandoned buildings have four clockwise views. The eight generic construction
kinds share one source view across camera rotations. All views of a kind share their canvas,
baseline, and exact native ground mask: 128, 256, or 384 pixels wide for 1×1, 2×2, or 3×3 sites.
`bake-result.json` records ownership checks for every view. A concave building is allowed inside
a site; a concave foundation is not.

## Art direction

`height-references.json` records the original ground-anchor-to-top distance for all 144 native
building kinds, scaled from 32 to 128 pixels per tile. Geometry guides constrain the highest roof
point across all four projected views, rather than assuming the roof lies at the plot centre or
inheriting height from a generated painting. Keep a deliberate
mix of low sites, mid-rise blocks and towers. Correct excessive storey counts in the artwork;
do not squeeze roof perspective or enlarge the footprint to disguise an oversized building.

Audit the actual four baked alpha masks with
`python3 tools/bake_building_sprites.py --audit-heights /tmp/building-heights.json`.
The report flags any view more than 10% plus two filtering pixels above its stock reference.
This is an offline review report; recording a reference does not itself correct installed artwork.
The references can be regenerated from resource-only `LARGE.DAT` and `PAL_MSTR.BMP` with
`--height-reference-atlas` and `--height-reference-palette`. Original sprite images are not redistributed.

The building is the subject. Put detail into its architecture, roof, windows, materials and
functional equipment. Surrounding ground should usually be simple paving, gravel or exposed
earth, suitable for both temperate and desert landscapes. Do not surround unrelated buildings
with flowerbeds, hedges, ornamental trees or lush grass. Industrial and ordinary commercial
sites should have functional yards; homes may have a small restrained planted area. Vegetation
must not hide walls, doors or the building silhouette. Reserve substantial greenery for a kind
whose description actually calls for it, such as a park or a garden.

Keep the base artwork sparse. Additional greenery and snow can later use separate, precisely
registered overlays for each rotation, authored offline and drawn through cached sprite passes.
They are separate art layers, not runtime plant generation or roof detection. Those optional
passes are not implemented by the current building replacement.

Setbacks vary by design: urban frontages can reach the side and rear boundaries, while detached
homes and campuses retain space around them. `site.json` records each design's layout and ground
choice. Exposed side and rear facades should also have windows; blank party walls are a deliberate
design exception, rather than a blanket rule for urban plots. Window placement is authored in the
four matching sprites and does not depend on neighbouring tiles at runtime.
Standard paving belongs to the shared stone/concrete ground tiles, not to the housing
painting. A registered ImageGen `foreground-turnaround.png` removes that ordinary ground; the
baker's `--split-foreground` option retains the full-site painting only for coordinate calibration.
`terrain` ground reveals the map's actual climate-dependent terrain. `stone` and `concrete`
select reusable tiles composited beneath the extracted building when packing. Unique steps and ramps remain in
the building artwork. No extra per-tile gameplay or save fields are needed.

During packing, fixed stone/concrete paving is composited beneath the foreground at the exact
native tile anchors. Each completed four-view set records whether its sprites fully cover their
ground footprint, using a flag in the existing ground metadata byte. On level dry sites the
renderer skips those covered ground diamonds, while retaining exposed terrain sides. Hidden
buildings, custom artwork and terrain-dependent sites still draw their required ground.
Source foregrounds and reusable ground PNGs remain separate for editing; no extra sprite copy
or per-tile coverage buffer is retained at runtime.

Generate the four views against `layout-turnaround.png`, using `design-reference.png` only for
architectural identity and materials. The geometry guide fixes component placement and genuine
quarter turns; copying the reference's landscaping is not part of preserving its identity.

Bake the sources offline with `tools/bake_building_sprites.py DIRECTORY ...`. It reuses the civic
foundation baker to calibrate the projection, preserve verticals, filter premultiplied colour,
and apply the authoritative union of terrain tile masks. The packer admits complete view sets
and rechecks their dimensions, masks, and site extents. The runtime uses cached sprite streams.

## Shared construction kit

`construction/` contains nine ImageGen source paintings: a foundation and one timber/steel
structural floor for each footprint. `tools/bake_building_sprites.py --construction` calibrates
these into three foundations and six transparent floor modules. The landscape packer stacks
the floors over their matching foundation offline at six height profiles and three completion
levels: 111 shared sprites total. Each frame includes the opaque construction soil, so a visible
site uses one sprite and skips covered flat ground diamonds. The packer verifies exact footprint
coverage for every frame and stores a construction-coverage flag in the existing ground byte.
Terrain sides, water, uneven ground and hidden/custom artwork retain their ground drawing.
These are shared by the 70 finished residential, commercial, and industrial kinds.

New zoned sites select their finished building immediately. Progress 31 shows foundations;
23, 15 and 7 show progressively taller framing; 0 switches to the finished building.
Construction never draws masked portions of the completed artwork. The finished RGB artwork
is stored once per rotation. There is no runtime model generation, texture lookup for geometry,
shading, or per-frame allocation.

The Advanced Options entry **Building construction stages** is on by default. Disabling it
uses stock construction for new sites, while existing projects continue. Stock comparison
explicitly disables the option and uses original artwork. Current native saves retain the
future kind and progress; stock exports project unfinished sites to generic construction.
