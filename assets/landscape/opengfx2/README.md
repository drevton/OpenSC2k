# OpenGFX2 landscape artwork

OpenGFX2 artwork by Richard Wheeler (Zephyris) and OpenGFX2 contributors, GPL-2.0.
Source: https://github.com/OpenTTD/OpenGFX2
Revision: 7a17fb370440aed7f2586715d606e9fe190b9dd3
See LICENSE. The upstream source at that revision is the editable artwork.

Each sprite is checked in as an individual PNG, with IDs and anchors in
`sprites.json`. Roads and railway entries reference their existing individual PNGs;
the other images live in `sprites/`. These are the canonical runtime artwork sources.
Normal CMake builds run `tools/pack_landscape_assets.py` using only these checked-in
files and Pillow. No upstream downloads or original game artwork are needed to pack them.
The generated `landscape.dat` lives under `build/mac/generated-assets/landscape/opengfx2/`
(or the equivalent directory in another build tree) and is never checked in.

`tools/build_landscape_assets.py` regenerates these individual PNGs offline from the
upstream OpenGFX2 extra-zoom terrain, trees, water, shoreline and bridge artwork.
Ground, riverbanks, foundations, trees, water and coastal composites are packaged
at 128px tile detail. Infrastructure bridges retain their separate artwork.
`tools/build_bridge_assets.py` prepares road and rail bridge approaches from the OpenGFX2
wooden/general ramp sheets in all four directions, with rising and level bank approaches.
Side beams are moved outward to fit the native
deck width without stretching their textures. These approaches include their own bridge structure;
flat ground beneath them is not painted as a stone embankment. They share the bridge palette bank.
Retained stock rail bridges select OpenGFX2 cantilever pieces by distance to each
end of the span. The two stock rail IDs describe structural roles rather than
repeating OpenGFX2 end and middle fragments; tapered ends occur only at the banks.
Before exporting the images, the builder verifies finished ground, water, coast,
bank and terrain-face pixels against their declared per-shape masks. Complete
surfaces must fill their mask with alpha 255; overlays may leave holes but cannot
draw outside their mask. A violation fails the build with the sprite ID, first
invalid pixel and counts of spill, missing and translucent pixels. These checks
run on every generation. Trees, selectors, bridges and foundation objects keep
their separate template bounds rather than being clipped to a surface diamond.
The builder also checks that flat diamonds and all four constant-grade inclines
partition pixels without gaps or overlap at 16 integer-scale tile widths (8–256 pixels).
It also rejects overlap at every compatible corner-height join between the twenty
surface/underground shapes (272 joins), at those same widths. The map zoom stops
before tiles exceed 256 displayed pixels, including in extra viewports. Cached sprite scaling
uses the same nearest-neighbor samples, without expanding edge coverage.
Terrain uses the upstream grid overlay passes over the original 256px source
artwork, reduced to 128px with a premultiplied color filter. Coverage comes from
one 128px master boundary sheet, independent of the color source resolution.
It uses pixel centers and half-open map coordinates: a flat diamond advances
64px horizontally and 32px vertically between neighbors, with a 16px height step.
Roads, rail, highways, terrain, zones, coasts and water use these same masks.
Underground surfaces use the same projection and boundaries, with separate front walls.
The raised flat surface cap also includes its exposed front wall.
Color detail cannot expand the master boundary. Missing boundary colors are extended from
the nearest source pixel without stretching interior texture.
Alternate rough tiles use the same 127-row flat source crop and edge extension as
ordinary ground; shortening that crop or fitting columns stretches the baked grid.
Rebuild these sixteen variants with `tools/build_landscape_assets.py --source CACHE --rough-only`.
Empty zones use green, blue and yellow for residential, commercial and industrial.
All zones use transparent overlays with a subtle wash in their own color. Dense zoning uses closer dots
and a 15% darker shade for its wash, dots and borders.
The background wash uses 28% zone color for light zones and 36% for dense zones.
The renderer supplies the actual ground beneath them, preserving snow, desert, rough and brown cleared ground.
Round dots and diamond borders have anti-aliased coverage; cached zoning sprites interpolate color and opacity
when resized, while retaining the exact nearest-sampled master footprint. The existing preparation workspace
supplies the two temporary source rows; no extra runtime drawing buffers are retained.
Diamond borders use the same color as the dots: green for residential, blue for commercial
and yellow for industrial. Rebuild just these surfaces with
`python3 tools/build_landscape_assets.py --zones-only`; no upstream cache is needed. The builder uses upstream
`graphics/trees/shapeproc.py` for the tree rasters. Install Pillow, numpy and
blend_modes, then run:

    python3 tools/build_landscape_assets.py --source /path/to/opengfx2-cache

When changing the master boundaries, rebuild every dependent family in order:

    python3 tools/build_landscape_assets.py --source /path/to/opengfx2-cache --boundaries-only
    python3 tools/build_road_assets.py --structure-source /path/to/opengfx2-cache
    python3 tools/build_rail_assets.py
    python3 tools/build_landscape_assets.py --source /path/to/opengfx2-cache

The final landscape import exports terrain and water, records native infrastructure
PNG references, and validates coverage. No original installation is needed.
Commit the changed PNGs and `sprites.json`, then run `python3 tools/dev.py build`.
The packer can also be run directly:

    python3 tools/pack_landscape_assets.py --output /tmp/landscape.dat

Runtime loads the generated pack once through Assets. It performs no downloads, PNG
processing, or executable reads. Buildings use the authored building/civic banks;
vehicles, disasters and world details use `../world/catalog.json`. Optional comparison
tooling keeps its own original resource loader outside the native runtime.
Native height steps are one eighth of the tile width, as in OpenTTD; City
altitudes and saved map bytes are unchanged. Compatibility sprite IDs 305–353 now hold independently authored underground
rasters. Native underground drawing uses its separate prepared banks. Surface rocks use
the separate native ground banks. Native roads use the finished
[128-pixel road set](../roads/README.md), including wider pavement, rounded bends,
matching structure surfaces and new tunnel portals. Rail uses the matching
[128-pixel railway set](../rails/README.md), with new ballast, timber sleepers,
steel rails, switches and crossings. Its manifest loads after roads, replacing
railway-owned composites and adding smooth diagonal approaches. The four rail
inclines are generated directly on the gentler native slope plane. Power
line components are generated from the project lattice-tower geometry. All infrastructure
and water slope variants are packaged as finished sprites: runtime only selects
and uniformly scales them for zoom. Palette indices remain available for mapping
and animation.
Rail transition IDs 59..62 use the matching straight-track overlays. Their
original raised/bent ends were specific to SimCity's steep geometry; native
rail meets the prepared incline directly at the tile boundary. All surface rail
IDs 44..62 use one terrain-shape lookup, selecting the existing prepared incline
from XTER's low four bits. On flat surfaces the stored ID supplies the track
layout. Climbing and transition IDs become their corresponding flat straight
layout there. No neighboring rail connections select the rendered slope.
The original City IDs, simulation and save encoding remain unchanged.
Road and rail ground uses separate OpenGFX2 foundation sprites from
`graphics/foundations/256/foundations_temperate_32bpp.png`, cropped with
`template_foundations_base` and `template_foundations_slope_halftile` (the
extended block starts at y=82). OpenTTD's straight-road slope rules select a
natural incline, inclined foundation, or raised level surface. SimCity curves
and junctions use full level foundations for roads. Rail's terrain lookup selects
the existing inclined or level foundations for non-planar shapes, sharing the
same side-wall drawing. No additional artwork variants are needed.
The four upstream wall blocks omit
rear walls covered by the adjacent surfaces. Ground and infrastructure share
the resulting surface height with picking and selection diamonds.
SimCity stores independent cell base heights and slope shapes. To retain its
solid tile boundary, the builder also prepares fourteen front faces from the
upstream foundation texture. Infrastructure draws these below its ground;
adjoining foreground tiles cover them on continuous hills. This closes exposed
black gaps without changing City terrain or save data. All foundation crops
and front faces are prepared offline; drawing only selects cached sprites.
Ground below elevated highways uses these same exposed slope faces and vertical
skirts, including underneath bends, junctions and highway bridge spans.
Highway inclines retain their prepared deck profile independently of the ground.
Native drawing places each underlying ground cell at its own elevation and resolves
the deck base from its footprint, so highway placement can preserve a natural hillside.
Banks use `graphics/infrastructure/256/river_overlay{alpha,normal,shading}.png`,
with `template_waterfeature_flat` crops and OpenTTD's side/outer/inner-corner composition.
SC2's bank forms encode water connections; importing them as OpenTTD coastal ground
slopes incorrectly replaced most of their water with grass. Banks fit flat water-level
diamonds: SC2 positions them at the water height rather than the submerged ground height.
Flat water uses the master ground boundary's exact opaque footprint;
the extra-zoom water mask alone has different edge coverage. Ground and water
use identical sampling at every cache scale so their edges remain complementary.
Blended ground sets fit both source surfaces to that footprint before mixing colors,
so differing biome edge rasters cannot introduce translucent ground pixels.
Arctic and tropical grass, all four snow levels and half/full desert have matching
shoreline variants. Banks borrow snow/desert coverage from adjacent dry land, since
their own climate describes the lower submerged base. Water retains upstream palette indices.
Stream waterfalls (XTER62) recover their slope from the eight adjacent land elevations.
The four planar slopes use `universal_rivertiles_32bpp.png` and `template_waterslopes`
at x=80, including the upstream rocky rapids. Other slope forms fit animated OpenGFX2
water to their ground silhouettes in the offline builder. All forms preserve
slope coverage at every zoom. Sloped rivers draw their two side banks from
`template_waterfeature_slopes`, omitting a side shared with adjacent water.
Bank artwork uses the extra-zoom sources reduced to 128px, with coverage sampled
by nearest-neighbor. Pixels outside the owning tile's surface footprint are clipped,
so a bank cannot spill across a neighboring water tile.
The shading pass uses upstream's blue transparency key,
preventing transparent black from becoming an opaque dark edge.
Picking and placement diamonds use the
same recovered slope. Ordinary banks remain flat at water level; saved map bytes are unchanged.
Sea shores are separate from these river banks: native saltwater codes 33–45 with equal
land/water base heights retain their slope, raising the dry corners above the sea. Their
artwork combines the upstream `shorelines_overlaynormal.png` with each cached climate's
sloped ground, preserving the shared 128px master boundary. Submerged sea tiles remain
flat water. Snow, desert and grass blends use matching coastal ground; no runtime blending
or new per-tile storage is needed. Picking and damage extents include the raised corners.
Landscaped banks use three additional baked shoreline sets: bare dirt, one-third grass and two-thirds
grass, composed from the same pinned shoreline overlay and existing ground sprites. Their stored recovery
stage selects the set until normal climate ground returns. Rebuild these sources with
`python3 tools/build_landscape_assets.py --source CACHE --coast-recovery-only`.
Flat seawater never borrows the freshwater river-edge overlays, including beside cleared coastal ground.
River bank presentation reads the eight local water connections, as OpenTTD does, so native
saves from before the generator's direction correction also display connected riverbanks.
Flat rivers draw sides and all four corners independently rather than reducing the
connections to SC2's twenty bank shapes. This preserves diagonal land tips beside
straight banks and multiple concave corners on the same water tile.
Bascule drawbridges use the project Blender model; power-line bridges use the
authored lattice-tower bank. Native trees store climate-local species and independent growth stages,
using OpenTTD's four-tree layouts and position groups. XBLD tree IDs retain cover/count semantics.
Compatibility tree sprites also use OpenGFX2 artwork.
See [tile storage](../../../docs/tile-storage.md) for the simulation and save encoding.

Native IDs 542..1045 contain 36 sets of fourteen slopes: four grass recovery states,
rough and two rock variants in each biome, plus snow/desert coverage sets. IDs 1046..1061
contain four extra flat rough variants per biome. IDs 1062..1221 contain eight sets of
twenty climate-specific shoreline forms. IDs 1222..1515 contain three grass-palette blends
across all seven ground variants and fourteen slopes; IDs 1516..1575 contain their shores.
IDs 1576..1631 contain temperate–Arctic / Arctic–tropical blend snow transitions (25% for ground
recovery and 50% for the cached target); IDs 1632..1671 contain their matching 50% snow shores.
IDs 1672..2105 hold 62 tree art banks, each with seven growing/grown/dying/dead stages.
Terrain and trees use 128px detail from the original extra-zoom sources, with
nearest-neighbor coverage masks. Desert rendering selects the upstream half/full
coverage art; Arctic snow retains four coverage levels. Arctic-containing grass blends use only
half snow, preserving their own exposed grass colors. Other grass and desert never acquire snow.
These combinations are prepared offline from existing snow patterns; drawing does no pixel blending.
Toyland is not offered for generation or planting.
Rocky land always selects the dense `groundtiles_rocks_alt` bank, matching OpenGFX2's
rocky-terrain mapping. The bundled sparse rock bank is retained but is not selected
as a second density, including on blended-climate ground.
The fourth ground/rough bank instead contains SimCity-colored temperate art, using the classic
brown/gold ground color (155, 135, 71) while preserving neutral rocks. Unused quarter-desert
sets at IDs 990..1003 and 1018..1023 hold the corresponding shoreline variants, keeping the
existing ground slots. Climate zero selects this ground; it uses temperate trees.

Placement previews import `graphics/selectors/{64,128,256}/selector_tile_32bpp.png`
with the upstream `template_selectors_tile` crops and slope anchors. The sprites
retain OpenTTD palette indices, including their shaded edges. Native placement
diamonds use one shared 128-pixel indexed bitmap resource in Overworld and Underworld,
with identical strokes and width scaling. White and red use 256-entry ARGB palettes
with the same coverage alpha; index zero stays transparent. Its terrain projections are baked offline and selected by the same corner heights
used for drawing and input in both views.
Point markers retain the imported artwork and `PALETTE_SEL_TILE_RED` (772) for blocked
or unaffordable cells. These overlays resolve to fixed framebuffer colors;
city/water palette animation stays independent.
Native asset IDs 500..541 hold the three sets of fourteen supported SC2 slopes;
they do not replace any original SimCity sprite or affect comparison rendering.
IDs 2106..2108 import `selector_point_32bpp.png` at 16/32/64-pixel tile detail,
with the upstream `SPR_DOT` (774) anchors. Raise, lower and level tools use this
corner marker; area tools retain the diamonds. Markers remain visible over buildings.
Drawing records visible tree coverage alongside the screen's palette indices so trees
cover markers without changing sprite order or guessing which bitmap colors are ground.
Coverage is retained and scrolled with the map; it adds no per-tile storage.
IDs 2109..2112 hold the four planar river slopes used by the offline water builder.
IDs 2113..2240 hold two river side banks for each planar slope and each of the sixteen climate values.
IDs 2241..2432 hold twelve flat riverbank pieces per climate: four straight sides,
four outer corners and four inner corners, in OpenTTD's `DrawWaterEdges` order.
IDs 2433..2656 contain fourteen coastal slopes for each of the sixteen climate values.
IDs 2657..2670 contain all fourteen finished water slopes, including those planar rapids.
The flat slot (2657) imports the inland river tile from `template_waterslopes(80,0,4)`
at x=402 in `universal_rivertiles_32bpp.png`, retaining its colors and anchor while
fitting its edge raster to the same fully opaque diamond footprint as sea and ground.
Fresh rivers and lakes select this sprite using the existing water-kind flags,
including freshwater at the sea datum. Salt water selects the sea sprite.
Submerged freshwater shores draw the river surface and separate bank pieces rather
than precomposed sea shores, which contain the sea wave pattern.
There is no freshwater tint. Both sprites use OpenTTD's dark-water palette cycle;
the sea's sparkle pixels and river rapids additionally use its independent glitter cycle.
The 256px-to-128px bake preserves opaque sparkle colors from the whole 2x2 sample
block, retaining their palette identity inside the nearest sample's water footprint.
Sampling only the upper-left pixel dropped the sea's sparkles during the 128px
artwork migration. The restored sea has all five glitter phases; ordinary zoom
sampling retains sparkle pixels at 16, 32, 64 and 128px tile sizes.
Rebuild only the sea sprite and its atlas/preview with
`python3 tools/build_landscape_assets.py --source CACHE --sea-water-only`.
IDs 2671..2684 contain infrastructure's fourteen terrain front faces.
IDs 2685..2740 hold four blocks of fourteen level foundations; IDs 2741..2772
hold four blocks of eight inclined foundations (W/S/E/N, X then Y).
Underground artwork is baked by `tools/build_underworld_assets.py` at 128 pixels per tile.
The full landscape builder calls the same generator; the standalone command replaces only
underground PNGs and catalogue entries, preserving unrelated sprite sources:

```sh
python3 tools/build_underworld_assets.py --preview /tmp/underworld-pieces.png
```

Legacy procedural texture slots 2773..3354, 3379 and 3380..3381 are retired holes.
IDs 3454..3473 contain twenty shared indexed ground shapes. Four 256-entry ARGB
palettes supply soil, dry coverage, supplied water and submerged-soil colors at draw
time. Palette entries used by ground are opaque; the general supplied-palette draw
call also supports transparency. Baked indices contain texture and light intensity,
so no surface lighting is calculated while drawing. Assets builds the four palettes
once during loading; cached sprite scales retain the shared indices.
Soil grain uses subtle irregular noise with a fixed seed and matching tile edges,
baked offline instead of a short repeating stripe pattern.
The extra six shapes cover two saddles and four slopes rising along both map axes,
so shared corners do not collapse into dark wedges. Backgrounds are standard bilinear
terrain, with their texture, grid, lighting and front faces baked offline.

IDs 3474..3525 contain 52 copper pipe pieces:

- 15 flat connection masks, including four solid capped ends;
- 16 straight incline halves: four ascending directions, lower/upper, plain/eased;
- 6 horizontal diagonal-run and direction-transition pieces;
- 8 plain half-grade arms for nonstandard sloped junctions.
- 1 shared central fitting with baked circular alpha coverage for those junctions.
- 6 vertical diagonal-run and direction-transition pieces, matching the horizontal set.

The first 37 pieces are the common catalogue. Plain half-grade arms avoid a full
connection-mask by terrain-shape cross product. Height easing applies only to ordinary
straight inclines meeting flat terrain. Each slope half meets at the same midpoint
height and tangent; a continuing incline uses plain halves. Bends and junctions use
standard artwork. Unusual slope junctions compose plain arms and a central fitting.
Pipe diameters are 17 pixels at 128-pixel tile width; irregular hammered-copper grain, rounded highlights,
merged flat junctions, shadows and solid cap faces are all baked into the bitmap.
Pipe shadows use the finished capped silhouette shifted downward, without an
inflated round stroke that protrudes at endpoints or arm joins.
Neighbor connectivity still determines the displayed pipe network, independently of
terrain-forced legacy kind bytes. Pipe/subway crossings retain separate axes.

IDs 3526..3825 hold subway overlays, 3826..3865 covers, 3866..3885 indexed selection
bitmaps, 3886 the shared indexed soil face, and 3887..3888 buried road tunnel artwork.
There are 435 underground sprites in total. The dark underground option and its
duplicate artwork have been removed; all use normal brightness.
White and red selections use the same indexed artwork with 256-entry ARGB palettes.
Coverage alpha is blended by the ordinary run-sprite renderer, including antialiased edges.
An error marker covered by the active diamond remains omitted.

Runtime drawing uses cached run sprites and bounded local neighbor/height queries.
It does not project textures, solve surfaces per pixel, build pipe curves or calculate
pipe lighting. Input and selections use the same bounded bilinear ground profiles.
Bridge approaches still recover buried land from neighbors; freshwater still follows
its water-surface elevation. These presentation choices do not alter City or saved terrain.
Service flags choose gray for dry capability and blue only for supplied capability.
Optional comparison tooling owns its original indexed resources independently.

IDs 14..80 contain native roads, railway and power fallbacks. World catalog overrides
replace retired compatibility artwork without reading an original atlas. Animated water
uses compact animation IDs. Cached scaling preserves each run mode and alpha.

## 128px landscape sources

The original OpenGFX2 artwork already supplies 256px sources for every landscape
family imported here. No AI upscaling or generated replacement is necessary.
The source revision and GPL-2.0 attribution above apply to the derived PNGs too.
Only assets already represented by this game are imported; no farm fields or
additional Toyland terrain are introduced. The existing tree art bank, including
all its growth, dying and snowy variants, keeps its IDs and species mapping.

`detail128/manifest.json` maps landscape IDs to atlas crops for inspection.
These atlases are generated reference sheets; `sprites.json` and the individual
PNGs are the build inputs. Edit the individual sprite to change artwork in-game. `preview.png` shows a small
selection at exact asset resolution, without preview interpolation. `terrain-masks.png`
contains twenty exact 128px ownership masks, with white opaque pixels and
transparent padding. The first fourteen match the surface shape order; the final
six cover the underground saddle and steep shapes. The manifest records their
corner heights. Full ground must exactly fill its mask; riverbank overlays must
remain inside it. The 256px color working canvas enlarges these masks with nearest
neighbor and reduces back to the same 128px coverage before packaging. Road and
railway builders read this master sheet directly rather than recovering a mask
from older packed sprites.

A ground sprite uses a 128×128 padded canvas: its visible flat diamond is
128×64; slopes can extend higher. Individual trees use a 128×224 canvas and a
192px baseline. Their canopies retain the upstream template bounds and trunk
anchor, extending above the ground diamond as intended. Alpha coverage is
sampled with nearest-neighbor separately from color reduction, so filtering
cannot grow the silhouette. Animated water colors in water, coasts and bank pieces
keep their palette identity rather than being averaged into static RGB. These sprites also use 128px tile
width; larger upstream images are build inputs, not runtime landscape artwork.

Regenerating landscape artwork updates these reference atlases and individual PNG sources together.
Use `--detail-output` to redirect the editable atlases for a diagnostic build.
The native build packs the individual PNGs and anchors; the game loads only the resulting `landscape.dat`.

## Binary format

App builds place the baked run-sprite data inside `graphics.sc2assets`, using the same indexed
format as the intro and audio packs. Fast development compression uses Zstandard level 1;
`SC2K_COMPRESS_ASSETS=ON` selects level 15 without changing the format or loader. See
[asset packs](../../../docs/asset-compression.md).

All integers are little endian. Header: eight bytes `SC2KLAN2`, uint32 sprite
count, then five uint32 RGB dark-water colors. Each record contains seven 32-bit
words: sprite ID, width, height, baseline, scale divisor, signed horizontal
offset, and payload size. Payload: height+1 uint32 row offsets followed by runs.
The final offset ends the last row; equal adjacent offsets identify an empty row.

Each nonempty row begins with its initial transparent skip: bytes 0..254 store
the count directly; 255 is followed by a uint16 full count. No trailing skip is
stored. Transparent gaps between visible runs use mode 0.

Each run starts with one byte: mode in the high three bits and a length field in
the low five. Fields 0..30 mean lengths 1..31. Field 31 reads an extension byte:
0..254 mean lengths 32..286; 255 reads a uint16 full length (287..65535).

| Mode | Payload per pixel | Retained animation ID |
| --- | --- | --- |
| 0 | None (transparent interior skip) | Unchanged |
| 1 | RGB, 3 bytes | Cleared |
| 2 | RGBA, 4 bytes | Cleared when alpha is nonzero |
| 3 | Static source palette index, 1 byte | Cleared |
| 4 | Static source index + alpha, 2 bytes | Cleared when alpha is nonzero |
| 5 | Animation ID, 1 byte | Stored |
| 6 | Animation ID + alpha, 2 bytes | Stored if opaque; map blends retain fixed RGB |
| 7 | Reserved | Invalid |

Static palette indices include zero. Animation IDs use a separate namespace:
zero means fixed RGB, 1..49 map to SimCity source entries 171..219, 50..65 map to
224..239, 66..70 are OpenTTD dark water and 71..75 are glitter. There is no
brightness byte or brightness-zero exception. Glitter preserves OpenGFX2 mask
indices 250..254 in order; Drawing resolves its fifteen-step cycle with three-step
entry spacing. Selectors use static indices in their supplied drawing palette.

The framebuffer's unused high byte retains source identity for selection and
water clipping. Its separate byte plane stores only compact animation IDs. Row
u32 bucket hints avoid scanning unaffected rows and are rebuilt during recoloring.
Alpha-blended map pixels retain their rendered RGB until an ordinary repaint;
they do not record palette dependencies or request animation repaints. Interface
bitmaps retain their global-palette dependency handling. Opaque animated pixels,
including sea glitter, use SIMD recoloring directly.

The two packaged files total 95,188,610 bytes, down from 102,198,234 bytes:
7,009,624 bytes (6.86%) saved, with the same decoded source pixels.

Bridge front/rear crops include OpenTTD's twelve-unit front position advance,
which cancels their template anchor difference in the finished composite.
Decks use the upstream 81-pixel placement and blue transparency key; finished
bridges face SimCity's unrotated road/rail diagonal before runtime mirroring.
Padding and anchors preserve bridge overhangs. Artwork
is clipped and scaled once per cache scale, with no draw-time allocations.

## Demolition effects

`demolition.dat` contains the original OpenGFX2 extra-zoom explosion frames from
revision `7a17fb370440aed7f2586715d606e9fe190b9dd3` (GPL-2.0, see `LICENSE`):
`graphics/effects/256/explosion_large_32bpp.png` (16 frames) and
`graphics/effects/256/explosion_small_32bpp.png` (12 frames). Anchors follow
`baseset/nml/base/base-3701-smoke-explosion.pnml` and `template_animation_frame`.
It uses the same validated native run format as `landscape.dat`, with a separate
28-frame asset set. Assets owns the source runs; Drawing caches scaled pixels.

Successful animated demolition queues bounded presentation effects, without
changing City RNG, saved state, command results or the original delay cadence.
Frames advance every 120 ms on presentation time, including while paused.
Only changed effect rectangles and their scrolled previous positions are damaged
in each visible viewport. Loading, generating or rotating a city clears effects.

Regenerate offline (Pillow):

```
python3 tools/build_demolition_assets.py --source /tmp/opengfx2-source
```

The native train renderer also uses the pinned OpenGFX2 SH 125 locomotive and passenger coach.
Source sheets, import metadata and registration details are in [world artwork](../world/README.md#opengfx2-trains).
