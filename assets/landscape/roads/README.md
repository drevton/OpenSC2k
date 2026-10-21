# Native roads

The in-game road set uses 128×64 transparent overlays, with 128×80 inclines.
Pavement occupies 75% of the tile's map-plane width. A 1.25-screen-pixel alpha
fringe lies outside that opaque width. Road alpha borrows the shared 128px master
ground boundaries from `../opengfx2/detail128/terrain-masks.png`, including all
four inclines and their baseline. Terrain, water, roads and railway overlays use
identical pixel ownership, independently of their color artwork resolution.
The master coverage mask is scaled with nearest-neighbour sampling at every
resolution. Filtered artwork and soft road shoulders remain clipped to that
binary tile mask; the shared tile boundary is never interpolated.

The shared lighting profile keeps a short 104-value asphalt interior and fades
gradually to 48-value charcoal edges. This OpenGFX2-style falloff spans the outer
84% of the width, rather than holding most of the pavement at a flat 112. Roads,
ramps, highways and structure surfaces share the profile so their joins match.

Surface roads also carry a narrow, softly broken highlight outside the asphalt
shoulder. A pale translucent band reveals the underlying grass, desert or snow,
with stronger light on edges facing the top left. It is generated offline into
the existing overlay and clipped to the same nearest-neighbour tile footprint.
Opaque asphalt and lane markings stay unchanged; bridge decks and tunnel
interiors do not receive this ground treatment. No extra runtime pass or
ground-specific road variants are required.

The 55 ordinary entries in `manifest.json` replace existing road-related sprite IDs:

| IDs | Artwork |
| --- | --- |
| 29–30 | Two straight directions |
| 31–34 | Four native inclines |
| 35–38 | Four rounded curves |
| 39–42 | Four T junctions |
| 43 | Crossroads |
| 63–66 | Road tunnel mouths |
| 67–68 | Road/power crossings |
| 69–70 | Road/rail crossings |
| 73–74 | Elevated highway carriageways |
| 75–76 | Road beneath highway crossings |
| 77–80 | Highway over rail and power crossings |
| 81–87 | Suspension, girder and plain bridge decks |
| 88–89 | Closed/open bascule bridge decks |
| 93–96 | Four single-lane highway ramps |
| 97–100 | Four divided highway inclines |
| 101–104 | Four divided highway bends |
| 105 | Highway interchange |
| 106–107 | Two highway bridge directions |

The [native railway set](../rails/README.md) now owns finished road/rail and
highway/rail composites. The landscape builder loads that manifest after roads;
the standalone road installer preserves railway-owned records. Rebuild the rail
set after changing asphalt or highway artwork so its composites stay in sync.

Alternating opposite road bends use twelve additional native sprites: four
orientations, each with a straight diagonal middle and two end transitions.
This removes the repeated wave from screen-horizontal and screen-vertical roads.
The renderer checks nearby connected road tiles, requiring matching flat
terrain and elevation. Isolated curves, slopes, junctions and custom overrides
retain their existing artwork. Quintic end transitions retain the ordinary road
tangent, and every variant uses the same nearest-neighbour ground mask and asphalt.
Diagonal sections cover 100% of their shared edge, making them about 33% thicker;
end transitions taper smoothly to ordinary roads' 75% coverage. The mask prevents
overlapping pixels. Tile connectivity and saved IDs remain unchanged.
Transitions meet both straight sections with zero curvature and interpolate
their actual perpendicular widths smoothly, avoiding a pinch or bulge at the join.
Eight rounded end sprites extend the turn into adjoining flat straight tiles.
The shared cubic centreline turns steadily between the diagonal and ordinary
road directions, without overshooting and turning back. Its split is exactly
halfway along the curve. Eight reusable approach halves finish these turns.
The renderer combines a curved half with a straight half, or combines two curved
halves when both ends bend. The packer derives four straight-half run sprites
from the existing straight PNGs. All halves use normal sprite drawing, with
their pixel boundaries baked into the runs. There are no special half draw modes
or stored shared-approach combinations. Ordinary straights still use one draw.
Reconnecting a bend also invalidates its straight approaches in every map viewport.
`DIAGONALS.png` compares connected ordinary bends with their contextual variants.

Refresh just these end sprites, halves and previews without rebuilding structures:

```sh
python3 tools/build_road_assets.py --diagonal-ends-only
python3 tools/build_rail_assets.py --diagonal-ends-only
python3 tools/dev.py build
```

The game represents road ends with straight sprites; there are no separate
dead-end IDs. Highway artwork is built offline by `tools/build_highway_assets.py`,
using the approved asphalt and concrete materials. Decks retain the original
48px elevation at 128px per tile, with divided carriageways, narrow guardrails
and piers. Ramps use broad circular single-lane bends with level tangents at their
road and deck ends, widening from a 0.29-tile half-width at the top to 0.375 at ground.
Offline projection compensates for the steep curve's apparent widening, keeping
the visible deck within a smooth taper between its unchanged end connections.
Short cubic blends give the taper zero width derivative at each end, avoiding
a kink where the pavement and guardrails meet the flat street or highway.
Two equal centre dashes are fitted symmetrically to the ramp's arc length, avoiding
a clipped final dash from the ordinary highway's repeating paint pattern.
The 2×2 interchange has four
outside right-turn ramps and four 270-degree left-turn loops, providing all twelve
approach-to-exit movements. Carriageways narrow inside the interchange to leave room
for the loops, retaining the adjacent highway's full width, centre and elevation at
every boundary. Guardrails and shoulder lines leave gaps at ramp merges. Transport
routing already permits all four approaches; this artwork requires no routing changes.
Ground-reaching piers and footings carry the eight approaches and the outer bends
of all four left-turn loops. Loop piers sit clear of the through carriageways;
the short right-turn connections share the supported approach decks.
An additional 24 highway variants cover a ramp joining either side or both sides
of each straight/crossing deck. Rounded branches extend into the highway to provide
turning room in both directions. The wide opening removes that side's guardrail,
edge paint, wall and dark asphalt shoulder. Support piers move clear of the merge. Drawing selects a prepared
variant from the two adjacent ramp tiles, including their mirrored orientations;
the opposite guardrail remains intact. Tile IDs and saved map fields do not change.
Curved guardrails join the ramp's two rails to the highway rails at either end
of the opening, with matching deck fillets beneath them. These connections are
included in the same prepared merge sprites.
Four additional 128px-wide carriageway inclines follow the same one-tile grade as the underlying
land. Compact highway placement uses these without expanding a straight to 2×2. Imported shared
inclines also draw each ground cell with this grade, keeping their ends level with adjoining
straights. The 144px canvas includes guardrail headroom above the 16px rise; the baseline remains
at the ground tile's base. Rebuild only these files with
`python3 tools/build_road_assets.py --highway-slopes-only`.
Ramps omit the parallel white shoulder stripe beside the guardrail, which
otherwise reads as a second fence near the street connection. Centre dashes remain.
Ramp placement reconnects the adjoining road from its actual neighbours, rather
than forcing a crossroads. Old crossroads directly beside ramps also select their
correct connection artwork while drawing, without changing a loaded city's map.
Rail artwork outside these
highway crossings retains its existing resources. Crossings and bridges retain their existing forms, front occlusion
and anchors; their exposed road surfaces use the matching material. Structure PNGs include
vertical headroom and can extend outside a surface tile.
Bridge assembly uses native straight B for the upstream even deck mask and
straight A for its odd mask, then mirrors the finished structure into the SC2
axis. Clipping a surface from the opposite diagonal exposes brown deck sections
at both travel ends. Upstream structure artwork is retained unchanged.

All four tunnel portals are newly projected covered entrances, using
`tunnel-material.png` from ImageGen (`tunnel-material-prompt.txt`). A rounded arch
at the mouth blends into a flared roof covering the full tunnel tile and tapering smoothly
into the hillside. At the uphill boundary its width narrows and its height drops
to the 16-pixel upper ground level, leaving no exposed rear wall. The road floor
remains at the tile's base elevation.
The rounded entrance face leans 20 degrees into the tunnel from its fixed floor
edge, blending back to the unchanged uphill join. Exposed roof and sidewalls are
opaque; buried geometry is clipped at the hillside instead of showing terrain
through the concrete. A thin, irregular earth rim lies on top along the buried
roof edges, with a crisp, broken outline under one artwork pixel wide. Concrete
and soil retain solid coverage; only silhouette antialiasing uses partial alpha.
The neutral earth works with every ground palette without baked-in grass.
The arched opening reaches 26 pixels above that floor at its centre on the
128-pixel artwork, under a 28-pixel-high front roof. Front views have a dark cavity,
a tapering vaulted lining and a level road beneath the roof. Outside walls meet the
incline, and rear views use the projected inclined ground to hide buried structure. No
original portal pixels are used. The four existing direction IDs are unchanged.
Rear views retain concrete sides above the actual slope and the road floor wherever
the hillside does not obscure it. A flat upper-height cutoff would remove these sides
and leave an unsupported roof with grass showing through. Visibility is calculated
offline using the builder's existing depth buffer; runtime adds no mask or allocation.
Rebuild only these four PNGs with `python3 tools/build_road_assets.py --tunnels-only`.
The floor, lining and dark cavity are clipped offline to the inclined arched
opening, so buried interior surfaces cannot leak onto the surrounding ground.

Two additional native sprites show buried road tunnels in Underworld as enclosed
concrete boxes, with textured sidewalls and a rounded 20-pixel roof. Texture
coordinates vary with height as well as distance along the tunnel, avoiding
vertical streaks. The narrower cross-section leaves room for the projected
sidewall and clear margins on both sides of the cutaway tile. They share the
portal material and join without end caps between tiles. Drawing derives the
axis from connected tunnels and their portals using their actual floor elevation.
The underground display is a schematic on the same grid plane as other utilities;
it does not project the box down to its buried road floor underneath that grid.
The entire silhouette is centered, including the sidewall. Material is rendered
at 128-pixel tile width, scaled with nearest-neighbour sampling into the 256-pixel
utility texture and clipped to its canonical footprint. The shared ground painter
projects it onto the same smooth surface used by picking and selection.
Surface artwork and saved tunnel state are unchanged; the legacy depth marker
is retained only for reference rendering.

`asphalt-material.png` is the 128px material sample used by the approved road
prototype, reduced offline from the ImageGen master. `material-prompt.txt` records
its original prompt. `lighting-profile.json` retains the OpenGFX2 road edge tone,
with a short, flat center to avoid a bright stripe under the paint, and wider
gradual shoulders. Straight roads and curves have two white centre dashes. Curves 1 and 3 retain the adjusted
spacing. Junctions continue white centre dashes on each connected road approach,
leaving the intersection centre clear. Their interior is one planar texture, smoothly blended to the material
of each connecting road, without a nearest-arm seam.

Paint coverage is reduced separately with a finite BOX filter. It cannot leave
resampling ghosts outside the white markings. Geometry, alpha, projection and
lighting are prepared offline; the game only loads and scales finished sprites.
The City IDs, road commands, foundations, simulation and save encoding are unchanged.

`PREVIEW.png` shows the complete surface set and a connected network, with no
interpolated preview scaling. `STRUCTURES.png` shows the structure connections. `HIGHWAYS.png` shows every
highway piece. Native terrain is drawn beneath transparent highway footprints
at all three zoom levels.

Alternating opposite 2×2 highway bends use twelve prepared diagonal variants:
four orientations with either end, or both ends, joining another opposite bend.
The middle carriageways and guardrails are straight; end variants turn into the
cardinal approach. Isolated corners retain their ordinary bend artwork. Selection
uses the neighbouring footprints at the same deck height in the current camera
rotation, without changing City tile IDs or stored footprints. Custom replacements
retain their original artwork. Refresh just these variants with:

```sh
python3 tools/build_road_assets.py --highway-diagonals-only
```

Regenerate the surface PNGs and refresh their catalogue entries.
`--terrain` selects the shared 128px master boundary PNG:

```sh
python3 tools/build_road_assets.py
```

When changing the shared material/profile, also regenerate the structure surfaces:

```sh
python3 tools/build_road_assets.py --structure-source /path/to/opengfx2-cache
```

The OpenGFX2 source revision is pinned by `tools/build_landscape_assets.py`.
Its complete rebuild imports this finished PNG manifest last, preserving these
overrides. Catalogue updates preserve unrelated sprite references.
Runtime does not read these PNGs, run Python, download artwork or generate roads.

## Decorative car paths

`tools/bake_road_car_paths.py` shares `DiagonalCoefficients` with the road painter.
Normal builds incrementally generate `road-car-paths.inc`; no image libraries are
needed for this step. Ordinary turns, connected diagonals, extended transitions,
shared approaches, dead ends and straights have 34 lane recipes. Rotated matches
reduce these to 22 shared paths.

Each path has 33 points at equal travel distances. A point occupies one `u32`:
9 bits each for tile-local X and Y in 1/256-tile units, and 8 bits for heading.
A recipe uses one byte: a five-bit path ID and two-bit rotation. Path zero marks
an inactive car. Lengths use `u16` in 1/256-tile units. Including ports, front-axle
offsets and the quarter-wave direction table, the immutable tables occupy
3,141 bytes at every map size. No table is allocated per map tile.

`src/drawing/maps/road_cars.c` owns the road-car logic. Runtime uses 32-bit integer
interpolation, cached byte coordinates/headings and the existing 16-byte car
records. Its eleven-bit lane cursor retains fractional progress between Q8
positions. Slope and bridge-ramp heights use linear interpolation; other ground
shapes retain the terrain surface interpolation. Movement updates every traffic
tick, with lifetime updates spread over sixteen index phases as before.

The asphalt material is AI-generated for this project. The OpenGFX2 lighting
reference and retained bridge structure artwork are by Richard Wheeler
(Zephyris) and OpenGFX2 contributors, GPL-2.0: see
[the landscape license](../opengfx2/LICENSE) and
[upstream source](https://github.com/OpenTTD/OpenGFX2).
Crossings use the native road, rail and highway rasters. The closed/open bascule
pair uses the project Blender model in `../world/`; no SimCity artwork is used.
The tunnel concrete material is AI-generated for this project.

Builders refresh the individual PNG references in `../opengfx2/sprites.json`.
Run `python3 tools/dev.py build` to regenerate the runtime pack from those checked-in sources.
