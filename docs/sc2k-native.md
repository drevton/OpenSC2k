# Native SC2K saves

Native `.sc2k` saves contain one RIFF/SC2K stream compressed as one Zstandard
frame. Every chunk shares that frame's compression history. Numeric values and
fixed records are little-endian; strings are UTF-8. This is the project's own
format, independent of stock `.sc2` import/export and resource data.

The numeric save-format version is **4**, defined by `CITY_SAVE_FORMAT_VERSION`
in `src/city/saveload/sc2k.h`. Bump it for a breaking layout or meaning change.
The loader checks it before applying City data. Earlier project formats need no
migration. The app release and Git revision identify the writer independently;
Load Game shows all three.

## Container

The decoded stream starts with `RIFF`, a little-endian `u32` length excluding
those first eight bytes, then `SC2K`. Each chunk contains a four-character ASCII
ID, a `u32` payload length, a `u32` CRC32 and the unchanged record bytes. Payload
length includes the CRC. An odd payload has one padding byte. Chunks are not
compressed individually; compression spans the complete RIFF stream.

The writer queues borrowed record references using the existing 24-byte entry
budget, computes the RIFF length, then streams one Zstandard frame at level 3
with a content size and checksum. It neither assembles a second full save image
nor seeks back into compressed output. Record sources stay immutable until the
synchronous writer finishes. The container admits at most 4,096 unique chunks,
512 MiB encoded and 768 MiB decoded, including RIFF headers and padding.

Full loading validates the frame, RIFF bounds, duplicate IDs and every chunk
CRC before applying City records. It then reads records forward through a small
decoder cursor, allocating only the current record. An earlier-offset request
restarts the decoder. The writer orders records to match their load dependencies.
No whole decoded-map buffer is retained or assembled. Saves with unknown chunks
retain the original compressed frame and only the unknown indexes; resaving
streams those chunks into the new compressor. With no unknown chunks, loaded
archive storage is released entirely.
Loaded indexes are sixteen-byte metadata/offset records. Queued write sources
keep the twenty-four-byte budget and their byte/archive source union inside the
writer implementation. Both expose the same name/CRC/size metadata; resume-state
fingerprints query that metadata without interpreting writer storage as loaded
indexes. These in-memory records are not serialized as structures.
The small retained extension descriptor also preserves the native save-format
choice. META bytes live only through loading and are released once their views
have been applied. The file operation's cleanup stack releases temporary archive
and metadata storage if an observed host read or seek throws.

Summary reads and terrain previews decode forward only through the requested
chunk and verify the consumed chunk CRCs. They do not validate the unread frame
trailer; complete-frame validation belongs to full loading. Terrain previews
stream `TILE` directly into their existing allocation without loading a City or
duplicating its map.

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
presence selects native saves. Format 4 retains these kinds in the compressed RIFF stream;
earlier formats are unsupported.
Raw record sizes and offsets are asserted at compilation, as is a little-endian host.
Stock conversion remains confined to stock import/export codecs.

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

The member length determines the name length: 1–256 bytes and at most 64 Unicode
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
there is no native `XNAT` member. Future fields append to the struct. Fixes retain
their stable `CityFix` bit indexes, including infrastructure rules; unsupported
bits reject the save. Cash, assets, date and fixes are not repeated in `NRTS`.

`VERS` starts with four `u32` values: major, minor, patch and release stage
(0 development, 1 alpha, 2 beta, 3 stable). Remaining bytes contain the writer's
`git describe --always --tags --long --dirty` text, 1–255 UTF-8 bytes without NUL.
No separate string length or magic is necessary. OpenSC2k 0.2 Alpha writes
`0, 2, 0, 1`. Resaving updates the writer metadata. Missing writer metadata displays
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

`NRTS` starts with a `u32` fingerprint binding it to `META`, required data members
and optional `CLIM`, followed by scalar values in the order declared by
[sc2k_fields.inc](../src/city/saveload/sc2k_fields.inc). The table's host offsets,
widths, counts and strides describe the selected values; this metadata, pointers
and structure padding are not encoded. The format version defines that layout.
Facility payloads, names, tile maps and history are stored only in their own members.

The scalar fields include cadence, calendar, RNGs, economy/population, ordinances,
weather, disasters, emergency units, geography, infrastructure, speed, newspaper,
completed construction work, monthly accounts, statistics, demographics, industry,
bonds, milestones, rankings, facility allocation rules and cheats. Derived neighbor
names are rebuilt. Next comes the contiguous `u16` scratch layer, then three `u32`
queue values (read cursor, write cursor and live length), and one four-byte
`PointU16` per live queue entry. The queue mask and duplicate-suppression bitset
are derived rather than saved. Consumed queue workspace is omitted.

The three queue values use unsigned ring arithmetic: live length is
`(write - read) & mask`, and point `i` restores at `(read + i) & mask`.
Loading checks both cursors against the derived capacity before restoring
points. It retains the existing write order: scratch first, duplicate marks
and points next, cursors after the point loop, then the final payload-size check.
A rejected payload can therefore retain those partial writes.

Malformed or missing resume state rejects the native save. Loading preserves the
existing partial-write/error behavior. Saving finishes pending simulation work;
active question continuations and borrowed command input cannot be saved.

Audio owns jukebox programmes, playlists, shuffle, volume, current track and playback
position. UI owns the music windows, collapse/pin flags, pointer feedback and title
animation, and borrows the displayed player. These are not City data. Preferences
store programme, playlists, shuffle, volume and the window flags.
Host paths, window layouts, cursor/tool selections, loaded artwork/fonts/audio and
render caches are not serialized.

## Command errors

City command execution returns a negative gameplay error on failure and a nonnegative
command value on success. `CITY_COMMAND_TEST` returns a nonnegative price or a negative
error without applying the action. Tool functions propagate the reason through the
command handler; they do not open command error windows.

`RunCityCommand` presents returned errors by default, including calls outside a mouse
event. Callbacks receive the same reason in `CityCommandResult.error`. `DoCityCommand`
is the execution/query boundary and does not open an error window. Blocker locations,
cancellation and informational notices are separate presentation metadata.
