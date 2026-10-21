# City map event storage

`CityMapEvent` occupies 24 bytes. Its common header owns the fields used across
object types; a union exposes each object's payload with meaningful names.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 1 | Type |
| 1 | 1 | Direction or animation stage |
| 2 | 2 | State or linked train slot |
| 4 | 1 | Age or height |
| 5 | 1 | Target storage flag and runtime hash membership flag |
| 6 | 2 | Hidden overlay/sign/facility index |
| 8 | 4 | Current `TileIndex` |
| 12 | 10 | Object payload |
| 22 | 2 | Runtime hash next link; omitted from saved records |

`CityMapEventMotion` defines the shared sub-tile offset pair once, as two `u8`
values, each followed by an explicitly reserved zero byte. Ships instead use
the compact payload below. One legacy unit is
16 native units. Normal movement carries to the next tile at 256, ships at 192
(their original 12-unit period); negative movement borrows from the previous
tile. Addition uses signed `int` intermediates before storing the normalized
byte. Aircraft, monsters and other moving objects reuse the shared layout. The train/subway family uses
the same space for a u32 next-tile index and its animation frame. Aircraft
payloads name destination coordinates and crash flags; monsters name poses and
actions; explosions name their disaster trigger; Maxis Man names its destination
and goal. The raw codec view covers reserved and otherwise unused legacy words.
It does not add a second copy of the payload. Every byte of the record is covered
by explicit fields, including the runtime hash link.

Ships use all ten payload bytes, with adjacent fields and no packed upper bits:

| Offset | Bytes | Ship field |
| --- | --- | --- |
| 12 | 4 | Home `TileIndex` |
| 16 | 1 | Y sub-tile offset |
| 17 | 1 | X sub-tile offset |
| 18 | 2 | Destination Y |
| 20 | 2 | Destination X |

The home is the entry tile recorded when spawning. Departure uses that ship's
home, falling back to its current tile if the home is `INVALID_TILE`. Movement,
interpolation, resizing and stock translation use the ship-specific layout.
Native saves retain the home in the same 22-byte persistent prefix and reject
out-of-map home indexes. Stock SC2 files omit the process's shared ship-home
coordinate, so importing them initializes each home from that ship's current
tile. Comparison imports use the live stock shared home. Stock export omits
the per-ship home and zeros the unused ship goal byte. Conversion to an explosion
clears the ship payload before retaining its movement offsets.

Current positions are always tile indexes. Stock SC2 coordinate
imports clamp invalid current positions and train next-tile coordinates to the
map bounds. This is defined malformed-input handling, not a claim of unchanged
stock behavior for invalid coordinates. Airplane destination waypoints can be
off-map and are not clamped. Sub-tile offsets are not map coordinates.

The target storage flag survives type changes. Every released event slot is
unlinked and entirely cleared with `memset`; callers capture any needed state
before release and do not continue updating the freed record. Fresh slots,
including bulldozer slot zero, are cleared before initialization. Map resizing
reindexes stored tiles for the new row stride, clamping when shrinking.

Stock SC2 imports apply `max(legacy_offset - 1, 0)` once before
multiplying by 16. This moves positive offsets back by one legacy unit and can
change tile-crossing timing. Train/subway targets are tile coordinates and are
excluded. Invalid legacy offsets above 16 are capped to the representable range.
Comparison state conversion scales units without repeating the subtraction.
Stock exports divide offsets by 16 (discarding any finer native precision).
Native SC2K reload restores the 22 persistent bytes into zeroed 24-byte runtime
records; repeated native save/load does not shift events or repeat the legacy
offset adjustment.

Stock XTHG and comparison transport remain 40 records of 12 bytes. Shared codecs
translate complete records explicitly. Native `XTHG.bin` stores consecutive
little-endian 22-byte records, including reserved slot zero; its byte length
determines the saved capacity. C assertions freeze the layout. Native saves clear
the hash membership bit and omit the next word, then rebuild both bucket heads
and links after loading. The train-chain `next_slot` is separate and is saved.
Native records are not duplicated in NRTS. Invalid native tile indexes, train
links and Maxis Man event targets are rejected. Previous project save layouts
are not migrated.

## Pool and tile index

The event pool shares the City's map backing allocation. Default capacities
follow map-area thresholds, including the tool slot:

| Maximum area | Slots | Record bytes |
| --- | --- | --- |
| 128 × 128 | 128 | 3,072 |
| 256 × 256 | 256 | 6,144 |
| 1,024 × 1,024 | 512 | 12,288 |
| 2,048 × 2,048 | 1,024 | 24,576 |
| 4,096 × 4,096 | 2,048 | 49,152 |

A larger imported pool is retained after shrinking a map. The runtime Advanced
Option **Larger moving-object pool** enables allocating beyond the stock 40
slots; comparison disables it. Existing high slots continue updating when the
option is turned off, subject to stock sailboat trimming when disabled.
The shared stock ship-home pair remains comparison transport; native ships own
their home tile individually. The option also raises ordinary vehicle caps to
roughly twice OpenSC2K's map profiles:

| Maximum area | Airplanes | Helicopters | Ships | Sailboats | Trains | Entries at caps |
| --- | --- | --- | --- | --- | --- | --- |
| 128 × 128 | 4 | 2 | 2 | 8 | 15 | 61 |
| 256 × 256 | 8 | 4 | 4 | 16 | 32 | 128 |
| 512 × 512 | 16 | 8 | 8 | 32 | 64 | 256 |
| 1,024 × 1,024 | 32 | 16 | 16 | 64 | 106 | 446 |
| 2,048 × 2,048 | 64 | 32 | 32 | 64 | 255 | 957 |
| 4,096 × 4,096 | 128 | 64 | 64 | 128 | 511 | 1,917 |

Rectangular maps select profiles by area. Train caps count surface and subway
engines together, with three records per train. The desired train cap is reduced
when necessary so all caps fit the ordinary-vehicle budget. This budget reserves
slot zero and `max(65, allocation_limit / 16)` other entries: enough for all
30 police, 30 fire and 5 military units, with additional headroom on the largest
maps. New ordinary vehicles also check the current active records against this
budget, so other events cannot consume the reserve indirectly through a new
vehicle allocation. Admission scans the existing pool without allocating state.
Each sailboat in a batch checks independently. Expanded mode retains existing
boats above a reduced profile cap; the cap limits new admission.

Disabling the option restores stock admission caps: 2 airplanes, 1 helicopter,
1 ship, 4 sailboats and 5 trains, along with the stock 40-slot allocation bound
and sailboat trimming. Comparison uses this disabled mode.

OpenSC2K's SC2X map profiles use these separate vehicle caps:

| Map edge | Airplanes | Helicopters | Ships | Sailboats | Trains |
| --- | --- | --- | --- | --- | --- |
| 128 | 2 | 1 | 1 | 4 | 8 |
| 256 | 4 | 2 | 2 | 8 | 16 |
| 512 | 8 | 4 | 4 | 16 | 32 |
| 1,024 | 16 | 8 | 8 | 32 | 64 |
| 2,048 | 32 | 16 | 16 | 32 | 128 |
| 4,096 | 64 | 32 | 32 | 64 | 256 |

Its train cap counts surface and subway engines together; each engine also
requires two car records. Every allocation must fit the profile's total pool
budget. Other working formats use area-scaled legacy caps instead.
Sources: [OpenSC2K profiles](https://github.com/nicholas-ochoa/OpenSC2K/blob/main/native/core/sim/src/formats/sc2x/limits.rs),
[spawner](https://github.com/nicholas-ochoa/OpenSC2K/blob/main/native/core/sim/src/sim/moving/spawner.rs).
OpenSC2K likewise retains a separate home per ship in four spare high-plane bytes:
[ship-home encoding](https://github.com/nicholas-ochoa/OpenSC2K/blob/main/native/core/sim/src/sim/things.rs).

Incoming airplanes choose the full border range of the relevant map axis,
with the original low margin of 10 and high margin of 18 tiles. This preserves
the stock 10..109 range and RNG calls on a 128-square map, while supporting
large rectangular maps.

The index uses 4,096 `u16` bucket heads (8 KiB) and each record's final `u16`
word. Links encode slot + 1, so zero means empty and slot zero can be indexed.
A 32-bit avalanche mixer hashes the authoritative `event->tile`. Bucket queries
filter exact tile equality to handle collisions. Lists remain ordered by slot
ID. Reload walks slots backwards and pushes each at the bucket head, producing
the same order in linear time. Simulation updates
also run in slot order. No second occupied-tile array is stored.

Native tile overlay bytes retain signs, facilities and disaster markers beneath
vehicles. Existing combined-overlay queries derive a virtual event ID from the
hash; extended IDs skip the stock marker range. Stock import resolves hidden
overlay chains once, then indexes each active record at its own tile. Stock
export reconstructs hidden-overlay chains in the same stable order and projects
event IDs back into XTXT; cities with active slots beyond 39 or more than one
ship use native saves. Stock export cannot silently truncate active high slots;
an explicit stock export omits the per-ship homes.

Native rendering draws terrain and ordinary roads first, then sorts whole
buildings, elevated structures, decorative cars and overlay groups by one scalar
camera depth. An overlay group keeps its existing animation/RNG scope and stable
slot order; moving vehicles contribute their interpolated position. Aircraft
shadows stay in the ground pass and signs draw after the sorted objects.

Drawing retains one 16,384-entry workspace shared across clips and map windows:
12 bytes per prepared sprite and a two-byte sorting index, 224 KiB total at every
map size. Records contain a u16 sprite ID and quarter-tile depth, clip-relative
i16 pixel coordinates, palette ID, draw flags and opacity. Asset selection,
projection and car paths are prepared once; replay only resolves cached sprites
and immutable palettes and blits. Ground retains its existing first pass.

The specialized in-place MSD radix sorter orders indices by depth, then original
submission index. Equal depths therefore preserve insertion order, including
construction pieces and smoke. Tiny buckets use direct-key insertion sorting;
there are no comparison callbacks or second full-size sorting buffers.

Before collecting, rectangles whose physical pixel area times 2^(2-zoom) exceeds
1,000,000 are bisected along the longer side. Each half reuses the same workspace
and clips rasterization to its own pixels. Actual capacity exhaustion also
subdivides before painting. An indivisible clip exceeding capacity reports a
rendering error rather than dropping sprites. No pixel-depth buffer is stored,
and cars never trigger foreground tile replay or rebuild their spatial hash
while drawing.

Existing damage footprints, interpolation histories and event-anchor lists grow
to the pool capacity and are reused; hash-link changes alone do not invalidate
sprite pixels. Additional views retain independent damage footprints. The scalar
order uses footprint centres and supporting elevation, not bitmap height. It is
an approximation for large buildings and overlapping bridge parts; general 3D
bounding-box ordering remains separate work.
