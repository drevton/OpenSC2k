# City tile storage

Current implementation reference, checked 2026-10-21. Bit numbers start at zero.
City always stores native tile encoding, including during stock comparison. Complete stock
records are translated at import/export and comparison boundaries; City fixes select behavior. The earlier [bit audit](../reports/2026-10-10-tile-metadata-bits.md)
describes the isolation work before these payload meanings were assigned.

Each tile-family source begins with its native `TileInfo` storage contract. These describe the actual stored bits,
including fields shared with other families, rather than the composed legacy bytes returned by compatibility helpers.
The common field map is beside `TileInfo` in [tile_storage.h](../src/city/maps/tile_storage.h).

| Family | Storage contract |
| --- | --- |
| Clear ground and ground cover | [ground.c](../src/city/maps/ground.c) |
| Trees | [trees.c](../src/city/maps/trees.c) |
| Water and wet occupants | [water.c](../src/city/maps/water.c) |
| Road, rail, ground crossings and subway/rail entrances | [surface_lines.c](../src/city/infrastructure/surface_lines.c) |
| Power lines and wire crossings | [power_lines.c](../src/city/infrastructure/power_lines.c) |
| Highways and ramps | [highways.c](../src/city/infrastructure/highways.c) |
| Road, rail and highway bridges | [bridges.c](../src/city/infrastructure/bridges.c) |
| Tunnels | [tunnels.c](../src/city/infrastructure/tunnels.c) |
| Buildings, facilities and parks | [buildings.c](../src/city/maps/buildings.c) |
| Rubble, waste and family conversion | [layers.c](../src/city/maps/layers.c) |
| Underground networks | [networks.c](../src/city/infrastructure/networks.c) |

Shared processing modules also describe the fields they use: geometry in `maps/terrain.c`, region/cover in
`maps/state.c`, sign/effect bits in `maps/labels.c` and `maps/tile_scan.c`, service flags in
`infrastructure/utilities.c`, route storage in `infrastructure/transport.c`, and zoning in `growth/zoning.c`.

## Layout and indexing

`City` holds map-layer pointers directly into one `City.map_storage` backing allocation.
`City.tile[]` contains the surface and underground fields in eight-byte `TileInfo` records.
Scratch remains a separate contiguous array. `City.half[]` groups pollution, traffic, crime and
land value in four-byte `CityHalfMapStatistics` records; `City.quarter[]` groups density, police,
fire and density history in four-byte `CityQuarterMapStatistics` records. This replaces eight
layer pointers with two without changing the backing allocation's size. Full-resolution arrays use
`tile = y * width + x`, where x is the column and y is the row.

`MapTileXY(city, x, y)` requires valid coordinates and asserts that invariant in debug builds.
It performs direct conversion and never returns `INVALID_TILE`. Check optional neighbors and
external coordinates with `IsMapCoordinateValid` before conversion. Command decoders and map
pickers may return `INVALID_TILE` when their input does not identify a tile.

| Native layer | Element | Grid | Current meaning | SC2 chunk |
| --- | --- | --- | --- | --- |
| `tile[].kind` | `u8` | Full | Native surface kind | `XBLD` |
| `tile[].flags` | `u8` | Full | Salinity, permanent region, utilities and traversal flags | `XBIT` |
| `tile[].zone` | `u8` | Full | Zone or slope, discriminator and growth/timing | `XZON` |
| `tile[].terrain` | `u8` | Full | Ground/coverage or water form, plus occupant metadata | `XTER` + `XGND` |
| `tile[].underground` | `u8` | Full | Underground infrastructure ID and metadata | `XUND` |
| `tile[].overlay` | `u8` | Full | Sign presence and stationary effect | `XTXT` |
| `tile[].altitude` | `u16` | Full | Land, water/stage/bridge style, tunnel and persistent flag | `ALTM` |
| `scratch` | `u16` | Full capacity | Reused simulation working storage | No ordinary SC2 chunk |
| `pollution` | `u8` | Half | Pollution calculation layer | `XPLT` |
| `traffic_density` | `u8` | Half | Route usage/traffic accumulation, decayed over time | `XTRF` |
| `crime` | `u8` | Half | Crime calculation layer | `XCRM` |
| `land_value` | `u8` | Half | Land-value calculation layer | `XVAL` |
| `density` | `u8` | Quarter | Accumulated development/density metric | `XPOP` |
| `police_coverage` | `u8` | Quarter | Police coverage/influence | `XPLC` |
| `fire_coverage` | `u8` | Quarter | Fire coverage/influence | `XFIR` |
| `density_history` | `u8` | Quarter | Smoothed change in the density metric | `XROG` |

Half grids have width/2 by height/2 cells; each byte covers a 2x2 tile region. Quarter grids have
width/4 by height/4 cells; each covers 4x4 tiles. Index them using shifted x/y and their own row stride.
These coarse values use whole bytes, not independently available bit fields. Accessors retain
their read/write contracts and force inlining. Stock and native save codecs gather/scatter the
record members into the existing file planes; the in-memory regrouping changes no file encoding.

City keeps its layer pointers, Game link, map dimensions, clocks/RNG, frequently used simulation
scalars and tile totals before cold tables and file metadata. Large name and economy arrays
follow the hot prefix; native state is naturally aligned and does not mimic stock addresses.

The chunk associations above are the literal current mappings in `schema.c`; do not infer them
from the English names of the chunks or native fields. `AccumulateFacilitySectionRow` writes
police-station influence into `police_coverage` and fire-station influence into `fire_coverage`.

Full-resolution layers occupy 10 bytes per tile, including scratch. Coarse layers add an average
1.25 bytes per tile. History and the additional working arrays below are separate costs.

Sources: [tile record](../src/city/maps/tile_storage.h), [map layers](../src/city/maps/map_types.h),
[binding/allocation](../src/city/state.c),
[accessors](../src/city/maps/layers.h), [save schema](../src/city/saveload/schema.c),
[environment calculations](../src/city/maps/environment.c).

```c
typedef struct TileInfo {
  u16 altitude;                                      // bytes 0–1
  u8 kind, zone, terrain, overlay, flags, underground; // bytes 2–7
} TileInfo;
```

The record has natural two-byte alignment, no padding, and eight records per aligned 64-byte
cache line. Compile-time assertions freeze its size and every field offset. Underground fills byte 7,
previously padding after the climate field was removed. Its separate array and City pointer are gone,
saving one byte per tile: 16 KiB at 128×128 and 16 MiB at 4096×4096. Scratch and coarse layers stay contiguous.
Save files and comparison checkpoints retain their existing separate byte planes; codecs gather
or scatter record members at that boundary. XUND still preserves all eight underground bits.

## Surface tile: `tiles[tile].kind` / XBLD

Shoreline trees use kinds 50–53 for one through four trees. They combine tree and water
classes, retain bank water in altitude and ground cover in the low terrain nibble, and share
normal tree species/growth storage. Planting, thinning and removal preserve the shoreline.
Tree trunks draw only on the raised land. Fully submerged tiles remain ineligible.

Gameplay uses named category predicates such as `IsTileKindBuilding`, `IsTileKindUnbuilt`,
`IsTileKindTunnel` and `IsTileKindGroundCrossing`. Contiguous family checks live with those predicates;
callers do not infer a category from the ID of an unrelated kind. Native kinds 19–26 are independent
of the stock crossings, ramps and subway entrances in IDs 67–111. Codecs map those families explicitly.
Simulation statistics retain their established stock category slots through `CityTileStatisticsKind`;
maintenance and asset-value calculations use those slots, not native kind ordering.

The entire byte is a native tile kind, not a bitset or sprite ID. Infrastructure and landscape kinds
occupy 0–49, leaving 50–111 available. Existing building IDs 112–255 stay fixed. Small park alone
moves from 13 to the retired native slot 199; stock park 13 still imports/exports as park, and stock
hydro artwork 199 still decodes as hydro. Explicit predicates preserve the small park’s landscape
behavior and exclude it from facility and power-plant ranges.

`kCityMapKindSprites` in `src/drawing/maps/tables.c` maps named `TK_*` kinds directly to
base Drawing sprite-cache slots in a compiled `u16[256]` array. Drawing indexes it directly.
The baker reads the same table to associate height metadata with native kinds, so reordering
kinds does not reorder graphics. There is no separate geometric-layout ID returned to callers.
`CityMapTileSpriteIndex` and `CityMapViewTileSpriteIndex` return resolution-independent sprite
identities. The tile resolver receives the requested camera turn explicitly, including for highway
ramps. Image resolution belongs to the graphics cache. Existing internal zoom banks remain private
prepared-image indexes, including when stored in a prepared drawing record.

Tile rules choose connected transport, construction, facing and bridge pieces. Classification,
bridge depth and ramp connectivity read native kinds and tile metadata rather than sprite IDs.
Specialized artwork composition still uses named base sprite slots when selecting matching
foundations, transport approach pieces or event poses. Ordinary road/rail sprite resolution starts
from the stored connection mask. Foundation handling then selects the final flat or inclined
artwork for the resulting surface, rather than selecting an incline from the original ground shape.

`assets/landscape/map-graphics.json` binds Drawing cache slots to authored sprite sources.
Each source group occupies four consecutive ordinary resource indexes. The baker writes a direct
cache-slot rotation table, so a requested view needs one lookup; it does not translate a slot to
a group and then back to another intermediate ID. `tile-graphics.dat` holds those rotations and
cache-slot source bindings, borrowing decoded bytes without runtime expansion.

The graphics pack's `u16` reference lookup is private to the generic sprite codec. Bits 0–14
select a shared header and bit 15 flips its artwork; header zero denotes an absent sprite.
Map code passes opaque resource indexes. `GetResourceSprite` supplies metadata without exposing
packed references, and `InstallResourceSprite` applies the artwork flip inside cache preparation.
Tile-facing mirror choices remain ordinary draw flags. The graphics pack itself has no camera
rules, and headers can share pixel data and palettes. Zoom caches resolve references once during
preparation and reuse existing slots; prepared drawing records remain 12 bytes. Native kinds,
Drawing cache slots and resource indexes are distinct; no gameplay rule may depend on an image ID.

### Image resolution variants

Map and UI packs use the same sparse `SpriteVariants` records and `ResolveSpriteVariant` resolver.
A record contains a little-endian `u16` sprite index, a four-bit `u8` level mask, a zero byte, then one
packed `u16` header reference per set bit in ascending level order. Records are sorted by sprite
index. An absent level uses that sprite's default reference and scales it during preparation.
No dense sprite-by-level resource lookup is expanded at load time.

`SC2KGFX3` stores the default reference array, a `u32` variant-byte count and those sparse records
before the image commands. Sprite headers share their ordinary image and palette namespaces.
The existing Assets image metadata catalog follows the resource header count, including additional
resolution images; adding an image does not require a new native tile kind or a C image constant.

Map sprite groups in `assets/landscape/map-graphics.json` can give each entry in `sprites` a
`variants` object. Keys `"0"` through `"3"` select the four map zoom levels. Values are source image
IDs or `{ "source": ID, "flipped": true }` references. The landscape source catalog also accepts
`variants` on an image entry; those overrides follow its group aliases unless explicitly replaced.
Every image supplies its own scale divisor, baseline and horizontal anchor. Building variants must
preserve their world footprint; their maximum measured heights contribute to damage bounds.

`SC2KICO3` uses the same default references and sparse records, followed by square native
RunSprite headers (`u32` side, `u32` byte offset from the pack start, `u32` payload length).
Payloads contain the ordinary row offsets and fixed RGB/RGBA/transparent runs, with straight alpha.
UI levels 0, 1 and 2 mean
1x, 2x and 4x detail, with 32-, 64- and 128-pixel source images. The offline icon baker supplies all
three levels. Optional `assets/ui/<group>/variants/<icon>-1x.png`, `-2x.png` and `-4x.png` files
replace individual authored resolutions. Assets owns immutable source sprites; Drawing owns
prepared button sprites. Flipping and disabled styling use the ordinary sprite drawer.

UI scale controls interface dimensions and button detail. Map zoom and display pixel density
control map sprites independently, including in auxiliary viewports. Changing UI scale can resize
a viewport's frame and visible world area, but does not change the size of its roads and buildings.
The map cache prepares only the display pixel scale and its magnified map scale; it does not
prepare a Cartesian product with UI scales.
Stock import/export numbering remains a separate codec concern. `TileKind` contains native kinds;
`CityLegacySurfaceTile` in `city/saveload/map_format.h` lists original SC2 IDs with the `TKL_` prefix.
Numeric overlaps do not imply matching meanings: stock rail 44 and native highway 27 are separate identities.

`city/maps/tile_storage.h` defines native tile values and packed fields only. Placement
selectors belong to tool IDs, facility classes and special counters to economy IDs,
map geometry to map types, and growth states to the growth interface. Information
and land-use categories belong to resource/UI IDs. Stock one-hot zone corners
belong to the map codec header, independently of native footprint links.

Immutable native kind lookups use designated `TK_` initializers. Full-kind tables use
`TK_COUNT`; compact building tables retain bounds such as `TK_COUNT - TK_RESIDENTIAL_FIRST`
and index entries as `[TK_HOSPITAL - TK_RESIDENTIAL_FIRST]`. Population, development
category, footprint, pollution, density and collision values remain simulation data,
independent of replacement artwork. The land-value scan shares the development-category
table instead of storing a duplicate copy.

Damage bounds use one `u8` height per native kind, in smallest-zoom pixels. The baker takes
its maximum across every footprint tile, all four views and the applicable construction frames.
All cells of a building therefore use the same height. Damage lookup needs no footprint walk,
view-specific profile or construction-height selection; snapshots retain the previous height.

`city/maps/kinds.h` defines `TileKindClasses`, the `TCLASS_` bits and kind-only queries.
`kCityTileClasses` in `city/maps/kinds.c`
supplies these overlapping flags in one immutable 256-entry `u32` table (1 KiB).
Bits 0–13 and 17–18 contain classification flags and bits 14–16 contain
one of seven layout selectors: fixed, stored variant, highway axis, road/rail connections,
power connections, building, or native bridge. Ramps use the highway-axis
selector with their full facing and camera-adjusted variant. The renderer masks and shifts these
three bits to selector values 0–6; there is no separate selector table. Cache a classification locally
when checking several classes of the same unchanged kind. Construction lookup belongs to the
building selector case, which also handles hydro plants' facing pair; other selectors skip it.
`ReadCityTileClasses` asserts the byte-sized input contract in debug builds and indexes directly
in optimized builds. Simple contiguous ranges remain arithmetic.
Bridge deck and underpass routes still require their tile metadata. The table is for native
surface kinds, not stock road/rail/highway artwork IDs. Entrances retain their native facing metadata.

Bits 19–22 of the same immutable class word select a `TileModuleId`; this adds no array or per-tile storage.
[The module interface](../src/city/maps/module.h) dispatches family drawing with prepared camera geometry and
scheduled landscape visits through `tick`. The registry lives with the immutable kind metadata in `maps/kinds.c`.
Ground, tree, water, power-line, highway, bridge, remains and building modules declare their callback records beside their
implementations.
Ground, tree, surface-line, power/crossing, highway and bridge ticks contain the previous landscape-visit branches.
Traversal order, cadence, early returns and RNG calls stay unchanged. A missing tick performs only the common
ground-cover refresh; economic zoning/construction growth retains its separate scan. The landscape traversal
does not classify tile families after dispatch. A missing draw callback or a false result
continues through the shared stock/custom artwork path. Feature and layout tests mask their own fields;
stock extension validation excludes the module selector when checking whether a kind has defined features.
The optional `draw_ground` callback paints each tile beneath a deferred sorted object. It receives prepared geometry
and draws only ground and underpasses; selection and overlays remain in the shared pass. A missing callback or false
result draws natural terrain. Building, tree, bridge, highway and power modules own their ground composition.

Kind predicates take an integer. Metadata accessors that only inspect or edit one record take
`const TileInfo *` or `TileInfo *`; raw map indexing, neighbour traversal and operations
depending on City settings keep `City` and `TileIndex`. Half/quarter-layer accessors derive one
`MapTilePt` and use `(width >> 1)` or `(width >> 2)` for their row stride.

| Native IDs | Meaning |
| --- | --- |
| 0 | Clear ground |
| 1 | Rubble; appearance variant in terrain metadata |
| 2–4 | Open water, river and water bank |
| 5 | Radioactive waste |
| 6–12 | Tree variants/densities; 9–12 are mature, 11–12 the densest pair |
| 13–16 | Power line without/with tower, on land then water |
| 17–18 | Ordinary road and rail; connections in terrain bits 4–7 |
| 19–21 | Road/power, road/rail and rail/power crossings |
| 22 | Highway ramp |
| 23–25 | Highway incline, bend and junction |
| 26 | Subway/rail transition |
| 27–30 | Highway, highway/rail, highway/road and highway/power |
| 31 | Tunnel entrance |
| 32–35 | Road bridge middles: clear, rail, road or power underneath |
| 36–39 | Rail bridge middles with the same underpass choices |
| 40–43 | Highway bridge middles with the same underpass choices |
| 44–46 | Road, rail and highway bridge heads |
| 47–49 | Road, rail and highway bridge middles over water |
| 50–111 | Available for future native kinds |
| 112–197 | Existing residential, commercial and industrial buildings, construction and abandoned variants |
| 198 | Hydro plant; orientation in terrain bits 4–5 |
| 199 | Small park, reclaiming the retired second hydro artwork slot |
| 200–255 | Existing power plants, civic/utility buildings, transport/military facilities, rewards and arcologies |

See [the complete named catalog](../src/city/maps/tile_storage.h). The ID selects simulation behavior as
well as graphics; changing an ID is not merely changing a sprite. Native trees use these IDs for
count: 6–8 contain one tree, 9–10 two, 11 three and 12 four. Species and age are stored separately.
Rough ground, rocks and recovering ground retain clear ID 0; permanent region survives buildings and rubble.

Dry bridge kinds occupy 32–46; the three water-middle kinds occupy 47–49. The middle families start on four-ID
boundaries, so the low two kind bits directly encode the underpass; heads 44–46 follow in
road/rail/highway order. Compile-time assertions in `tile_storage.h` enforce every offset used by
classification and family arithmetic. These are native storage IDs only: stock SC2 files and sprite
catalogs retain their original road, rail and bridge IDs through the existing translation boundary. Rail and highway statistic counters retain their stock
numbering independently of the native kind plane.
Reassigning native kinds changes the current native save representation; older project saves are
not migrated.

Native bridge heads store style in altitude bits 5–8. `BridgeStyle` defines explicit saved road/rail values 0–13;
highway heads use `HighwayBridgeStyle` values 1 standard and 2 reinforced. Both enums live in `tile_storage.h`.
These fields are independent of the `CitySpanStyle` chooser/command values. The bridge module converts admitted
choices explicitly and owns the style table for deck-length admission, geometry and palette selection. Drawbridge
approaches map to concrete artwork through that table without changing their saved drawbridge identity.
Native extension validation checks styles against the head's family; unknown highway style values are rejected.
Ground is in terrain's low nibble, and slope remains in the shared zone/slope byte. Middle kinds identify both the
transport on the bridge and the crossing underneath.
Routing reads those kinds directly. Drawing walks along the stored axis to find a head when it needs
style, elevation or the appropriate bridge piece. No per-piece style, piece number or extra tile byte is needed.
Every bridge placement and stock import uses these native head/middle records. Stock causeways
and suspension bridges select native concrete or suspension styles; rail bridges select
the native steel cantilever style. Their old piece kinds never remain in City.
Raising bridges preserve native style 13, using the existing **Raising bridge** choice at 5–11 tiles including heads.
Explicit endpoint placement requires water beneath the moving tile. Rail bridges do not offer this style.
The lower middle tile is the moving leaf; other deck tiles and heads use concrete artwork/palettes.
Open/closed state is derived from ships within four tiles in either axis, using the existing event hash. This covers
the ship steering probe's four-tile lookahead, including diagonal approaches. It adds no tile bits, timer or saved
buffer. The leaf uses the authored open/closed
sprites at every zoom and mirrors with the viewed bridge axis. Ship creation, tile movement and removal invalidate
only leaves whose open state changes; another nearby ship keeps the bridge open. Decorative cars wait before
the open leaf, and no new car spawns on it. A car already crossing when it opens finishes its cached path while
its sprite is hidden on the lifted section. The leaf closes once the last nearby ship clears the passage.
Stock export projects that middle tile back to the open/closed drawbridge kind; native saves retain the head style.
Stock import transfers the XBIT mirror bit into terrain bit 4 as the physical X/Y axis.
Landscape initialization preserves that axis; each camera projects it independently when choosing artwork.

Native bridge planning uses the water kind class to find the crossing and the
packed water height to require one level surface from bank to bank. The submerged
land height and 16/32/48-series terrain encodings do not choose separate placement
algorithms. Rapids and changes in water level are rejected. Every water tile stays
water beneath the bridge; approach tiles are dry, either flat or a simple slope
descending toward the crossing. With dry-ground ramps enabled their original
heights and slopes remain unchanged. A low flat bank gets an inclined structural
ramp; a descending bank gets a level supported head at the same deck elevation.
The structural ramp is separate from the stored land slope and needs no saved terrain copy. Native road/rail bridge
demolition preserves that slope. With terrain preservation enabled, highway bridge demolition also skips the stock
bank-repair operation, which would otherwise reshape nearby ground and remove its occupants. Older bridges built
with terrain grading enabled retain the altered land; their original slope was not saved.

Dedicated **Road Bridge** and **Rail Bridge** tools use explicit dry endpoints. Dragging chooses the
dominant map axis and the chooser collects a style before the City command executes. Heads are flat,
at equal height; the deck rises one level above them. A two-tile bridge consists of two adjacent
heads, rising and falling directly, with no middle; styles admitting zero deck tiles remain available.
Intermediate tiles may contain lower terrain,
rocks, water, or one straight perpendicular road, rail or power line. Junctions, parallel infrastructure, buildings
and highways below the bridge are rejected. The existing ground, water and underground remain intact.
Road and rail tools can also add a straight perpendicular underpass after a bridge is built.
Only dry, flat middle tiles with clearance below the deck admit this placement; bridge heads,
parallel lines and a different existing underlying network remain blocked. Adding the underpass
changes only the bridge middle's underlying category and clears removable rough/rocky ground.
Repeated placement reuses the existing line when infrastructure reuse is enabled.
The three transport types each have a distinct head kind and four middle kinds (see the catalog above).
Heads store facing explicitly; artwork pieces follow distance to the heads. For road and rail bridges,
styles 0–12 select the OpenGFX2 bridge catalog. Reinforced highway bridges retain the existing composite
artwork, with style 1 in their heads; their two lanes store only one width-position bit each.
The legacy 2×2 artwork grouping is derived from distance along the span, including after rotation.

Both highway water-span styles use native highway bridge heads and middles. Stock ordinary
highways over water are converted by complete two-lane footprints at import. Demolition follows
the two native lanes and preserves the ground and water beneath them.

| Field | Bridge head | Bridge middle |
| --- | --- | --- |
| `kind` | Road, rail or highway head | Road, rail or highway × clear/rail/road/power crossing |
| `terrain` bit 4 | Axis: 0 X, 1 Y | Axis: 0 X, 1 Y |
| `terrain` bit 5 | Inward facing: 0 positive, 1 negative | Reserved |
| `terrain` bit 6 | Reserved | Reserved |
| `terrain` bit 7 | Highway width position | Same |
| `terrain` bits 0–3 | Stored ground/coverage | Stored ground/coverage or underlying water kind |
| `zone` bits 0–4 | Slope discriminator and slope, or unzoned flat ground | Same |
| `altitude` bits 5–8 | Bridge style, 0–12 | Part of water/deck height |
| `altitude` bit 9 | Highway artwork phase | Part of water/deck height |
| `flags` bits 1–2 | Permanent region | Permanent region |

Stock highway bridge kinds 106/107 are codec/artwork IDs only. Import resolves
each complete two-lane span into native highway heads (`62`) and middles
(`56`, or `57`–`59` with an underpass), with explicit axis, lane and head facing.
The first and last tiles of each lane are heads; import retains the terrain
and water beneath them. The existing eight-byte tile holds all metadata.
Head altitude bit 9 retains the phase of the original alternating two-cell artwork
groups relative to the lower-coordinate end. New bridges use phase zero.
Rendering and SC2 export reconstruct 106/107; native gameplay and demolition
use the native bridge family. Native statistics aggregate the two old pieces,
and SC2/comparison export splits their counts back by the derived piece.
Native save validation rejects 106/107 as stored kinds; older project saves
containing them are not migrated. Highway footprint updates accept only land
and junction selectors, while span construction owns native bridge placement.

Routing compares the bridge/underpass edge height and retains the entered axis, so traffic cannot turn
between the two levels. Crossings contribute to ordinary road, rail or power statistics and survive
bridge demolition. Highway demolition removes both complete lanes. Underground bytes remain unchanged.
Normal elevated-highway underpasses retain their existing storage; this layout applies to bridges only.
Surface rectangle demolition removes native bridges after the other selected tiles. Removing a span
therefore cannot expose water or an underpass to another bulldozer action in the same rectangle.
Removing a bridge middle leaves its retained ground category untouched, including ordinary grass (`0`),
without starting a new bare-ground recovery cycle. Heads retain the complete ground nibble, including
snow and individual recovery stages. Heads and dry middles use the phased eight-cycle landscape
regrowth cadence; no ground counter overlaps their axis, facing or highway width metadata.
Water, rough ground and rocks do not regrow. Each changed tile is explicitly invalidated.

Bulldozing clear green ground beneath a bridge middle starts ordinary bare-ground recovery while
retaining the bridge, using the same grass-clearing cost setting as open ground. Clearing rough
ground or rocks beneath a middle also starts recovery. Already-bare brown ground, either on open
land or beneath a middle with no underpass, admits a zero-cost bulldozer no-op without changing
its recovery state or producing another explosion. Water beneath a middle remains blocked.
Uncovered water selected directly still follows the ordinary water-clearing behavior.
Native freshwater clearing is rejected while the stored water height is above the ground minimum:
changing only its water kind would expose a dry pit beneath the neighboring water. Raise/lower
landscaping continues to edit the bed through `SetCityTerrainWater`, retaining the existing water
level until ground exposure; zero-depth freshwater can still be cleared normally. Rejection changes
neither terrain nor cash, and explains that the ground must first reach the water level.

## Flags: `tiles[tile].flags` / XBIT

| Bit | Mask | Meaning |
| --- | --- | --- |
| 0 | `0x01` | Salt water |
| 1–2 | `0x06` | Permanent region code, interpreted by the City landscape preset |
| 3 | `0x08` | Temporary visited marker, shared by network scans and local disaster propagation |
| 4 | `0x10` | Watered/service state |
| 5 | `0x20` | Piped/utility capability state |
| 6 | `0x40` | Powered |
| 7 | `0x80` | Powerable |

All eight native flag bits have assigned meanings. Bits 1–2 form the permanent region code;
stock orientation and water flags are decoded at import and reconstructed at export.
The visited bit is temporary but can remain live across simulation work. Exact utility predicates
depend on tile kind and the operation.
Source: `TileFlags` in [tile_storage.h](../src/city/maps/tile_storage.h).

## Zone or slope: `tiles[tile].zone`

| Bits | Meaning |
| --- | --- |
| 0–3 | Zone kind when bit 4 is clear; slope 1–13 when bit 4 is set |
| 4 | Slope discriminator (`CITY_TILE_SLOPED`, `0x10`) |
| 5–7 | Clear-ground recovery delay 0–7, or tree growth 0–6 |

A flat tile stores its ordinary zone. A sloped tile is unzoned, following SimCity 2000’s
placement restriction. `ReadCityZoneKind` returns none for slopes; `ReadTerrainCode` returns flat
for zoned tiles. `WriteTerrainCode` removes zoning when installing a slope, and zoning writes require
flat terrain. Wind-power buildings use bit 5 for their construction-era design instead of growth.
All other occupants leave bits 5–7 clear.

`kTerrainRaisedCorners` in the map tables maps a terrain shape to its four raised
vertices: bits 0–3 correspond to offsets (0,0), (0,1), (1,1), (1,0). Geometry,
coast generation, stock projection and tile painters share this immutable lookup;
rendering uses the same mapping after rotating the shape. This is distinct from
the underground selection shape numbering, which also includes saddle and steep
selection artwork.

Gameplay reads these meanings through the accessors. `ReadTileZoneByte` composes the zone and
occupant metadata from their respective fields for retained algorithms; its result is not the stored
zone byte. Codecs project complete records into stock XZON. Raw native save planes retain the discriminator.
Tile-information windows use the same semantic accessor on their tile snapshots.

| Value | Zone |
| --- | --- |
| 0 | None |
| 1, 2 | Low/high-density residential |
| 3, 4 | Low/high-density commercial |
| 5, 6 | Low/high-density industrial |
| 7 | Military |
| 8 | Airport |
| 9 | Seaport |
| 10–15 | Reserved; not available as unrelated metadata |

## Occupant metadata: `tiles[tile].terrain` bits 4–7

The high terrain nibble has a tile-specific interpretation, independent of the ground state below it:

| Surface kind | Bits 4–7 |
| --- | --- |
| Clear and exposed water | Clear |
| Rubble | Bits 4–5: appearance variant 0–3 |
| Tunnel and subway/rail entrance | Bits 4–5: direction 0–3 |
| Highway ramp | Bits 4–5: direction; bit 6: mirrored |
| Trees | Local species: 12 temperate/tropical, 8 arctic |
| Ordinary road, rail and power lines | West/south/east/north connections = 1/2/4/8 |
| Bridge heads | Bit 4: axis; bit 5: facing; bit 7: highway width position |
| Bridge middles | Bit 4: axis; bit 7: highway width position |
| Ground crossings | Bit 4: primary network axis |
| Land highways | Bits 4–5: orientation; bits 6–7: footprint links |
| Hydro plant | Bits 4–5: orientation |
| Buildings | Bits 4–5: facing; bit 6: continue left; bit 7: continue up |
| Park | Clear |

Unlisted bits within a category are reserved and cleared on category changes.

Both continuation bits clear identify the footprint's top-left anchor. Every tile to its right
sets bit 6, and every tile below its row sets bit 7. Adjacent buildings and highway blocks have
independent anchors. Highway anchors come from these bits, including mixed road/rail/power crossing
blocks, rather than map-coordinate parity. The encoding supports rectangular footprints; current building definitions
still supply their existing square dimensions. Facing is stored uniformly over the footprint,
relative to the current map coordinates. Physical map rotation updates both facing and
continuation links; native artwork and dimension lookup use that facing directly. Stock import
adds the saved map rotation to the original facing, and stock export subtracts it to restore
the original mirror flag and hydro sprite pair. Building placement receives facing explicitly;
runway growth and aircraft spawning use the native physical axis. The stock building mirror is
`(facing - map_rotation) & 1`, so rotating both facing and the map leaves that exported bit unchanged.
Orientations 0, 1, 2, 3 encode as `0x00`, `0x10`, `0x20`, `0x30`.
Facing is the ordinary two-bit number `(terrain >> 4) & 3`.
Kinds that need only an axis can use bit 4 alone and assign bits 5–7 as a contiguous payload.
Building footprints still use bits 6–7 for their continuation links.

Rubble and these three entrance groups each use one native kind. Placement and destruction pass
the appearance/direction explicitly to `ReplaceCityTileVariant`; its stock path retains the original
kind and RNG sequence. Highway ramps store mirroring in terrain bit 6, so all eight original ramp
states remain representable without the flags orientation bit. Map rotation updates tunnel/subway
direction and ramp direction/mirroring;
rubble appearance stays unchanged. Drawing and retained directional algorithms use the stock artwork
projection. SC2 import decodes IDs 1–4, 63–66, 93–96 and 108–111 into the canonical kind and terrain payload;
export reconstructs those IDs and original zone roles, including the stock ramp mirror flag. Native save
extensions preserve the terrain payload; stock import transfers the ramp mirror flag to terrain bit 6 and clears it. Camera projection decodes the ramp's road and highway connection
directions, rotates both directions arithmetically and selects the viewed variant and reflection.
There is no clockwise variant lookup table or repeated quarter-turn lookup.

Runtime single-corner queries use `IsCityBuildingCorner` or `IsCityFootprintCorner`, with physical
corner indices 0=top-left, 1=bottom-left, 2=bottom-right, 3=top-right. Building queries read no
neighbours for the top-left corner, at most one for top-right/bottom-left, and at most two for
bottom-right; they short-circuit when local continuation bits already exclude the corner.
`IsCityMapViewFootprintAnchor` selects physical corner `(3 + camera_rotation) & 3` for drawing,
without generating or rotating a legacy corner mask. The ordinary growth scan selects physical
corner 3 directly, preserving its previous rotation-relative scan mask.

`ReadFootprintCorners` remains for callers needing the complete old four-corner mask, including
exact-mask comparisons. `CityBuildingCornerMask` supplies that mask for SC2 export using direct
right/bottom neighbour reads and one rotation of the completed nibble. Neither helper searches
for the anchor. `GetBuildingStartTile(city, tile)` returns the
top-left `TileIndex` by following the left/up links. Single-cell buildings return themselves
without reading footprint links; highway sections use their stored rectangular footprint.
The helper does not return dimensions or depend on view rotation.
`ResolveBulldozeFootprint(const City *, TileIndex)` returns an eight-byte `CityTileArea`: a
32-bit start tile and `u16` width/height, including rectangular highway footprints. The start is
always the northwest tile (the smallest tile index), independent of view rotation. The helper reads
the kind from the tile and returns `INVALID_TILE` with zero dimensions for an unresolved footprint.
Callers that need the old right/top anchor derive it as `area.start + area.width - 1`.
Demolition and disaster footprint operations take tile indexes directly; retained disaster work
keeps the same compact area across animation suspension.
Physical map rotation transforms facing and continuation links before
moving tile kinds, using existing rotation scratch. It preserves the payload roles of clear,
tree and infrastructure tiles.

`GetBuildingFootprintDimensions(kind, facing)` reads the same square-size table for every kind
in the building range, 112–255. It returns a `CityPoint` with width in `x` and height in `y`,
both `int` fields. Parks return 1×1; kinds outside the building range otherwise return zero
dimensions. Facing is reserved for future rectangular footprints and currently does not affect
the square dimensions. `MapTilePt(city, tile)` returns a tile's x/y coordinates as a `CityPoint`.
`MapTileXYOrInvalid(city, x, y)` converts optional coordinates to a `TileIndex`, returning
`INVALID_TILE` outside the map. Use `MapTileXY` when coordinates are already known to be valid.
`GetRelativeTileOrInvalid(city, tile, x, y)` applies x/y offsets from a valid tile and returns
`INVALID_TILE` if the destination falls outside the map, without wrapping between rows.
Existing building definitions retain their square sizes, including the 4x4 special buildings.
Native ground/selection traversal, construction progress and demolition use separate width and
height. The original positional square-size table remains unchanged for stock algorithms/codecs.
Rectangular artwork and placement definitions still need to be authored before adding a new
non-square building; changing a dimension entry alone does not provide those assets.

`ReadFootprintCorners` returns zero for trees, native rubble, tunnels, ramps, clear tiles, bridges and power-line tiles
except highway/power crossings, whose bits belong to the highway footprint. Their wire support is always a span
and is derived rather than stored. Parks and subway/rail transitions synthesize the singleton corner mask when needed.
Use `ReadTreeSpecies` from `trees.h` and `ReadGroundRecoveryCounter` from `ground.h`
for their family payloads. `ReadTransportConnections` and `CityPowerLineHasTower`
remain shared record queries. Tree count changes retain species. Category changes
initialize the new interpretation explicitly in `ChangeCityLandscapeTile`, called by
`ReplaceCityTileAndStatistics`. Actual low-nibble zoning remains independent, including under trees.
Camera rotation transforms connection masks for drawing without changing stored tiles.

Gameplay uses one native footprint layout: building/highway continuation links and explicit
highway axes. Stock files are identified at the import boundary by `native_map_storage == false`;
the loader converts the complete stock map before simulation rebuilding. Comparison imports stock
records through the same boundary and runs gameplay on native records. Earlier project-specific
save layouts have no compatibility guarantee. Native imports convert complete legacy footprints
using their original corner markers and sizes. `TranslateCityTileFromLegacy` reads the unchanged
source map and writes a complete eight-byte result, including kind and flags, into a temporary output
map. Import copies the records back and recounts kinds only after the entire conversion succeeds.
Simulation rebuilding then runs on native records, followed by native landscape initialization.
The temporary map is freed immediately; current-format imports need no conversion buffer. Straight
highways use the stock 2×2 grid only during import. Incomplete highway footprints and wet building
footprints are rejected. Incomplete dry multi-cell buildings, including isolated interiors in shipped
`SYDNEY.SC2` and overwritten loading bays in `JUNETOWN.SC2`, are cleared tile by tile. Terrain and
zoning remain intact; obsolete building utility flags and construction metadata are removed. Complete
footprints and their neighboring tiles remain unchanged. Loading displays the number of cleared tiles;
this transient diagnostic count is not saved.


With **Compact highway placement** enabled, a straight highway segment occupies one cell
along its axis and two across it: 1×2 on X, 2×1 on Y. A placement can start on any map cell.
This includes uphill and downhill segments. Both carriageways must follow the same grade,
and consecutive segments must meet at equal edge heights. Hills do not switch the drag to
the stock 2×2 planner or move its starting boundary.
Straight dragging selects its dominant axis and projects only the endpoint onto that axis, keeping the
starting cell anchored. Movement of at least two tiles along both axes selects the existing 2×2
bend planner, restoring diagonal routes without treating one-cell sideways movement as a turn.
Preview, pricing and endpoint commands share this routing decision. Both planners write native
tile records. Normal highway mouse releases commit through the endpoint command after collecting
bridge/border choices, including diagonal drags that do not use the compact planner.
A click beside a highway selects the axis needed to join it.
Placement rotation selects the isolated segment's axis and breaks equally connected choices.
The continuation bits identify the actual
rectangle independently of the option, so disabling it does not change existing segments.
Curves, junctions, imported shared inclines and water spans retain their existing larger pieces. Flat bends and
junctions reconnect automatically using the two cells across each neighboring edge, independent of
segment length or map parity. Inclines count as connections when both carriageways meet the
junction's edge height, so adding a branch preserves the sloped approach and selects an interchange
when three or four approaches meet. A click can reserve a 2×2 corner and charges for all newly occupied cells.
A larger piece may replace compact segments only when it contains their complete footprints;
existing road, rail and power crossings are not replaced by an automatic bend.
Dragging a perpendicular highway over existing straight pieces reshapes the covered cells and
reconnects their outer edges into bends or junctions. A 4×2 overlap can therefore join parallel
stretches with two bends. The drawn area must contain every reshaped piece in full; placement
charges only for newly occupied cells when infrastructure reuse is enabled.
Connections at a placement boundary must include both carriageways in alignment. A one-cell
stagger cannot join opposite carriageways. This also applies to bends, inclines and highway
bridge approaches; parallel highways may still sit alongside one another without joining.
Native highway placement is separate from stock 2×2 placement. `PlaceCityTransportLine` selects
`PlaceCityNativeTransportLine` or `PlaceCityLegacyTransportLine` once; a native rejection never
falls back to the stock path. The native command uses one plan for validation and cost, returns
before writes for `CITY_COMMAND_TEST`, and otherwise applies that same plan. Bridge styles are
explicit command options collected by the frontend before execution.
The native plan records land and water rectangles; previews and the command use those same bounds.
`FindCityNativeHighwaySpan` keeps the selected axis without stock bank grading or turn inference.
The highway module exposes shared plan, cost, preview and command entry points. Grade-edge checks,
alignment admission, reconnection, compact construction and native span/placement helpers are private
to `highways.c`; onramp execution is reached through `CmdPlaceHighwayOnramp`.
Land and water share the command's tile-writing loop. Reconnection recognizes
ordinary water decks and native reinforced bridge heads at their surface height, so neither bank
is inferred as an unrelated bend or disconnected from a junction.
Highway incline artwork also follows this surface shape: a sloped bed below level water must
not bend either carriageway. Dry highways continue to follow their land slopes.

**Preserve terrain beneath highways** is enabled by default. Highway placement
keeps the existing land heights and slope shapes instead of creating stock raised
platforms or flattening an incline's footprint. The prepared deck has its own
incline; drawing resolves its base from the highest ground in the bounded footprint
(one level below its top for an incline). Each ground cell draws at its actual
height, and picking still follows that ground surface. No deck-height payload or
additional tile bits are stored. Disabling the option restores stock grading for
future placements; existing terrain is not reconstructed. Stock comparison sets
the fix mask to zero and retains its original grading.

All land highway shapes store orientation 0–3 in terrain bits 4–5. Opposite orientations
share an axis: 0/2 use X and 1/3 use Y. Bits 6–7 retain the footprint continuation
links. Ordinary highways and crossings retain their four network kinds; inclines
use kind 35, bends kind 36 and junctions kind 37. The junction is symmetric and
placement writes orientation zero. Entrances use the same two-bit field, with
mirroring in bit 6 instead of a footprint link. Terrain's upper nibble retains its
ground/water payload; no per-tile storage is added.

Placement selectors are converted explicitly to shape kinds and orientations,
rather than added to a kind ID. Footprint initialization and crossing placement
preserve both orientation bits. Routing derives the axis from orientation parity
and checks transverse anchor alignment before joining separate straight footprints.
Lane changes within one footprint remain possible; bends and junctions use both axes.
Camera rotation selects artwork independently of stored orientation.

Stock import folds directional incline/bend kinds into their native shape kinds.
Stock export reconstructs the directional IDs from the stored orientation, while
native saves retain kind and orientation separately. Ordinary stock pairs 73/74,
75/76, 77/78 and 79/80 map explicitly to native kinds 44/46/45/47. Live statistics
combine each straight pair and each directional shape family; stock counter
exports split those totals by orientation. Native extension validation requires
all cells in a highway footprint to agree on both orientation bits.

Highway bridges retain their separate bridge encoding: terrain bit 4 stores the
axis, bit 5 the head facing and bit 7 the width position. Ground state occupies the low nibble;
bridge heads store style and artwork phase in altitude bits 5–9.

Sources: [zone constants](../src/city/maps/tile_storage.h), [metadata accessors](../src/city/maps/layers.h),
[footprint searches/demolition](../src/city/tools/demolition.c), [placement](../src/city/infrastructure/lines.c).

## Terrain: `tiles[tile].terrain` / XTER

Permanent water presence comes from `TCLASS_WATER` in the existing immutable kind-class table.
`HasTileWater(tp)` performs the shared inline lookup. `TK_CLEAR`, trees and bridge heads are dry.
Water kinds are exposed `TK_WATER` (2), `TK_RIVER` (3), `TK_WATER_BANK` (4), power lines 16–17,
bridge middles 47–49, hydro plants 198,
piers 223 and the whole marina footprint 248. Marina placement/import initializes one water plane across all nine cells.
There is no per-tile water flag and no new presence bit in terrain's upper nibble.
The stock verifier explicitly enables a guarded marina placement repair in Unicorn memory:
successful placement gives all nine cells the same water plane and salinity before the final redraw.
Placement admission, rejection continuations and RNG consumption remain unchanged.

Stock import converts unsupported submerged occupants, including trees and ordinary buildings, to
exposed water of the same underlying kind, retaining its geometry and showing a warning with the converted tile count.
Bridge import includes a dry approach at each end; unsupported spans without two dry banks are
converted to exposed terrain with the same warning. Stock exports reconstruct the original XBIT water bit.
Statistics retain their established stock categories; native saves retain the explicit water kinds.
Normal disaster flooding remains a transient overlay, independent of permanent water presence.

Bits 0–3 store ground state on dry tiles. The slope is in the zone/slope byte, including beneath
water and every occupant. Native slope shapes are 0–13.

| Low nibble | Meaning |
| --- | --- |
| `0` | Fully recovered grass |
| `1`–`4` | Stored snow coverage: 25%, 50%, 75%, 100% |
| `5` | Rough ground, or stone paving on surface road/rail |
| `6` | Rocks; cannot coexist with trees |
| `7` | Bare recovery state retained for ground clearing |
| `8` | Bare grass |
| `9` | One-third grass |
| `A` | Two-thirds grass |
| `B`, `C` | Half/full desert |
| `D` | Reserved shore ground state |
| `E`, `F` | Invalid |

Trees retain their own species and growth independently of this ground state. They cannot carry rocks.
There is no separate climate byte or underlying rough-ground state beneath stored snow.

Dry surface roads and railroads (ordinary lines, road/rail/power crossings, and lines beneath straight
highways) reuse these ground values: `0` is ordinary grass, `1`–`4` snow and `B`/`C` desert; `5` is stone paving instead of
rough ground, and `7`–`A` are the existing bare-ground recovery stages. New lines start at bare grass (`8`). The native landscape pass visits tiles in its existing pseudorandom order; each
line has a one-in-four chance per visit of advancing a stage. No recovery counter or extra storage is needed.

One adjacent completed qualifying building on a free side encourages paving, including luxury small homes,
better apartments, offices and selected developed factories. Completed developed buildings also qualify
when their local land value is at least 128 on the 0–255 scale. Construction and abandoned buildings never
qualify, and high land value on undeveloped ground does not qualify. Diagonal buildings do not count.
A free side has neither a road nor a rail connection. Bends, straight lines and junctions also inherit paving
when at least two reciprocally connected neighbors are paved. Connections must match the same network
across the edge, including road/rail crossings. A single paved neighbor is insufficient; isolated paved
loops can sustain themselves.

Eligible lines can go directly from bare ground to paving; otherwise they recover through the normal stages.
Paving without qualifying support returns to local ground cover on a later successful visit.
Changes between surface road/rail categories retain ground state; leaving those categories clears the paving
interpretation. Permanent region remains independent, so local cover returns through the landscape visits.
The default-enabled "Gradual road and rail paving" setting controls new bare lines and periodic ground changes.
Native saves retain the ground nibble; stock export projects ordinary terrain without paving metadata.
Successful stock city imports immediately pave qualifying lines, without consuming RNG, then settle inherited
paving until no more lines qualify. Native saves retain their stored ground state instead of rerunning this pass.

Exposed water stores its behavior directly in the tile kind:

| Kind | Behavior |
| --- | --- |
| `TK_WATER` (2) | Level open water that spreads with the open-water datum |
| `TK_RIVER` (3) | Confined channel, including its bank edges and sloped rapids |
| `TK_WATER_BANK` (4) | Ordinary water shore; retains open-water spreading behavior |

A river has no separate bank or rapids kind. Its bank artwork comes from neighboring water tiles.
Generation retains channel identity through carving and grading, including flat broad rivers,
small streams and their ground-level pools. Pools placed on existing flat land also use `TK_RIVER`;
open-water behavior would flood the surrounding terrace at the same height. That temporary marker
becomes `TK_RIVER` and never enters native tile flags; carved lakes and ocean tiles keep open-water kinds.
A river whose bed minimum equals its waterline and whose bed is sloped is a rapid. A sloping
bed below the waterline still has a level surface. Compound unsupported river slopes remain rapids
for placement even when drawing uses flat fallback artwork.

Open water and rivers leave the low terrain nibble zero. Exposed banks use it for ground recovery:
0 selects the normal climate shoreline; 7 and 8 select bare dirt; 9 and 10 select partial grass.
Banks use zone bits 5–7 for the modulo-eight recovery counter, independently of their bed slope.
Water infrastructure and buildings retain the
underlying exposed-water kind minus `TK_WATER` (0, 1 or 2) in terrain's low nibble. `ReadCityWaterGroundKind`
reads either representation and returns `TK_CLEAR` on dry tiles. Placement transfers this payload;
demolition restores its kind, so a bridge or hydro plant never turns a confined river into spreading
water. Marina cells retain their individual water kinds; dry members borrow the first wet member's
kind along with its common water plane. Native TILE records retain both kind and terrain without changing
the eight-byte record or adding map storage.

Water presence comes from the kind class; salinity remains in `TF_SALTWATER` and never selects
spreading behavior. Fresh open water can spread and salty river water stays confined. Adjusting the
open-water datum leaves existing rivers, including occupied rivers and rapids, at their local waterline.
Bulldozing exposed water at or below its bed minimum restores dry `TK_CLEAR`, preserving slope and
land height and starting bare-ground recovery in the same action, including in terrain edit mode.
Flat seawater draws no freshwater river-edge overlay beside the cleared tile; coastal banks retain
their own sloped shoreline artwork. Water deeper than the bed cannot be cleared. Native landscape
visits let open water and ordinary banks reclaim adjacent clear ground through edges at or below
their waterline; rivers do not spread. Reflooding restores the source water level and salinity and
chooses a shoreline kind for exposed sloping ground. Clearing itself never immediately refloods the tile.
Landscaping and lowering the water level also start bare-ground recovery when a water tile becomes
dry clear land. Immediate climate refreshes preserve this bare phase instead of making it green.
New or reshaped exposed banks also start bare-ground recovery while retaining their water plane and slope.
Each stage lasts eight landscape visits, ending at normal climate ground. Native saves retain the stage and
counter; stock export omits them. Unchanged banks retain their current recovery progress, and generated or
stock-imported banks start with their normal climate shoreline. `ReadGroundKind` exposes this bank recovery;
other wet tiles report ordinary grass. Bridge heads are dry and retain ordinary ground state; style lives in
their altitude word.

The saved per-city rule `CITY_FIX_WATER_SPREADING` controls whether adjacent open water or banks can reclaim
flat clear land. It defaults on for new cities and off for stock imports, preserving stock coastlines whose
dry land is level with the water without changing geometry. Shore slopes remain eligible with either setting,
subject to the shared-edge and water-height checks above. Rivers remain confined by kind. The rule uses the
existing City settings mask and does not add tile metadata.

Stock 16-series water imports as open water, 32-series shores as water banks, and 48-series channels
and waterfalls as rivers. Inferred corners on a level stock channel are bank decoration, not rapids.
Stock export reconstructs those terrain codes and river bank patterns at the boundary. The stock
flood search still distinguishes ordinary shallow banks from channel water independently of salinity.

On wind-power buildings (kind 200), zone bit 5 (`0x20`) stores the construction-era design:
zero selects the lattice windmill, one the modern three-blade turbine. Placement sets it from
the absolute construction year, with 1980 as the first modern year. The ground nibble remains
available for snow/desert cover. Category changes clear the design bit; stock projection omits it.
Pure stock imports use the old design because individual turbine dates are absent.

Water and ground heights remain in ALTM. `TK_WATER` has a level surface. Water banks show
raised shoreline corners only where the bed minimum equals the waterline; fully submerged banks
remain level. Rivers use their stored bed slope for rapids at the waterline, independently of salinity.
Hydro plants use surface kind 198, distinct from the water/ground geometry beneath them. Terrain bit 4
stores mirroring and bit 5 selects the original hydro sprite pair (198/199); bits 6–7 remain clear.
Import combines the old kind and orientation into this payload. Drawing and SC2 export select the
original sprite ID from it. Power production and facility counts use the single native kind.
Native statistics count kind 198; SC2 export splits its original counters when hydro plants exist.
Both orientations contribute $400 to city assets, without a map scan. Comparison runs explicitly
repair stock kind198 valuation to $400 in Unicorn; the shared coal construction price remains $4000.

`ReadTerrainCode` decodes the zone/slope byte. `WriteTerrainCode` preserves its growth/timing bits
and leaves ground and occupant metadata untouched. Grass recovery is `8 → 9 → A → 0`.
`BeginCityGroundRecovery` starts at bare grass, or immediately recovered ground in terrain edit mode.
Snow has four stored coverage states and approaches the elevation target during landscape visits;
desert uses half/full ground states. Cover changes do not modify species, tree growth or permanent region.

SC2 files and comparison transport retain combined XTER codes 16–69 and the original bit-7 metadata.
The City behind comparison transport uses explicit native bed slopes.
Their decoder and connection-pattern projection live in `file/city/map_legacy.c`. Import resolves missing
channel bed shapes once against the unchanged source records, stages all results, then publishes native slopes.
Legacy waterfall beds follow the decoded river slope; their native water datum becomes the slope's base.
Original connection codes and rotation-table padding are never stored in ordinary native terrain.

Before conversion, decoded stock river/pond banks are normalized from their eight neighboring water
flags. This is mandatory: the native format does not preserve history-dependent bank artwork.
Only wet bank codes 48–61 and 64–69 participate; waterfalls, unknown codes, heights and flags are
unchanged. The pass preserves XTER bit 7 and uses the same bank calculation as stock export.
It runs in memory once at the native import boundary, without modifying the source file, allocating
a map layer, or borrowing climate/recovery bits. Native saves retain their existing geometry.
Diagnostic replay also uses native tile storage. With the native-landscape fix disabled it skips this
normalization; comparisons enabling the repair must explicitly normalize their stock baseline too. Exact original bytes for stale
banks are intentionally outside the native round-trip contract.

Native landscaping edits shared vertices and encodes each affected tile as its minimum height
plus raised-corner shape. Diamond mode edits all four vertices; point mode edits the north vertex
shown by the indicator. Lowering a diamond keeps corners already at zero in place and lowers its
remaining raised corners; the sea-level error applies only when every selected corner is at zero.
Propagation keeps adjacent tile corners within one height level and resolves
opposite-corner saddles, which have no SC2 terrain sprite. Cost queries build the same bounded local
plan as execution. Terrain refreshes preserve this geometry rather than reconstructing it from tile
minimum heights. Submerged terrain retains its slope in codes 16–28 while drawing a flat water plane;
changing the water level can expose the saved ground again. With automatic filling disabled, dry
land stays dry after landscaping; existing water and explicit water-level controls retain their behavior.

### Shared terrain and water geometry

Native callers read water presence from the kind class and salinity from `TF_SALTWATER` and use `ReadCityTileGeometry`
in [`city/maps/terrain.c`](../src/city/maps/terrain.c) for complete geometry, rather than interpreting
terrain ranges as water presence or treating every low nibble as a slope. The computed description contains:

- Ground minimum height and raised-corner shape, including submerged ground. Native decoding
  normalizes the corner-equivalent shapes 14/15 to prepared shapes 13/0.
- Water kind is `CITY_WATER_NONE`, `CITY_WATER_FRESH` or `CITY_WATER_SALT`. It combines
  the kind class with salinity; it is not a flag byte. A dry tile may retain `TF_SALTWATER`.
  Diagnostic legacy mode retains the original water-form classification.
- Stored water height, zero for dry tiles. Stale water bits and building construction stages
  never lift dry ground.
- Visible surface baseline and shape, shared by drawing, picking and damage bounds.
  Explicit stock banks retain their submerged bed shape independently of the visible surface.
  Sydney's beds at heights 4–5 lie beneath flat water at 5, adjoining height-5 land. These draw
  flat bank edges at water level. Raised shoreline artwork is used only when the bed minimum
  equals water height, leaving its raised corners above water.
- Water surface form: level, mixed shore, rapids or unsupported encoding. Flat fallback artwork
  for an unsupported rapid does not make it admissible as level water for bridge construction.

Native geometry reads the stored bed slope directly. It does not reconstruct channel beds or waterfall
directions from neighboring heights on redraw. River and explicit level water-bank decoration read eight
neighboring water kinds, so edge/corner artwork can express combinations absent from the original twenty bank patterns.
Read-only decompilation of stock `DrawCityMapLargeTile` (`0x43C730`) confirms that wet tiles use the packed
water height; the terrain sprite table uses the same flat bank family for 32-series and 48-series banks.
Saltwater shores can clip shared edges against adjoining flat freshwater at river mouths.
Exposed sides of mixed shore slopes use the same prepared terrain faces as dry slopes.

Shared-edge height sampling reads both land minima once before visiting its two
endpoints. Shore-slope sampling keeps the existing cardinal neighbor order and
skips non-river ground before height decoding; each relevant river level is read
once. These queries use only stable tile records, with no mutation or callbacks.
Corner masks, water classification and the resulting geometry retain their rules.

`SetCityTerrainWater` is the shared native writer for terrain/water-level edits. It preserves
existing salinity and water behavior, and borrows neighboring seawater salinity when covering dry
land. Ordinary water selects `TK_WATER_BANK` when sloping ground reaches the waterline. Partially
exposed river beds become dry, with banks drawn by adjoining river tiles. Existing water keeps its
local level during terrain refreshes. Callers select whether existing sloped rapids survive an edit:
landscaping disables preservation, while river creation and terrain rebuilding retain it. Callers
invalidate changed tiles explicitly. Legacy channel connectivity is reconstructed only for SC2 export,
comparison transport and stock-art presentation. Native geometry needs no additional map storage.
Native underground drawing, picking and selection use the stored ground minimum and slope directly,
including submerged beds and ground under bridge approaches. The bilinear patch is built from that tile's
four stored corner bits; neighboring tiles do not raise or reshape it. Water changes the underground
palette rather than replacing the bed with a flat water-height plane. Utilities follow the same patch.
Neighbor reads remain for utility connections and exposed soil faces, not for reconstructing ground.
Legacy diagnostic storage retains its separate terrain/artwork interpretation.
Native generation resolves unsupported opposite-corner saddles after its final water carving and
bank grading, before encoding the stored bed slopes. This reuses the bounded generation queue;
it does not replace an unsupported saddle with an all-corners-raised cap during encoding or drawing.
After grading banks, native generation also checks the two ground corners along each dry shoreline
edge. Water that would stand above that edge drains to the containing level; the bed and nearby
ground are graded together using the existing bounded work queue. This preserves water footprints
and salinity while preventing generated lakes from relying on retaining-wall artwork for containment.

## Permanent region and stored cover

There is no per-tile climate field. Flags bits 1–2 store one of four permanent region codes:

| Code | Mixed preset | Desert preset |
| --- | --- | --- |
| 0 | Temperate | Tropical |
| 1 | Arctic | Desert |
| 2 | Tropical | Rainforest |
| 3 | Desert | Tropical (unused duplicate) |

Normal, Snowy and SimCity Classic presets derive one uniform base family from the City setting.
The two region bits survive construction, demolition, terrain edits and tree aging. Snow is a
stored ground state, not a permanent region or a rendering-only altitude test.

`ReadCityTileBaseClimate` resolves the permanent family for species and ecological rules.
`ReadTileClimate` combines it with stored ground for basic appearance. `ReadCityTileClimate`
selects grass transitions from the four cardinal neighbors’ permanent regions. The old sixteen
`CLIMATE_*` values remain derived artwork/command identifiers; they are never stored in a tile byte.

Arctic ground approaches 0–4 snow coverage from land height and the local snow line. Editor
terrain edits settle coverage immediately. In city mode, landscape visits approach the target
one step at a time. Tree growth and species survive these changes. Snow-covered arctic trees
select their matching winter artwork from the stored coverage.

Desert follows the original OpenTTD 0.1 rule: permanent desert tiles become fully sandy;
non-desert tiles with a cardinal desert neighbor become half sandy. Other ground retains its
grass family. The half-sand border is derived from region neighbors without a saved transition code.
Temperate/arctic/tropical grass blends are similarly derived on one side of each boundary.
Banks borrow adjacent dry-land coverage; drawing uses bounded neighbor reads and prebuilt sprites.
No whole-map pass or extra map layer is needed during drawing.

Generation sets permanent regions from the temperature/moisture controls, then settles stored
coverage after trees and rough/rock ground are initialized. Temperature and moisture controls
remain spaced 64 tiles apart: 18 bytes at 128×128 and 8,450 bytes at 4096×4096, in the existing
map allocation. Native saves store their values in CLIM and the snow line, variation, preset and
moisture threshold in META. Stock exports use XGND/XNAT. Rendering never reruns the terrain generator.

Native bridge styles occupy head altitude bits 5–8 and highway phase bit 9. Bridge middle
crossings remain encoded in kind. Those category-specific meanings do not consume region or snow bits.

## Underground: `tiles[tile].underground` / XUND

Bits 0–5 hold the ID; bits 6–7 are independent metadata:

| IDs | Meaning |
| --- | --- |
| 0 | Clear |
| 1–15 | Subway straights, slopes, bends and junctions |
| 16–30 | Pipe straights, slopes, bends and junctions |
| 31–32 | Perpendicular subway/pipe combinations |
| 33 | No named role established in the current enum |
| 34 | Missile silo |
| 35 | Subway access/station connection |

`CITY_UNDERGROUND_KIND_MASK` is `0x3F`; `CITY_UNDERGROUND_METADATA_MASK` is `0xC0`.
Semantic readers exclude metadata and semantic writes preserve it, including clearing/replacing
infrastructure. Rotation remaps only the ID; saves retain the full byte. No climate/growth meaning
has been assigned to the two metadata bits yet. Source: `TileUndergroundKind` in
[tile_storage.h](../src/city/maps/tile_storage.h).

## Associations/effects: `tiles[tile].overlay` / XTXT

| Bits | Meaning |
| --- | --- |
| 0 | Sign present; text and location live in the City sign hash table |
| 1–4 | Stationary effect: 0 none; 1–15 preserve stock marker IDs 241–255, including reserved marker codes |
| 5–7 | Reserved; zero |

Facility presence is derived from building kind and the footprint hash lookup; no tile bit stores it.
Stock shared records resolve directly from building kind.

Signs survive fire and moving events because their presence bit is independent. Moving events retain
City-owned tile positions and their own hash table. `ReadOverlay` resolves associations/effects for
simulation callers; its IDs are not the stored byte. Extended sign and facility IDs occupy disjoint
ranges beyond the bounded event namespace.

Every civic, power, utility and transport facility placed through the building tools receives a separate
microsim record in the default mode,
including wind/hydro power, stations, large parks, museums, libraries, marinas, pumps and water towers.
Records contain the eight-byte simulation payload, a footprint tile key, a separate `u32` chronological
age and a bounded name. The original payload's `age` byte retains its type-specific meaning, such as
school quality. Sparse pools grow with placed objects and are bounded by map area; there is no
map-sized facility index or sign array. Demolition releases the record; plant renewal resets its age.

“Individual facility records” in SimCity 2000 fixes selects this behavior. Disabling it restores the
stock ten reserved slots, shared category records and 150-record allocation limit. Shared slots resolve
from building kind; individual slots resolve through the footprint hash. Comparison explicitly disables
the option. Both modes use the same native tile encoding. Stock codecs project IDs 1–50 for signs,
51–200 for facilities, 201–240 for events and 250–255 for connection/effect markers.

Native saves serialize facility records/ages and sign tile/text records, then rebuild both hashes on
load. Hash buckets and links are never saved. See [native saves](sc2k-native.md).
Sources: [overlay constants](../src/city/maps/tile_storage.h),
[association storage](../src/city/maps/labels.c), [SC2K codec](../src/city/saveload/sc2k.c).

## Altitude word: `tiles[tile].altitude` / ALTM

This is one `u16`, not two independent bytes. Decode the word before interpreting its fields.

| Bits | Mask | Meaning |
| --- | --- | --- |
| 0–4 | `0x001F` | Land height, 0–31 |
| 5–9 | `0x03E0` | Water height, construction stage on dry buildings, or deck height on dry bridge middles; head bits 5–8 store style and bit 9 artwork phase |
| 10–14 | `0x7C00` | Tunnel depth/height field |
| 15 | `0x8000` | Opaque saved flag, preserved by field edits |

Dry native buildings (surface ID at least 112, without the water class) always interpret bits 5–9 as
construction stage, 0–31. `ReadCityWaterHeight` returns their land height instead of interpreting
progress as water elevation. Stock import clears the unused water-height field on those buildings;
native saves retain their stage directly. No runtime storage-mode flag or first-use map conversion
is needed. Real water still stores its local surface elevation. Terrain-water removal clears the
former water height on a building before it can be read as progress. Disaster floods use overlays,
preserve terrain water fields and can demolish building footprints through the disaster commands.

With native kind storage and the Building construction stages option enabled, a new zoned site
chooses its eventual building kind immediately. The kind stays in the tile throughout construction;
all cells in its footprint share progress 31, 23, 15, 7, then 0 (finished). Successful ordinary
construction-growth opportunities advance one stage. Unfinished sites contribute construction
counters and pollution/density values, not completed-building population. Legacy projections export
the matching generic construction kind; the existing native kind plane and altitude word retain the
chosen building and progress when saving natively. No additional per-tile allocation or format version
is required. Turning the option off affects new sites; work already underway continues to completion.

`ReadAltitudeWord`/`WriteAltitudeWord` read/write this complete packed altitude word. No part of the
word is globally free. Explicit native bridge spans borrow bits 5–9 on dry bridge tiles for the deck's
base height; the road deck is one level above that base. Bridge heads derive the deck from their bank's
land height plus one, without depending on the dry tile's borrowed water-height payload. Wet tiles retain
their real water height, and an elevated wet deck finds its height through the bridge head. Demolition
resets the borrowed field to land height on dry tiles. Clear/tree tiles do not use this encoding.

Sources: [masks](../src/city/maps/tile_storage.h), [interpretation](../src/city/maps/layers.h),
[construction-stage lifecycle](../src/city/growth/zoning.c).

## Working storage and other arrays

- `scratch`: full-grid capacity of 16-bit words, also read as signed `i16`. Environment passes reuse
  different regions for smoothing, influences and accumulations; terrain tools reuse it for heights.
  It is not persistent per-tile metadata. Its live contents can matter while simulation work is
  suspended. SC2K native resume data includes it.
- `route_points`: a preallocated ring of four-byte `PointU16` coordinates, shared by utilities,
  terrain propagation and transport paths. Capacity is `512 * max(width, height) / 128` entries:
  2 KiB at 128x128 and 64 KiB at 4096x4096. One slot distinguishes empty from full. Read/write
  cursors and the capacity mask belong to City; a full flood queue drops its oldest entry.
- `route_queued`: one bit per tile for flood-queue duplicate suppression, allocated at every map size.
  The corresponding City fix enables its use. Transport paths permit revisits and bypass suppression.
- `water_source_order`: a separate array of up to 8,192 `u32` source-order keys on non-128x128 maps.
  The water scan reuses it to visit sparse sources in the established order; an unavailable or
  insufficient workspace uses the ordinary ordered scan. It does not share storage with the route ring.
- `history`: 16 series of 52 `u32` samples, 3,328 bytes total. These are graph histories, not spatial
  tile data; SC2 stores them in `XGRP`.

Sources: [allocation](../src/city/state.c), [working passes](../src/city/maps/environment.c),
[history constants](../src/city/economy/types.h), [native resume codec](../src/city/saveload/sc2k.c).

Power and water source scans, component measurement/distribution and supply totals live in
`city/infrastructure/utilities.c`. Their public interfaces are in `infrastructure/infrastructure.h`;
component helpers are private. Incremental scans receive the scheduler's existing retained work explicitly
and return false while work remains. The scheduler owns phase selection and batching, while utility code
owns the scan/component stages. Whole-pass scans keep their component work local. Every traversal requires
exclusive use of the shared route queue and visited flags until completion.

## Tile updates

`city/maps/state.c` owns the pending tile, layer and full-map change flags and their reset.
`InvalidateTile` records the City batch, invalidates map information, then calls the direct
`PublishCityMapTileDamage` presentation hook synchronously. UI projects the changed tile into
visible viewports using old/new Drawing extents. Bulk layer/full notifications keep their existing
batch behavior. Presentation clears the batch after all views receive it; City also clears it
when replacing map storage. No callback registry, deferred tile queue or second change record is used.

`ReplaceCityTileAndStatistics` in `city/maps/layers.c` owns category changes: statistics,
footprint/connection metadata, construction progress, tree initialization and ground recovery.
It keeps the old kind installed until metadata conversion finishes, then writes the new kind.
Conversion snapshots the old resolved zone before changing metadata and reuses ground decoding
only while the record is unchanged. Clearing growth uses `CITY_TILE_GROWTH_MASK`, preserving
the zone/slope discriminator and low nibble.
Callers finish their explicit variant/orientation edits and invalidate the resulting extents.
There is no separate landscape-change call to pair with a kind assignment.

Demolition has two levels in `city/tools/demolition.c`. `ApplyCityBulldozerAtTile` checks
admission, charges the resolved tool cost and returns success or a negative gameplay error.
Admission reuses the selected tile's borrowed record for local field reads, retaining the
underground kind mask independently of tunnel depth. Execution snapshots the surface kind
before payment and keeps its later live reads and reentrant notice boundaries; it does not
retain a tile borrow across them.
`BulldozeCityTile` is the shared structure-removal body used by tools, terrain and disasters;
it performs no pricing or payment. Tunnel and linked-highway removal live with those infrastructure families;
connected runway/pier removal lives with buildings. Highway ground recalculation retains the footprint traversal
order in `highways.c`. The shared demolition body replaces ordinary footprint cells, clears power state and
invalidates the completed area. Family operations use shared kind replacement for counters/category conversion.
Clearing an exposed water tile and clearing a bridge underpass are raw edits in `water.c` and `bridges.c`;
the tool handler owns admission/payment and reconnects neighboring networks after the edit.
Cell and neighbor order are significant: rubble selection consumes RNG during traversal.

Physical terrain queries and reconstruction live in `city/maps/terrain.c`, declared in `maps/map.h`.
`ReadCityTerrainVertexHeight` samples shared corners for the edit planner using the same corner masks as map geometry.
`RecalculateTerrainCell` preserves stored native slopes and water levels after removing incompatible construction;
`RebuildCityTerrainCell` and `RefreshTerrainNeighborhood` reconstruct corners from tile minima for generation and
disaster callers. Edit admission, propagation, prices and payment remain in `city/tools/terrain.c`.

River conversion and stream placement belong to `city/maps/water.c`, declared in `maps/water.h`.
`CreateCityRiverTile` converts an admitted dry tile to fresh river at its bed height while preserving the slope.
Existing water is unchanged. `CmdPlaceStream` retains its source admission and execution RNG sequence.
Stock regrade offsets used by codecs and compatibility tables live in `saveload/terrain_format.h`, separately
from native generator settings and native slope shapes.
Native terrain recalculation after demolition preserves dry ground and existing water levels. Applying the
editor sea level belongs to terrain edits that change vertices, so deleting a highway does not flood its footprint.
Flat dry tree tiles accept zoning without removing their trees or local species/growth metadata. Preview, quotes
and execution use the same admission predicate. The sorted ground pass paints the zone wash beneath the tree
silhouettes, including when the tree layer is hidden; placement selection is still painted once.

Native tree and bulldozer operations take explicit options. The Shift/Alt/selected-price adapters and the retained
zoning drag caller live in [city/comparison/tool_placement.c](../src/city/comparison/tool_placement.c), outside
normal play. Core zoning separates preparation from applying the live construction state; it never collects pointer
input. A retained drag can overwrite that working state through nested commands, so its adapter pays the live cost
and applies the same live state after dragging. Raw zoning quotes are const queries and perform no payment.

Building footprints, construction progress, purchase definitions, placement and drawing live together in
[city/maps/buildings.c](../src/city/maps/buildings.c), with contracts in
[buildings.h](../src/city/maps/buildings.h). One property catalog emits the compact footprint, population,
category, pollution and density columns; growth scheduling consumes those columns and invokes building operations.
Square facility admission and installation use the same building module; tool selection maps to an explicit kind
before placement. Pricing and execution share the same footprint edge check, using width for columns and height
for rows on rectangular maps. The shared renderer routes prepared geometry and fallback sprite IDs to the family;
Drawing exposes view-option flags, catalog dimensions and scoped effect frames.
Building drawing uses fractional sprite coordinates for smoke without changing Drawing's internal offsets.
The building module also owns baked-ground coverage and ground-material selection. Sorted ground callbacks and
whole-footprint drawing share that policy; whole-footprint drawing reads the building's ground flags once.
Artwork-slot projection belongs to Assets and has no dependency on City or a viewport.

Facility classification, record allocation, label initialization, annual accounting and reward availability live in
[city/economy/facilities.c](../src/city/economy/facilities.c). Its [header](../src/city/economy/facilities.h) owns
record layouts, stadium identifiers and the deterministic annual continuation. Callers keep one work record through
aging notices and finish the same scan. The facility module also restores rewards and releases stadium teams before
shared demolition replaces cells and clears their overlay associations. Allocation retains its slot reuse and RNG
rules. Tools own only selection
policy; their mapping uses named group/variant initializers, and the facility class table uses named tile kinds.

Newspaper problem ranking, article-score decay and event insertion live in
[city/events/news.c](../src/city/events/news.c), with their state and public contracts in `news.h`. The City-owned
`news_records` contains seven ranked articles plus two composition slots; each `CityNewsRecord` retains its
8-byte ID/score/data/cache layout. Comparison root labels retain their original transport names. Historical
series and chart calculations stay in `economy/history.c`; `RecordCityMonthlyHistory` owns sample advancement,
while `DecayCityNewsRankings` subtracts the per-article baseline and reorders the ranked records. Neither operation
is a facility-ranking pass. Insertion and notice/continuation ordering remain unchanged. Startup calls `InitializeCityNews`,
which sets the existing news flags, seeds the CRT generator from the signed low word of logical time and consumes
122 draws to initialize/shuffle six stored editions. Edition records remain five bytes each; the shuffle and byte
swap helpers are private to the news module. No runtime buffers are allocated by this initializer. `CITY_NEWSPAPER_EDITION_COUNT`
defines the six valid edition indexes (0..5) for storage, initialization, scripting and diagnostic admission.
`IsCityNewspaperEditionValid` rejects negative indexes and the nonexistent seventh edition before replay dispatch.

The deterministic RNG interface and its three-stream `SimulationRandomState` are declared in
`city/engine/random.h`; implementations and all three seed functions live in `random.c`. Callers include that
interface directly instead of relying on scheduler declarations. CRT, map and event draws retain their existing
algorithms and order; the independent native landscape generator retains its separate seed.

Logical City clock operations remain in `city/engine/simulation_clock.c` and `simulation.h`. Game delay advancement,
clock-sample observation and cadence delivery live in `game/timing.c`, declared in `game/timing.h`. Activation polling
belongs to `game/update.c`; UI applies its effects through `ui/input.h`, and palette refresh is declared by Drawing.
These moves preserve observer ordering and do not sample host time while advancing the logical City clock.

Monthly weather transitions live in [city/engine/weather.c](../src/city/engine/weather.c), alongside their private
transition and property tables. `weather.h` declares the four-byte `CityWeatherState`, status identifiers and
`AdvanceCityWeather`. The economy continuation invokes it after milestone dismissal, preserving one CRT draw and
reading the live quarter/weather after any nested commands. The scheduler header does not expose weather tables.

Tile-kind totals (`CityStatistics`/`CityTileCounts`) are declared with map-derived types in `city/maps/map_types.h`.
Growth and map queries use that contract without importing the economy state header; City still owns one totals record.

Zone growth admission, population accounting, construction and decline live in
[city/growth/zoning.c](../src/city/growth/zoning.c). `GrowCityZonedTile` processes one scheduled tile, including
airport/seaport growth and vehicle spawning; military growth delegates to its own domain. The same growth module
owns interleaved visit order and the shared large-city-effect decision through `ScanCityTileBatch` and
`ScanQuarterCityTiles`. Reserved-zone underlays, infrastructure maintenance and facility activity retain their
existing scan positions. The simulation scheduler selects quarters and batch sizes without owning those rules.
Single-tile growth, immediate footprint selection and large-city church replacement are private to `zoning.c`.
The public growth header exposes batch scheduling and shared growth operations without importing tile storage.
Environment calculations remain in [city/maps/environment.c](../src/city/maps/environment.c), with their retained
`CityEnvironmentScan` declared in `map_types.h`. They own stage transitions and exclusive use of the existing
scratch array until the pass finishes; no additional workspace is allocated. Each stage processes complete rows
through direct calls, with no section dispatch or function-pointer calls. Incremental budgets still count rows in
the current stage's resolution. Columns retain west-to-east order, including the four-tile influence groups whose
coarse visited writes can affect later reads. Traffic decay belongs to
[city/infrastructure/transport.c](../src/city/infrastructure/transport.c): the incremental scheduler accumulates
section totals, while whole-map decay invalidates the layer and processes sections in the same order.

Road/rail cell placement, subway-to-rail transitions, connections, foundation policy and diagonal artwork
live together in
[city/infrastructure/surface_lines.c](../src/city/infrastructure/surface_lines.c), with contracts in
[surface_lines.h](../src/city/infrastructure/surface_lines.h). Reconnection preserves north/south/west/east order,
including the rail rule where the first matching incline chooses the axis. Placement uses direct road/rail calls.
Ground queries distinguish stored-world geometry from an explicit camera rotation; road cars use the world result.
Their derived `CityMapSurface` value belongs to the surface-line header, without a Drawing-header dependency.
The same module paints transport foundations and composes fallback transport artwork, delegating tunnel portals,
highway footprints and bridge decks to those families. The shared renderer supplies camera geometry after its
visibility/detail checks and keeps traversal, clipping, depth collection and overlays. Transport composition keeps
ground, placement, object and power-warning order; it reads City without changing simulation state or RNG.
The renderer retains its direct ground path for ordinary roads and tracks. Underground connection updates and
cross-family neighborhood refresh stay in `networks.c`, which also owns underground writes and subway totals.
The transition preview and execution share a read-only neighbor search: surface rail takes precedence over
underground subway, with south/east/north/west priority within each pass. Shared line admission, charging and
tracing stay in `lines.c`. Resolved road, rail, power, pipe, subway and highway commands prepare their endpoint/cost
there directly, without entering a UI drag collector. The collector also delegates already-resolved input to this
planner; actual pointer tracking keeps its existing input order. Failed plans keep their partial construction writes
and do not install a new cost. Drawing owns the sprite cache, clipping, pixels and current ground/object pass.
Geometry queries, span searches and cost APIs accept a const City. Route traversal writes
only the supplied planning state/cursor: quotes use local state, while execution can supply City.construction.

Road-tunnel entrance search, construction, removal and portal drawing live together in
[city/infrastructure/tunnels.c](../src/city/infrastructure/tunnels.c), with contracts in
[tunnels.h](../src/city/infrastructure/tunnels.h). Command admission remains read-only; execution charges once.
The retained confirmation path keeps its opening geometry and reloads the live construction cost after dismissal.
Removal returns the far entrance to shared demolition for its final cleanup area, preserving rubble RNG and effects.
The renderer supplies the portal's resolved sprite after its normal visibility/detail checks; ground and portal
use Drawing's existing terrain, clipping and depth services without importing UI or Drawing internals.

Highway planning, onramp admission and placement, junctions, footprints and drawing live together in
[city/infrastructure/highways.c](../src/city/infrastructure/highways.c), with explicit contracts in
[highways.h](../src/city/infrastructure/highways.h). Local orientation/axis, ramp-mirror and
footprint-selector helpers belong to that interface. The module owns ramp views, diagonal joins,
deck heights, crossing ground/deck separation and sprite reflection. It uses Drawing's terrain and sprite
services without importing UI or Drawing internals. Shared bridge encoding stays with bridge spans and
stock conversion with codecs.

Native bridge metadata, validation, construction, underpasses, removal and drawing live together in
[city/infrastructure/bridges.c](../src/city/infrastructure/bridges.c), with contracts in
[bridges.h](../src/city/infrastructure/bridges.h). Local axis, head/style accessors and
bridge encoding constants belong to that family interface; the kind-only bridge-head mapping
lives in `kinds.h`.
Whole-span traversal and view projection belong to the family. The shared span planner retains line tracing,
style selection and command continuation order. Water-power span removal belongs to `power_lines.c`;
demolition tries the power and bridge families in their original order. Ship movement supplies level-water geometry;
the bridge module decides passage beneath middle pieces, including highway gaps and drawbridge state.
Bridge drawing shares one underpass artwork path and borrows Drawing's terrain, sprite and immutable palette services.
The terrain service paints only terrain; it never adds a network beneath a highway bridge. Sorted ground callbacks
compose each underpass explicitly, while shared highway footprints do so in their existing four-cell ground order.
Terrain-only data maps and hidden network details therefore do not acquire underpasses as a side effect.

Surface, data and underground map painting share [map_traversal.c](../src/drawing/maps/map_traversal.c).
`PaintVisibleCityMapTiles` prepares its own sprite bounds and event anchors, derives tile ranges from the clip and
visits them in isometric order. It has no enable switch, fixed-size fallback scan or separate zoom-specific walkers.
`DrawCityMapTile` consumes the already resolved `TileIndex`; the surface API selects the zoom once for the pass.
Map passes restore the caller's scan clip. Animation capture uses that clip with an empty raster paint rectangle,
and no longer separately prepares the same event anchors before entering the shared traversal.
The old repaint-from-tile adapters and cumulative diagnostic counters were removed; frame statistics still count
tile visits and visible visits in the common traversal. Stock-facing per-tile and full-map adapters remain optional.
Data-map tiles also receive their zoom, `TileIndex` and unelevated screen origin directly from this traversal.
Intensity/service/growth artwork selection is separate from tile composition; no per-tile cursor or derived zoom
values remain in Drawing. The original 28-byte data-view record, including reserved words, lives entirely in
comparison world storage and is no longer synthesized from seven native fields.
The underground painter uses the same explicit tile/origin contract and binds its own palette before traversal.
It directly composes the baked underground artwork; the unreachable catalog-based fallback and its terrain table
have been removed. Camera geometry exposes the shared drawing/picking/selection profiles in `map_camera.h`,
including fractional presentation profiles reconstructed from selection pixels. The tile painter reuses the decoded
bed elevation for its baseline and skirts instead of projecting the tile and decoding its elevation again.
Map drawing contracts are declared in `map_render.h`, projection/picking/camera controls in `map_camera.h`, and
zoom/view data in `map_view.h`. The map-render header imports those narrow interfaces, not the Drawing umbrella
or the full City/UI state; desktop participation is an opaque pointer. `drawing.h` aggregates the map services for
existing UI callers without redeclaring them. Drawing state explicitly includes the RNG and options types it stores.
Placement selection allocation, sorting and retained cells live with UI input in `ui/tools/map_input.c`.
Drawing supplies marker bounds and damage through the UI-independent `drawing/maps/placement.h` and owns marker
rasterization. UI owns the error tile beside its window and requests old/new damage before updating it.
The unused polygon-preview
setter, rasterizer and desktop record have been removed, together with their obsolete ctypes entry. Active placement uses
the existing tile selection. Scoped preview-target binding still saves and restores the caller's borrowed surface.

Ground behavior and drawing live together in [city/maps/ground.c](../src/city/maps/ground.c): ground metadata,
rough/rock generation, recovery, paving and climate-dependent sprite selection. It also owns fallback ground details,
park selection order and the remains module's radioactive-waste artwork. The landscape scheduler retains its
visit order and invokes the family routines. [Shared terrain drawing services](../src/drawing/maps/map_terrain.h)
expose cliffs, skirts, placement overlays and the current ground pass without importing UI or Drawing state.

Power-line topology, tower selection, per-cell placement and drawing live together in
[city/infrastructure/power_lines.c](../src/city/infrastructure/power_lines.c). Shared line tracing owns admission,
charging and traversal order; utility-grid traversal remains in `city/infrastructure/utilities.c`.
Wire endpoints use fractional sprite coordinates through Drawing's normal clipping and depth collection,
without changing Drawing's internal offsets. Placement and edit refresh retain their respective neighbor orders.

Tree rules and drawing live together in [city/maps/trees.c](../src/city/maps/trees.c): species,
placement admission/pricing, tool execution, metadata initialization, generation, both growth algorithms,
and sprite selection/arrangement. Its [header](../src/city/maps/trees.h) exposes the family contract.
Shared replacement calls its metadata initializer before writing the new kind. The map traversal owns
visit order and calls shared ground recovery before tree growth at the existing cadence. The stock economy phase
calls stock tree growth after its waste-decay attempt, keeping the original CRT draw sequence.
Tree drawing reads City and uses Drawing's sprite services; clipping, depth collection, caches and
pixel writes remain in Drawing. It does not import UI or Drawing's internal state.

Water behavior and drawing live together in [city/maps/water.c](../src/city/maps/water.c): dry/wet kind
selection, canal admission and pricing, resolved water commands, editor water levels, channel carving and water
artwork/bank selection. Channel probes retain their CRT draw order; canal admission reads virtual event overlays
through `ReadOverlay`. Shared native geometry stays in `city/maps/terrain.c`; stock record conversion stays in the
codecs. Water drawing borrows the prepared geometry and uses Drawing's clipping, terrain faces and sprite services.

New native landscapes retain the terrain generator's forest locations but initialize each occupied
tree tile like OpenTTD's `PlaceTree`: 1–4 trees, randomized growing/grown/dying/dead stages, and a mix
of grass and rough ground beneath uncovered inland trees. Count comes from random bits 22–23;
the newest tree's stage comes from bits 16–18, clamped to dead (6), so dead has probability 2/8
and each other stage 1/8. Species follows the tile's climate. One independent landscape random word
supplies each tile's variation, and tile-kind statistics are updated with the new counts. Existing
loaded cities retain their tree counts and saved stages. This initial variety does not enable the
optional ongoing tree lifecycle; with it disabled, native stages remain at their generated values.

`AdvanceCityLandscape` uses OpenTTD's 27 ms landscape ticks and maximal LFSR ordering.
A normal-speed City phase represents 400 ms, so it executes 14 or 15 landscape ticks; the saved
City clock preserves the fractional schedule, with exactly 400 landscape ticks per 27 City phases.
Pausing stops advances, and City speed scales the rate. Each landscape tick visits `cell_count / 256`
tiles; tile zero is visited separately once per 256 ticks. No full-map pass or allocation occurs in one
landscape step. One-time import/generation initializes the arrays. The cursor, loop tick and independent
RNG belong to City and are saved.

Previously the loop ran once per City phase, slowing recovery by about fifteen times. Clear grass now
reaches its next appearance after approximately 55 seconds and fully recovers in approximately
166 seconds at normal speed, with a tile's initial visit offset accounting for small timing differences.

Clear ground advances after eight tile visits, using the three-bit zone/slope counter. Ground beneath trees
advances when `(11*x + 9*y + (tick >> 8)) & 7` is 7, without a stored per-tree recovery counter.
The advanced Infrastructure setting **OpenTTD tree birth and death** is opt-in per city, with the
original SimCity tree count/spreading simulation as the default. Newly planted trees always start at
the first growing stage and mature through three stages, matching OpenTTD's normal-play planting.
Enabling the option replaces the original tree update with OpenTTD's growing/grown/dying/dead sequence,
bounded eight-neighbour spreading and at most four trees per tile. Tree growth uses the coordinate
cycle with mask 15. Only the newest tree uses the changing
growth stage; the others render as grown. Desert trees do not spread and regenerate after dying.
Random planting also seeds empty land, using a map-size-scaled byte countdown and additional rainforest
attempts. Planted trees require unzoned, dry, fully recovered grass or rough ground; neighbouring trees
retain their species and require matching tile climates. These placement restrictions adapt OpenTTD to
SimCity's zones and per-tile climates. All lifecycle randomness uses the separate saved landscape RNG.
Changed tiles explicitly invalidate their old/new sprite extents without redrawing the whole map.

Disabling the option stops native death and seeding and restores the original tree update. Saplings
continue maturing without consuming random numbers; mature, dying and dead stages remain unchanged
by the native loop until the option is enabled again. Ground recovery remains active independently
of the tree setting.

Grass uses four appearances. Snow has four stored covered appearances plus bare ground.
Desert has half/full ground states. Landscaping updates stored coverage immediately in the editor;
the city landscape loop approaches snow coverage from the current height and permanent region.

Sources: [landscape traversal](../src/city/maps/state.c), [tree behavior and drawing](../src/city/maps/trees.c), OpenTTD
[landscape.cpp](https://github.com/OpenTTD/OpenTTD/blob/master/src/landscape.cpp),
[clear_cmd.cpp](https://github.com/OpenTTD/OpenTTD/blob/master/src/clear_cmd.cpp),
[tree_cmd.cpp](https://github.com/OpenTTD/OpenTTD/blob/master/src/tree_cmd.cpp).

## Saving and comparison

Native `.sc2k` saves write little-endian records directly into individually Zstandard-compressed
RIFF chunks. `TILE` contains the contiguous eight-byte row-major records; `HALF` and `QUAR` store
coarse statistics at their actual resolution. `META` contains fixed scalar settings and the 64-bit
fixes mask; `CLIM` stores the climate/snow lattice. Scratch data and remaining simulation fields
belong to `NRTS`. Native reload validates tile discriminators and topology directly, without a
stock projection, dense map planes or a second ground-map copy. See [native storage](sc2k-native.md)
for chunk and record layouts.

Stock SC2 ALTM words remain big-endian; stock map chunks retain their 128×128, 64×64 or 32×32
sizes. Stock XTER/XZON exports contain original terrain codes and zoning/footprint markers,
excluding native ground categories, species and recovery counters. Original tools can read
this projection but cannot simulate native landscape state. Optional stock extensions retain it:

- `XNAT`: the explicitly little-endian scalar block: treasury, landscape state, tree lifecycle,
  snow/climate settings, terrain protection, native-map marker, assets and fixes. Current writes
  use a 56-byte prefix. Reserved tool bytes are zero. Offsets match the block inside native META.
- `XGND`: raw row-major terrain, zone/slope, flags and kind planes, then little-endian altitude
  words, followed by `CLIM`, a `u32` control-byte count and the climate/snow lattice. This retains
  native bridge style/phase omitted by the stock ALTM projection. No BRDG trailer is needed.
  Validation checks native tile metadata, bridge/footprint topology and climate length, then checks
  the projection against stock base chunks. Invalid loads preserve partial writes and skip success
  rebuilding. Unknown trailing bytes survive archive-backed stock saves.

Stock map translation is centralized in `src/city/saveload/map_legacy.c`. Streaming and
archive-backed stock SC2 use the same full-record projection. XZON reconstructs stock corner
masks and XBIT reconstructs orientation. `TranslateCityTileToLegacy` is also used at comparison
boundaries. Native City records remain unchanged; parks and subway/rail transitions regain the
stock all-corners marker only in the exported record. Native `.sc2k` saves do not use this conversion.
Earlier project layouts need no migration.

Native resume fingerprints cover META and data chunks, including CLIM when present. Stock comparison
sets City fixes to zero and disables tree lifecycle and other simulation improvements. Its City
keeps native encoding: imported stock planes are collected into complete records before conversion,
and exports/checkpoints project them back through `TranslateCityTileToLegacy`. The optional bridge
audit packs its historical diagnostic records into a separate transport buffer without changing City.
Enabled gameplay improvements remain rejected by stock checkpoint admission.

## Landscape behavior and encoding

`CITY_FIX_NATIVE_LANDSCAPE` selects native artwork, terrain editing and landscape
simulation improvements. It is enabled by default in Advanced Options. Turning it
off selects the retained stock behavior while keeping the same native tile records.
`ReadTerrainCode` and `WriteTerrainCode` always access the underlying land slope;
writing a slope preserves water and upper-nibble metadata. Neither depends on the
fix. Water presence comes from the kind class and salinity from `TF_SALTWATER`; complete surface geometry comes
from `ReadCityTileGeometry`. Retained stock algorithms explicitly use
`ReadLegacyTerrainCode` and `WriteLegacyCityTerrainCode` from `city/saveload/map_format.h` for combined slope/bank/water
codes. Tile access and infrastructure headers do not import the stock format; retained algorithms include it explicitly.
Legacy writes are decoded before entering native storage. Highway terrain
preservation checks only `CITY_FIX_HIGHWAY_TERRAIN`, independently of landscape
artwork selection.

## Native power-line metadata

Native power lines use kinds 14/15 on dry ground and 16/17 over water, without/with a tower.
Terrain bits 4–7 store west/south/east/north connections as 1/2/4/8;
low-nibble zoning is preserved. `ReadTransportConnections` and
`WriteTransportConnections` access the mask, and `CityPowerLineHasTower` reads the
kind. No support type or additional map allocation is stored. TileInfo remains
eight bytes.

Kinds 16–17 represent lines across water. Their water class and terrain/water
height retain the underlying water; the stored mask supplies the span's axis.
Towers over water select the water-pylon artwork. Kind 92 belongs only to stock
water-power artwork and established simulation statistics, not native map storage.

Ground crossings use one kind per network pair: road/power 67, road/rail 69 and
rail/power 71. Terrain bit 4 selects the primary axis: road for road/power and
road/rail, rail for rail/power; zero means east/west and one north/south. The other
network is perpendicular. Crossings with power lines are cable-only. Highway
crossings keep their existing axis and footprint metadata. Native bridge underpasses also
remain cable-only and derive their connections from the bridge axis.

Construction and local edits compute connections from immediately adjacent
powerable tiles and compatible terrain axes. Straight runs place one tower every
four tiles in rotation-independent coordinates, leaving three cable-only cells;
endpoints, bends and junctions have towers. Water spans retain their straight mask
and use the same spacing. Lines adjoining cable-only network crossings also get
towers so replacing a periodic tower does not leave an oversized gap. Placement,
demolition, building capability changes and terrain edits refresh nearby masks and
both halves of affected wires. Category changes clear or initialize the upper terrain
bits explicitly.

Rendering reads the stored connection mask. Tower artwork follows the mask:
straight, end/corner or junction, with the existing heavy straight-tower variation
selected by tile position. The water kind class selects water pylons. Cable grades read
only adjoining endpoint heights and tower presence. No topology discovery or
search for the next tower occurs during painting.

Stock import collapses directional power kinds 14–28 into the appropriate dry or wet native kinds and
converts water-power kind 92 into the same pair while retaining its axis. A local
connection/tower rebuild runs after the complete map has been converted. Export
uses stored masks and terrain to select directional stock kinds, emits kind 92 for
water lines, and strips native connection bits. Native packet validation checks dry
masks against adjacent capability/terrain. Water spans retain their construction
axis; lines submerged by terrain edits retain their existing cardinal mask. Previous
project metadata encodings have no compatibility branch or new save version.

## Ground crossing axes

`IsTileKindGroundCrossing` uses the shared kind classification table. `ReadCityCrossingAxis`
and `WriteCityCrossingAxis` access terrain bit 4; `ReadCityCrossingConnections` returns
that primary network's cardinal mask. Cars, paving, wire drawing and sprite selection
read this explicit axis, with no odd/even assumptions about native kind IDs.
Low-nibble zoning stays independent, and footprint decoding returns zero for all
three ground crossing kinds.

Stock import converts 67/68, 69/70 and 71/72 to one native kind per pair and records
the orientation in zone. Export reconstructs the original directional kind and clears
the axis from stock XZON. Native statistics aggregate each pair into its one kind;
stock statistics are split by orientation during export. Native save packets reject
retired directional kinds and reserved metadata bits. No extra tile storage or map allocation
is required; earlier project encodings have no compatibility branch.

## Ordinary road and rail connections

Native ordinary roads use kind 17 and rails use kind 18. Terrain bits 4–7 store
west/south/east/north connections as 1/2/4/8; low-nibble zoning is independent.
`ReadTransportConnections` and `WriteTransportConnections` access this payload.
This connection encoding is unconditional, including during stock comparison.
Crossings, bridges, tunnels and subway access retain their kinds
and their existing metadata. No climate or orientation bits store ordinary connections.

Placement updates the mask explicitly, including when the stored kind stays the
same. The existing replacement operation normalizes ordinary directional writes
to the native base kind and initializes their connection payload. Category changes
clear the previous upper-terrain interpretation. Rotation moves the base kind unchanged
and rotates only the cardinal connection nibble.

Stock-file import converts every ordinary road and rail layout to its native base
kind and connection mask before enabling native bridge storage. Stock export
reconstructs the directional layouts and their individual tile counters from those
masks. Native saves retain the base kinds and masks directly.

`TranslateCityTileToLegacy` projects stored connections to stock layouts for rendering,
retained orientation-based algorithms, routes and comparison. Terrain supplies climbing
shapes. Stock projection reconstructs transition IDs from an adjoining incline; native
sprite selection uses the flat connection mask directly and does not scan neighbors for
those retired transitions. No rail transition kind or extra metadata is retained in native tiles.
Retained stock algorithms consume projected directional IDs; they do not change City encoding.

All retired power/road/rail layout constants live in `CityLegacyTileKind` with
`TKL_` names. They remain valid stock/artwork numbers and are absent
from the active surface-kind enum. The shared conversion worker owns stock/native
translation and supplies the connection and projection helpers.

### Conversion boundary

SC1 files and their ten-bit tile importer are unsupported. Only stock SC2 import/export
and the current native format are maintained. Failed SC2 loads do not probe another file format.

`src/city/saveload/map_legacy.c` owns stock SC2/native tile conversion,
terrain and water decoding, directional road/rail/power layouts, entrance variants,
hydro orientations and counters, building/highway footprint projection, bridge
packing and XGND validation. Native initialization and ground-extension encoding remain in
`map_native.c`. The shared `map_format.h` interface exposes stock IDs and conversion operations to explicit codec
and compatibility callers, without importing City or other owner definitions. Engine headers do not publish those
IDs. Highway span classification reads native geometry directly: flat land, shore, water or steep terrain, rather
than indexing a stock terrain table. The single
`City.native_map_storage` distinguishes unfinished stock file decoding from complete native tile
encoding. Both terrain generators build native records throughout: the original 128-square command
keeps its height, smoothing, carving and progress stages without entering stock storage or converting
the finished map. Water planes, salinity and slopes use native terrain geometry. Ocean propagation
marks only water, and channel carving stores the river bed and water level directly.
Gameplay, drawing, bridge metadata, rotation, events and growth assume native records. Format checks
remain in import/export codecs; stock maps are converted before loaded-state simulation rebuilding.
Terrain and bridge formats cannot be selected independently. Native geometry, map mutations and simulation
remain owned by City.

The diagnostic native command stream and its independent
`NativeStockVerifier(native_bridge_storage=True)` policy can explicitly compare packed bridge storage.
The retired alternate reference-library build is no longer used. Other tile families keep
the stock representation in this focused mode. Original piece/zone/approach state
is retained locally so open bascules and partially removed spans export exactly.
Stock has no climate plane; this mode uses a converter-private bridge-metadata marker to
disambiguate reclaimed IDs from unconverted directional road/rail IDs. These
payloads are separate from native tile storage. The converter stages
one bounded map copy at the diagnostic export boundary, never during drawing.

Each checkpoint compares the canonical stock planes and the raw packed records.
The expected packed records come independently from original executable state in
`sc2k_re/tile_format.py`, not from candidate bytes or the C converter. The focused
pointer-span checker accepts `--native-storage --lifecycle` for accept/cancel,
following ticks, both rotations and demolition. This does not admit all native
landscape/gameplay options, which still require their own oracle evidence.
