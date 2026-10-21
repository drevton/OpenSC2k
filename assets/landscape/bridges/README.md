# OpenGFX2 road and rail bridges

Artwork: Richard Wheeler (Zephyris) and OpenGFX2 contributors, GPL-2.0.
Pinned source: https://github.com/OpenTTD/OpenGFX2/tree/7a17fb370440aed7f2586715d606e9fe190b9dd3
License: ../opengfx2/LICENSE.

The thirteen OpenTTD choices use seven geometry families. Cantilever, tubular,
and suspension color variants share their indexed sprite geometry. Each family
has six longitudinal pieces in both map directions, for both road and rail.
`geometry-*.png` contains 128-pixel-wide road artwork with native asphalt and center
lines. `rail-geometry-*.png` uses the native ballast, rails and sleepers. Both use
source front/rear structures. Source piers are selected from the actual piece layouts rather than
added indiscriminately. The baseline places the pavement one native height step
above the supporting surface.

`palette-source.png` identifies the 256 indices during packing. Its RGB values
are unique; PNG alpha independently preserves edge coverage. The first 192
indices describe fixed road/rail/wood/stone colors. The remaining 64 describe painted
structural shading. `palette-0.png` through `palette-12.png` are opaque, 256-entry
RGB palettes selected at drawing time. Their structure ramps follow the upstream
PALETTE_TO_STRUCT recolor definitions, interpolated to preserve finer shading.
They do not use the game's animated palette or contribute a palette alpha value.

`layouts.json` records the OpenTTD src/table/bridge_land.h piece selections for road and rail with
OpenGFX2 bridges.pnml crop coordinates. The layout algorithm follows OpenTTD
CalcBridgePiece. OpenTTD code is GPL-2.0; see ../../../src/city/terrain/LICENSE.OpenTTD.
The art importer needs Pillow and numpy:

    python3 tools/build_bridge_assets.py

Normal builds only need the checked-in PNGs, manifests, and Pillow. They generate
SC2KLAN3 landscape.dat, containing the shared indexed sprite streams and thirteen
palettes; no generated sprite pack is checked in. Landscape packs and the native
executable use the current format together. The diagonal `icon-*.png`, `preview.png` and `preview-rail.png` images are offline
comparison artwork. Runtime bridge choices use original generated side-view icons from
`assets/ui/bridges/generated/`, packed by `tools/generate_map_icons.cjs`.

## Moving traffic occlusion

`front-road-*.png`, `front-rail-*.png` and `front-head-*.png` preserve the source's
actual front structures separately from the pavement. The runtime restores these
rails, beams and trusses over a vehicle on the deck. The full deck sorts at the
back of its footprint and the prepared front structures at its near edge, keeping
pavement below traffic for the entire tile. Underpass traffic sorts before the
complete deck; bridge heads use their bank height for the back layer and deck
height for the front layer. This avoids inferring pixel depth from a finished
bitmap. The front layers share the existing bridge palettes and baselines.
`traffic-foreground.json` orders the road geometry, rail geometry, then the
32 approaches. Normal builds append these layers to the landscape pack.

Refresh just these derived sources with:

    python3 tools/build_bridge_assets.py --traffic-foreground-only

The full importer also refreshes them. Source artwork, attribution and license
are the same as the complete bridge sprites above.
