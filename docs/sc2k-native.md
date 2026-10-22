# Native SC2K saves

Native `.sc2k` saves use a RIFF container of type `SC2K`, with one Zstandard frame
per chunk. Numeric values and fixed records are little-endian; strings are UTF-8.
This is the project's own format, independent of stock `.sc2` import/export.
There is no JSON, stock RLE, stock map projection or ZIP wrapper in native saves.

The numeric save-format version is **3**, defined by `CITY_SAVE_FORMAT_VERSION`
in `src/city/saveload/sc2k.h`. Bump it for a breaking layout or meaning change.
The loader checks it before applying City data. Earlier project formats need no
migration. The app release and Git revision identify the writer independently;
Load Game shows all three.

## Container

The file starts with `RIFF`, a little-endian `u32` file size minus eight, and `SC2K`.
Each chunk has a four-byte ID, `u32` payload size, `u32` decoded size, `u32` CRC32,
and a Zstandard frame. Payload size includes the decoded-size and CRC fields.
An odd payload has one padding byte. The writer uses compression level 3.
Unknown chunks retain their compressed bytes when resaving.

The container admits at most 4,096 unique chunks, 512 MiB encoded and 768 MiB
in total decoded data. Each decoded chunk must match its declared size and CRC.
Summary reads decode only bounded metadata. Terrain previews stream `TILE` into
the existing preview allocation without loading a City or duplicating its map.

## Chunks

| ID | Contents |
| --- | --- |
| `META` | Format version, dimensions, date, sign identity high-water mark, fixed scalar settings and city name |
| `VERS` | Writer release, release stage and Git revision |
| `TILE` | Contiguous native eight-byte `TileInfo` records |
| `HALF` | Native four-byte half-resolution statistics records |
| `QUAR` | Native four-byte quarter-resolution statistics records |
| `HIST` | Sixteen history series of 52 little-endian `u32` samples |
| `LABL` | The 256 native 32-byte name slots; collection-owned names are cleared here |
| `FACL` | Fixed facility records, one anchor tile per facility |
| `SIGN` | Fixed records for live signs |
| `EVNT` | Native 22-byte persistent event records |
| `CLIM` | Climate/snow lattice, present when native landscape state exists |
| `NRTS` | Remaining deterministic scalar fields, scratch layer and live traversal queue |

Maps use their actual rectangular dimensions and row-major tile indexes
(`y * width + x`). `TILE` is the only copy of altitude, kind, zone/slope, terrain,
overlay, flags and underground data. Coarse maps retain their actual resolution:
`HALF` has map area / 4 records and `QUAR` has map area / 16. No square padding,
transposition, expanded coarse planes or separate ground-map copy is needed.
Shoreline trees use kinds 50–53, keeping water presence in the kind class and preserving
bank geometry, ground cover, tree species/growth and one through four trees. Their
presence selects native saves. Format 3 adds these kinds; earlier formats are unsupported.
Raw record sizes and offsets are asserted at compilation, as is a little-endian host.
Stock conversion remains confined to stock codecs and comparison boundaries.

## Metadata

`META` has the following layout, followed immediately by the city name:

| Offset | Field |
| --- | --- |
| 0 | `u32` save-format version |
| 4, 8 | `u32` map width and height |
| 12 | `u32` next sign identity |
| 16 | `i32` starting year |
| 20 | `u32` calendar tick |
| 24 | 56-byte `CitySaveExtension` scalar block |
| 80 | UTF-8 city name, without a terminator |

The chunk length determines the name length: 1–256 bytes and at most 64 Unicode
scalars. Dimensions, UTF-8 and runtime name lengths/capacities/terminators are
validated. Scalar block offsets are relative to offset 24 above:

| Offset | Stored field |
| --- | --- |
| 0 | Signed 64-bit treasury |
| 8 | Four reserved zero bytes |
| 12, 16, 20 | Landscape cursor, tick and RNG state (`u32` each) |
| 24, 25 | Tree lifecycle setting and tick counter (`u8` each) |
| 26, 27, 28 | Snow line, variation (`u8`) and seed (`u32`) |
| 32, 33, 34 | Terrain climate (`u8`), reserved zero byte, desert limit (`u16`) |
| 36 | Protect terrain structures (`u8`) |
| 37, 38, 39 | Two reserved zero bytes and native-landscape marker (`u8` each) |
| 40 | Unsigned 64-bit asset value |
| 48 | Unsigned 64-bit simulation-fixes mask |

This block reuses the fixed scalar layout used by optional `XNAT` in stock files;
there is no native `XNAT` chunk. Future fields append to the struct. Fixes retain
their stable `CityFix` bit indexes, including infrastructure rules; unsupported
bits reject the save. Cash, assets, date and fixes are not repeated in `NRTS`.

`VERS` starts with four `u32` values: major, minor, patch and release stage
(0 development, 1 alpha, 2 beta, 3 stable). Remaining bytes contain the writer's
`git describe --always --tags --long --dirty` text, 1–255 UTF-8 bytes without NUL.
No separate string length or magic is necessary. OpenSC2k 0.0.1 Alpha writes
`0, 0, 1, 1`. Resaving updates the writer metadata. Missing writer metadata displays
as Unknown; it does not determine format compatibility.

## Collections and resume state

`FACL` is an array of 48-byte records. Array position is the facility slot:

| Offset | Field |
| --- | --- |
| 0 | Eight-byte `CityAnnualFacilityRecord` simulation payload |
| 8 | `TileIndex` anchor, or `INVALID_TILE` for an unassociated record |
| 12 | `u32` chronological age in years |
| 16 | 32-byte UTF-8 name including terminator |

Footprint size and links are already in tile data; rectangles, coordinate lists,
separate name/age tables and orphan extensions are unnecessary. Shared facility
associations resolve from building class. Hash buckets/links rebuild after loading.
The `individual_facilities` rule selects allocation behavior without changing
record layout. Reserved and unused slots retain their simulation/name data.

`SIGN` contains only live 40-byte records: `u32` stable token, `TileIndex` and a
32-byte UTF-8 name. The token preserves sparse identities; `META` retains the
high-water mark. Tile sign-presence bits must agree with these records. Names are
stored in their collection once; the corresponding `LABL` slots are zeroed.
Other names and inactive legacy label slots remain in `LABL`.

`EVNT` stores each runtime event's 22-byte persistent prefix, including reserved
slot zero. Chunk length determines event capacity (40–2,048). Unused slots are
zeroed and the derived hash-membership flag is cleared. Bucket heads and hash
links rebuild after loading; train-chain links remain saved. See [event storage](event-storage.md).

`NRTS` starts with a `u32` fingerprint binding it to `META`, required data chunks
and optional `CLIM`, followed by scalar values in the order declared by
[sc2k_fields.inc](../src/city/saveload/sc2k_fields.inc). Field IDs, widths, counts,
host offsets, strides, pointers and structure padding are not encoded. The format
version defines that layout. Facility payloads, names, tile maps and history are
stored only in their own chunks.

The scalar fields include cadence, calendar, RNGs, economy/population, ordinances,
weather, disasters, emergency units, geography, infrastructure, speed, newspaper,
completed construction work, monthly accounts, statistics, demographics, industry,
bonds, milestones, rankings, facility allocation rules and cheats. Derived neighbor
names are rebuilt. Next comes the contiguous `u16` scratch layer, then three `u32`
queue values (read cursor, write cursor and live length), and one four-byte
`PointU16` per live queue entry. The queue mask and duplicate-suppression bitset
are derived rather than saved. Consumed queue workspace is omitted.

Malformed or missing resume state rejects the native save. Loading preserves the
existing partial-write/error behavior. Saving finishes pending simulation work;
active question continuations and borrowed command input cannot be saved.

Audio owns jukebox programmes, playlists, shuffle, volume, current track and playback
position. UI owns the music windows, collapse/pin flags, pointer feedback and title
animation, and borrows the displayed player. These are not City data. Preferences
store programme, playlists, shuffle, volume and the window flags.
Host paths, window layouts, cursor/tool selections, loaded artwork/fonts/audio and
render caches are not serialized.

## Historical implementation notes

The measurements below describe the earlier SC2X implementation at their stated
commits. They are retained as historical evidence, not verification of the new
SC2K wire format.

## Map-bound audit, 2026-10-10

The source audit covered decimal 126/127/128, hexadecimal equivalents, stock map
constants, coordinate masks and random helpers. It corrected seven-bit disaster
and helicopter destinations, stadium script admission, and a flood search that
used width for both axes. Native and comparison targets compile after these fixes;
this audit did not run simulation or cross-application save tests.

Remaining fixed-size map-related paths are intentional:

- Terrain features start on a 128-square landform, then scale to the City's axes.
- Map-data thumbnails sample the full City into a 128-square presentation image.
- Original painter/picker fallback paths use stock dimensions. Expanded maps always
  take the dimension-aware, clip-bounded renderer and native picker instead. The
  two literal 126 position offsets in `map_data.c` belong to this stock fallback.
- Stock SC2 chunks and comparison records retain their original fixed dimensions.
- Transport's 128-entry arrays hold route depth, not map coordinates. Its native
  caller supplies a maximum cost of 100, and every step costs at least one.

Other occurrences include palette indices, glyph data, tile/command IDs, bounded
sprite scratch, probability masks and UI pixel positions; these are not map bounds.

## Whole-map performance, 2026-10-10

Unspecified single-configuration builds now default to RelWithDebInfo instead of
unoptimized code. Explicit build configurations remain supported. The optional
headless benchmark uses synthetic cities and deletes its private temporary save:

```
cmake --build build --target benchmark_large_map sc2k sc2k_comparison -j8
build/benchmark_large_map 4096 4096 3 all
build/benchmark_large_map 4096 128 1 fixtures
```

The second optimization batch improves contiguous source discovery, empty overlay
scanning, tile histograms and blocked row/column transposes. Water sources are
sorted into the existing rotated visitation order using the map's rotation
workspace; sorting is capped at 8192 sources, with source-dense maps falling back
before traversal starts. The
original 128-square water scan and both power traversal passes remain intact.
No RNG calls, simulation phases or active disaster cells are skipped. SC2X keeps
its compression level and encoding, including partial writes on invalid overlays.

Paired timings on this machine against b670f41e, with both versions optimized,
for 4096-square (absolute times vary with host load):

| Synthetic operation | Before (ms) | After (ms) |
| --- | ---: | ---: |
| Terrain generation, seed 12345, ocean and river | 734 | 571 |
| Tile recount, mostly clear terrain | 28.4 | 7.4 |
| Power source scan, no sources | 12.4 | 4.4 |
| Water source scan, no sources | 41.8 | 10.5 |
| Empty transient overlay scan | 12.3 | 0.6 |
| Fully connected power-line map | 210 | 208 |
| SC2X save after the connected-power scan | 1610 | 1433 |
| SC2X load of that save | 707 | 293 |

Focused validation compared 57 state checksums before/after across 128-square,
256-square, 4096-by-128, 128-by-4096 and 4096-square. These cover all water rotations,
the dense-source fallback, mixed tile counts, environment/decay, and repeated
sparse fire/flood scans, including RNG and infrastructure fields. Terrain, map
layers and complete compressed archives also match before/after in the four
all-operation workloads (all sizes above except 256-square). Save/load preserves
their map bytes and exact dimensions. The signed flood-counter boundary check
also matched simulation memory and return values for 48 stock-executable scans.
Another 14 stock-executable power/decay cases matched memory exactly, covering
fixed source kinds, an edge source, connected sources, the power ordinance, and
zero/maximum/gradient decay layers.
These are bounded checks, not proof of every simulation branch or populated city.
After rebasing onto the concurrent terrain corrections in 16f7e07b, the complete
128-square and 4096-square workloads again matched that updated baseline's terrain,
map and archive checksums. The bounded-sort fallback also matched its prior fixture.

OpenSC2K runs an isolated simulation tick on a worker and grants approximately
4–20 ms of work per frame. Checkpoints pause that worker on its existing call
stack; only a completed tick is published. See its
[frame runner](https://github.com/nicholas-ochoa/OpenSC2K/blob/main/game/src/simulation/core/frame_simulation_runner.gd)
and [budget checkpoints](https://github.com/nicholas-ochoa/OpenSC2K/blob/main/native/core/sim/src/sim/budget.rs).
The original native C mode remains synchronous. The opt-in frame batching below
addresses dense traversal without replacing the pathfinding algorithm. Original-game parity is checked at 128-square;
expanded maps have no original-game oracle.

Format reference: https://github.com/nicholas-ochoa/OpenSC2K/blob/main/docs/sc2x-format.md

## Populated phase benchmarks, 2026-10-10

The `phases` mode resets a synthetic city before each repeat and cycles all four
rotations. It measures the original sixteen growth subphases separately, mixed
built terrain with police/fire coverage and ordinances, connected pipe/power
networks, and fire/flood/riot overlays on one cell in sixteen. Growth includes
both mixed zones with destinations and residential-only roads without compatible
destinations. Sprite atlases and facility data are loaded before timing so active
disaster branches can use real sprite bounds. No user city is opened or saved.
Setup, state hashing and resource loading are outside the timed intervals; no
SDL window or compositor is active. These are simulation timings, not frame rates.

```
cmake --build build --target benchmark_large_map sc2k sc2k_comparison -j8
build/benchmark_large_map 4096 4096 4 phases
build/benchmark_large_map 4096 4096 8 phases environment
build/benchmark_large_map 4096 128 4 phases
```

A final 4096-square run gave these mean times on this host (RelWithDebInfo, four
repeats). Environment is taken from the more stable isolated paired run below.
Host load noticeably affects absolute timings.

| Workload | Total (ms) | Slowest growth subphase (ms) |
| --- | ---: | ---: |
| Growth, mixed zones, all sixteen subphases | 953 | 82 |
| Growth, no compatible destination, all sixteen subphases | 4871 | 413 |
| Environment, developed terrain and services | 76 | — |
| Water, connected pipes and stored tower supply | 231 | — |
| Power, connected lines | 241 | — |
| Fire, active on one cell in sixteen | 26 | — |
| Flood, active on one cell in sixteen | 12 | — |
| Riots, active on one cell in sixteen | 10 | — |

Environment now caches dimensions, uses direct indices in bounded full-map
loops, and accumulates four adjacent tiles before storing their shared scratch
cells. Tile processing and coarse visited-flag writes keep their original order,
including aliasing with flags read later in the same scan. The grouped additions
preserve 16-bit scratch wrapping. Traffic searches initialize only their root
cost instead of clearing all five path arrays; every pushed depth initializes
its entries before any read. Root-cost comparison replay is retained.
Neither change adds allocations or changes RNG calls, phase order, traversal
rules, damage notifications or resource assignment.

For environment, alternating before/after/after/before runs with eight repeats
per run measured 106.2/75.7/76.5/107.2 ms: a mean reduction from 106.7 to 76.1 ms
(29%). Growth gains were small relative to host variability, so no reliable
percentage improvement is claimed. Connected-network traversals and active
disaster algorithms remain unchanged. Failed traffic searches are still the
largest observed synchronous cost; these optimizations do not provide a frame
budget for very large developed maps.

Before/after canonical state hashes matched at every measured subphase: 912
checks across 128-square, 256-square, 1024-square, 4096-square, 4096-by-128 and
128-by-4096, all four rotations. The final dimension/index changes additionally
matched all 24 environment checks and all isolated paired checks. Hashes include
map/scratch/history bytes, explicit SC2X resume fields, pending queue contents,
queue cursors and return outcomes, excluding pointers, padding and consumed
queue workspace. This is bounded regression coverage, not an expanded-map oracle.
Focused stock checks passed for 28 traffic-route cases and 30 power/water cases,
including station transitions, failed routes, map edges, all water rotations
and the original queue cursor wrapping on a 32-square network.

An existing environment comparison gap remains visible: empty and low-tile
fixtures pass, while built/facility fixtures and six random seeds fail against
the stock executable. The unmodified baseline has the same reported failures;
they are not counted as passing parity coverage. Resolving whether these are
simulation or adapter defects remains separate from the measured optimizations.

A further traffic cleanup removes the duplicate access probe before a powered
building's route search, caches immutable layer pointers and computes each
neighbour index once. A 4096² run (four rotations) retained all 128 growth-phase
state hashes, and all 28 focused stock route probes passed. Mixed growth changed
from 1185 to 1146 ms for all sixteen phases; the no-destination workload remained
within timing noise (5874 to 5923 ms). This reduces redundant work but does not
solve the cost of failed randomized searches.

## Simulation work across frames, 2026-10-10

Enable **Spread simulation across frames** in Advanced settings for an expanded
city. It defaults off and belongs to the saved city. A 128-square city continues
to use the synchronous original phase dispatch even when the option is enabled.

The host prepares and presents the current frame first, measures its CPU input,
control and drawing work separately from the SDL vsync wait, then spends the
remaining display interval on simulation. The display refresh rate supplies the
interval, falling back to 60 Hz. It delivers several batches when time permits,
rather than limiting a frame to one batch. Normal/fast speeds stop after the due
phase completes; unlimited speed can begin subsequent phases within the same
budget. Ordinary idle presentation is deferred during this host delivery, so batches do
not each trigger another redraw/vsync. At 60 Hz the total CPU budget is 16.67 ms
before subtracting input and drawing work. The deadline is soft: one batch always runs when work is due, and a
batch already running finishes even if it overruns. Pending work reduces the
next event wait instead of adding another fixed 16 ms delay.

City retains the scan position, environment subpass/accumulators and utility
component accounting. Existing map scratch and the existing route queue are
reused; no map-sized allocation is added. Order-sensitive batches retain linear
scan order and the existing global RNG sequence. They do not introduce independent section
streams or skip failed searches. Growth yields between tiles, utilities between
queue entries/source scans, environment between rows/subpasses, and decay between
sections. One delivery covers up to 262,144 map/queue operations, 16,384
growth scan positions, or 1,048,576 environment input-tile equivalents. These are
work quotas, not a guarantee of elapsed time; a single traffic search runs to
completion. The calendar advances once when a phase starts and remains fixed
until it finishes. Event movement and pending disasters enter between phases,
while palette animation and UI navigation continue.

Commands that need the shared scratch/queue finish pending work before executing.
This includes construction, rotation and saving, so such an input may still incur
a synchronous pause. A save settles the phase and writes its completed resume
state, rather than serializing a half-finished algorithm. A reentrant save that
cannot settle a running phase is rejected. The host batching preference belongs to
Game and is stored as `game.spread_simulation` in the local configuration. It is
not serialized in city archives and does not select a save format. Loading a city
preserves the current local scheduling policy. Completed deterministic City state
is saved independently of how the host delivered its simulation work.

```
build/benchmark_large_map 4096 4096 4 batches
build/benchmark_large_map 4096 4096 1 batch-controls
```

Completed batch results matched synchronous map/history, explicit resume fields,
RNG, queue state and outcomes in all four rotations at 256-square, 4096-square,
4096-by-128 and 128-by-4096. The 4096-square synthetic maximum batch measurements
were 7.54 ms for failed-search growth, 1.49 ms for mixed growth, 2.53 ms for
environment, 3.63 ms for water and 3.94 ms for power. These exclude input/drawing
and are bounded fixture measurements, not worst-case guarantees. Host delivery
checks additionally cover suppressed per-batch idle redraws, unlimited speed
starting multiple phases, an expired deadline making progress, several batches
per frame, pause/resume, held calendar, a command barrier and private temporary
SC2X saves with the option on/off, including exact resume-state round trips.

The original synchronous route and utility paths still pass the focused 28 and
30 stock-executable probes. Per-action stock comparison explicitly rejects the
new mode, because partial phase state has no equivalent stock checkpoint. The
previously recorded environment parity gap and unproved reentrant branches remain
open; synchronous/batched agreement does not close those gaps. Fire, flood,
riot scans and other simulation phases are not made incremental by this change.

The growth scan now processes fixed 128-column strips: 32 sampled positions in
each quarter-grid row. Every supported axis is a power of two from 128 through
4096, so strips need no partial-width tail. This preserves the original row and
tile order. Power/water traversals capture layer pointers and map dimensions per
delivery; growth and environment indexing also use a captured row width. A
restrict-qualified byte-store helper alone cannot establish that invariant for
the caller, and helpers accessing the same layer through City prevent a broader
restrict promise.

A paired optimized (`-O2`) 4096-square run over four rotations retained all 140
growth/environment/power/water state hashes. Mean power phase time changed from
240.8 to 196.4 ms, water from 230.4 to 206.8 ms, all sixteen mixed growth phases
from 1053.6 to 793.0 ms, failed-search growth from 6336.4 to 4732.4 ms, and
environment from 112.3 to 107.8 ms. These are individual paired measurements,
not worst-case or statistically stable speedup claims. Synchronous/batched
results also agree for 128-square and both 4096-by-128 orientations. The 28
stock route probes, 30 stock utility probes and host batch controls still pass;
the comparison gaps described above remain open.

Section operations are separate from scheduling: power/water flag clearing and
decay have section kernels, while growth and each environment subpass have
section-row kernels. Budget/cursor handling calls those kernels without embedding
their tile operations. Independent clearing and decay process whole 128-by-128
map sections, up to sixteen per delivery. Decay therefore visits 65,536 coarse
entries per delivery. Clearing the entire map still precedes network traversal;
connected networks are not restricted to section boundaries.

Full-resolution section rows use fixed 128-iteration loops. Half-resolution rows
use 64 entries and quarter-resolution/growth sampling uses 32. Order-sensitive
environment passes traverse section rows left to right across the full map before
advancing down, preserving scratch/flag alias effects and RNG ordering. Array-only
row offsets use size_t; signed coordinates retain int. The isolated clearing and
decay kernels can safely use restricted layer pointers without helper calls
accessing those same bytes through City. Queue traversals and bounded neighbour
searches retain their algorithm-dependent iteration counts.

After extracting these kernels, synchronous and batched results still agree in
all four rotations at 128-square, 4096-square and both 4096-by-128 orientations.
All 128 expanded growth hashes and the four environment hashes match the prior
implementation, and all 58 focused stock route/utility probes pass. Host controls
also pass pause/resume, phase barriers, unlimited delivery and private temporary
save/load round trips. One optimized 4096-square run measured maximum batches of
1.97 ms for power, 1.95 ms for water, 2.07 ms for environment, 0.023 ms for decay
and 10.01 ms for failed-search growth. These remain fixture measurements under
variable host load, not hard frame-time bounds or proof of the open rare branches.

## Command errors

City command execution returns a negative gameplay error on failure and a nonnegative
command value on success. `CITY_COMMAND_TEST` returns a nonnegative price or a negative
error without applying the action. Tool functions propagate the reason through the
command handler; they do not open command error windows.

`RunCityCommand` presents returned errors by default, including calls outside a mouse
event. Callbacks receive the same reason in `CityCommandResult.error`. `DoCityCommand`
is the execution/query boundary and does not open an error window. Blocker locations,
cancellation and informational notices are separate presentation metadata.
