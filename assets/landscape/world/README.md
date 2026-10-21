# Authored world artwork

Native world-detail replacements at 128-pixel tile pitch. No SimCity artwork,
models, silhouettes, palette files or executable resources are source inputs. Trains use the freely licensed
OpenGFX2 artwork described below; the other vehicles retain their original Blender models.

| Slots | Artwork |
| --- | --- |
| 1–4 | Full-tile demolition rubble over brown earth, four registered variants |
| 5 | Low radioactive canisters and contaminated soil, one opaque surface |
| 88–89 | Blender bascule bridge, closed/open |
| 108–111 | Four directional recessed rail ramps with concrete subway portals |
| 305–358, 450–477 | Authored utility compatibility art, selectors and indicators |
| 359–381 | Blender airplane, helicopter, freighter and sailboat; OpenGFX2 locomotive at 374–378 |
| 382–389 | Project dispatch icons and warning; licensed OpenGFX2 explosion |
| 390–399 | Blender bulldozer, painted dust and four fire phases |
| 400–439 | Five helicopter headings with eight registered rotor phases each |
| 440–447 | Eight rescue helicopter rotor phases |
| 478–488 | OpenGFX2 passenger coach and remaining locomotive headings |
| 490–499 | Industrial crawler, water, crowd, rescue helicopter, smoke and tornado |

Unused palette traffic slots 448–449 explicitly resolve to an empty sprite. Retired
composite monster-part slots are blank except 478–488, reused for train coaches and locomotive headings.
Native animated road vehicles keep their independent Blender bank.
Power compatibility slots borrow the existing authored tower rasters. Park and
building compatibility slots resolve to the complete native building/civic banks.
`catalog.json` overrides the base catalog without shifting existing native IDs.

## Radioactive ground

Large black trefoils sit on broad yellow lids. The canisters have a low silhouette;
the entire painting stays inside its tile, including repeated neighboring patches.
`waste-ground-00.png` through `waste-ground-13.png` bake the detail, source coverage,
shading and contaminated soil together for every surface grade. Their alpha channels
contain only 0 and 255, and the packer rejects translucent pixels. The normal RGB run
renderer draws one surface sprite; outside-footprint skips need no alpha payload or
background blending. No runtime composition, per-pixel mask or per-tile state is added.
Terrain cliff faces still use the shared terrain renderer.

## Authoring

`sources/rubble-ground.png` replaces the old isolated pile for slots 1–4. Its prompt
requested a low, detailed field of concrete, brick and broken timber covering an
entire 2:1 isometric diamond, with opaque brown demolition soil, no grass, no tall
piles and no debris outside the footprint. The original `sources/rubble.png` is
retained and adds a shallow raised heap above that surface. The baker registers
the surface at the terrain baseline, fills edge coverage with brown earth and
applies the shared binary ground boundary so neighboring tiles meet exactly.
All four variants cover the complete tile without alpha blending or grass showing
through. `rubble-preview.png` shows the variants touching in a 4×4 patch.

`sources/` holds ImageGen masters. `provenance.json` records their SHA-256 hashes and
per-slot sources. The waste master was directed to use two short wide yellow canisters,
huge black radiation trefoils on the lids, sparse contaminated gravel, a low silhouette,
a 30-degree orthographic view, transparent margins and no lettering. Rubble uses concrete,
brick and timber; the fire strip has four aligned flame phases; the tornado is a funnel
with a ground-contact debris cloud. These are new paintings, not transformations of
original game artwork. Dust and pollution reuse the project's authored power-smoke art.

`blender/manifest.json` records source hashes, cameras and common ground anchors.
`tools/render_world_vehicles_blender.py` constructs the models from original geometry,
using one 30-degree orthographic camera and one pixel pitch across headings. The source
`.blend` and PNG renders are checked in. Never fit each heading's silhouette separately.

Helicopter main and tail rotors are separate model assemblies. The authoring script
renders eight phases per helicopter heading and eight for the rescue helicopter's
map view. The baker uses one common crop and anchor across each heading's phases,
so the fuselage stays still while the blades rotate. Drawing selects phases at
24 frames per second on the existing unpaused presentation clock; landed helicopters
use the resting frame. No City event or simulation timing is changed.

```sh
blender --background --threads 6 --python tools/render_world_vehicles_blender.py -- --samples 48
python3 tools/bake_world_assets.py
python3 tools/dev.py build
```

Normal builds only pack the checked-in PNGs; Blender and ImageGen are authoring tools.
`preview.png` and `waste-preview.png` are development contact sheets, not runtime inputs.
OpenGFX2-derived explosions, ground textures and water retain GPL-2.0 attribution:
see `../opengfx2/LICENSE` and its pinned upstream revision. All other source provenance
is listed in the manifests or the owning toolbar/building asset directory.

Subway-to-rail portals are authored by `SubwayRailPortal` in the world baker.
They share the ordinary railway gauge and ballast material, with sleepers and rails
descending into a dark opening between low concrete retaining walls. Variants follow
the native exposed rail direction: south, east, north and west. They replace the
incorrect rubble aliases without changing tile storage or resource IDs.

## OpenGFX2 trains

`tools/import_train_assets.py` imports the SH 125 diesel locomotive and temperate passenger coach
from [OpenGFX2](https://github.com/OpenTTD/OpenGFX2), pinned to revision
`7a17fb370440aed7f2586715d606e9fe190b9dd3`. Artwork is by Richard Wheeler (Zephyris) and
OpenGFX2 contributors, GPL-2.0; see [the packaged license](../opengfx2/LICENSE).
`opengfx2-trains.json` records source paths, hashes, templates and catalog assignments. The original
source sheets remain in `sources/opengfx2-trains/`; upstream Paint.NET masters are available at the
pinned revision. The earlier project Blender train models/renders are retained in `blender/`.

All eight headings preserve OpenGFX2's template origins at one common pixel pitch, on a shared canvas
with a wheel-center anchor. Symmetric coaches reuse their four source views. Catalog slots 374–378
and 483–485 hold locomotive views; 478–482 and 486–488 hold coach views. `train-preview.png` shows
all headings. Regenerate explicitly with:

```sh
python3 tools/import_train_assets.py --source /path/to/opengfx2-cache
python3 tools/dev.py build
```

Normal builds pack checked-in PNGs without downloads. The world baker preserves these imported
records. Drawing follows a rounded path through successive rail positions, chooses headings from
its tangent, and interpolates track height through slopes and bridge approaches. Rail tile artwork
no longer controls vehicle orientation or adds tile-specific vehicle offsets. Motion history stays
in Drawing's existing per-event buffer. Coaches sample the same path at 65% of a tile spacing to
match their imported length, rather than leaving the full-tile simulation gaps between vehicles.
City movement, RNG and save records are unchanged.
