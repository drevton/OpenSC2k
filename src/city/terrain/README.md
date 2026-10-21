# Native city terrain generation

`GenerateTerrainMap` builds operation-local terrain for preview and accepted new
cities. The caller owns the returned arrays and applies them to City through the
existing command path. Power-of-two axes from 128 to 4096 are supported; output
is native y-major order. Generation reuses contiguous working buffers and adds no
persistent map planes or rendering calculations.

The top-level pipeline shapes and grades land, tiles carved water, adds ponds
and routed streams, settles water and banks, and copies the finished native map.
The fixed working descriptor lives on the stack; map-sized scratch storage keeps
its existing single backing allocation.

Noise, interpolation and feature geometry use signed Q16 integers. Trigonometry
uses immutable integer tables; square root and relief falloff also use integer
arithmetic. Coefficients are quantized once in source. New terrain can therefore
differ from the earlier floating-point generator for the same seed; saved maps
keep their existing terrain.

## Relief and buildable land

Base relief uses a continuous five-band noise field sampled every four tiles.
Smoothness selects the amplitude of each detail band. Fractional heights survive
interpolation; the Height slider controls the elevation range. A squared height
curve leaves broad low plains for city construction. At minimum Height, Rough
and Very rough can still add a few levels of local relief.

Regional Variety blends four height curves for plains, foothills, hills and
plateaus. Low, Medium and High use two, four and eight regions per 128 tiles on
each axis. At minimum Height it can add up to two levels of broad inland relief.
Carved water and its immediate flat banks are excluded from this uplift.
Height 0 with No regional variety and Very smooth leaves flat base terrain.
Required shores and explicitly selected landforms remain.

The old OpenSC2K integer midpoint subdivision, repeated height smoothing and
height-shift pipeline are removed. Land and river banks share one eight-neighbor
minimum-envelope grading routine with forward/reverse raster passes, followed
by the native supported-slope repair. The earlier cardinal-only envelope was
redundant: grading against eight-neighbor distance already includes every
cardinal constraint. There is no special recursive 128-square grading stack.

## Coast and archipelagos

Ordinary Coast follows each checked North/East/South/West edge, defaulting to East
with no checked edges. The integer random walk follows the retired generator's
coastline method: start at depth 15, choose a new target from 0 to 30 for each
tile, and move toward it without overshooting. Steps are normally one tile and
two tiles one quarter of the time. Each selected edge has an independent seeded
walk. Water shifts its average continuously by up to three tiles either way, with depth 15
at the midpoint; final ocean depth is always at least three tiles.
A three-tap binomial low-pass filter blends with the original profile at 0-80
percent strength. Independent seeded strengths at 32-tile intervals use integer
smoothstep interpolation, creating smooth stretches alongside jagged sections.
All ordinary coastline profile calculations use Q8 integers, with no sine table
or floating-point noise. The walk and smoothing stay tied to the city seed when
Water changes. Filtering reuses the existing queue. A one-level
coastal bed avoids flooding extra bank tiles during grading. A bounded smooth
maximum softens adjacent corners without erasing local detail. Coast does not add
large inlets or offshore islands; Water also controls scattered freshwater.

Random chooses a seeded subset of one to four eligible sides, including adjacent
sides. Checked sides restrict the eligible set; without checks all four are
eligible. Effective choices do not overwrite the player's saved checkboxes.
Independent broad profiles vary inlet position, depth and spacing. Profile depth
keeps eight fractional bits and receives three nine-sample box-filter passes;
independent tile-scale detail is added afterwards to retain small shoreline crags.

A single two-dimensional coastal height field produces Random's offshore land.
It is sampled every four tiles and low-pass filtered in both directions. A
histogram of offshore samples selects a threshold leaving roughly the highest
22 percent as island candidates. This avoids depending on an arbitrary fixed
noise cutoff, though final coverage changes during slope grading. A water channel
separates candidates from the mainland. Raised cores survive normal shore grading.
Island shapes and group positions follow coherent noise rather than separately
placed ellipses. The coordinate-warp field, regional island searches and placement
retry loops are removed. All field/grid/histogram storage reuses scratch and queue.
Feature wavelengths stay in tile units, so larger maps add features instead of
stretching individual islands. Bay, Peninsula, One island and Two islands are no
longer presets.

## City waterways

The established broad-river geometry is retained, including Normal, Forked,
Crossing, Rejoining, Meanders and Delta. The simple river path keeps its five- to
twenty-five-tile bed controlled by Water. Expanded layouts use connected reaches,
with tile-scale bed widths and a slowly increasing number of main systems: one at
128/256, two at 512, three at 1024, four at 2048/4096. Lake and landform geometry is
also retained. Separate water components are no longer discarded merely because
another body is larger or reaches an edge.

Streams are independent of broad waterways. No/Few/normal/Many correspond to
zero/two/four/eight slots per 128-square region, with at most 32 source retries.
A reverse reachability pass finds valid downhill routes to existing water;
random-cost A* searches build connected 32-step reaches. Searches use existing
buffers for queues, costs, heap positions, visited marks and outlet directions.
Each bounded search clears its own visited marks; there is no generation-stamp
counter or preliminary whole-map clearing pass.
Sources must be dry flat terrain; paths admit flat/simple downhill slopes and
must reach water. There is no new allocation or per-tile saved state.
Streams and scattered ponds are placed on existing ground. A temporary bit in
the existing generation flags keeps them distinct from carved river/lake beds
during grading; it is cleared before returning the native map.

Water without explicit features selects low terrain with a histogram, up to a
forty-percent target. With explicit features, Water also grows coherent scattered
freshwater ponds over up to twenty percent of eligible flat land, independently
of broad-river width and stream count. Coverage changes after grading and overlap.
After pond and stream placement, dry planar slopes between existing upper and lower
water are filled as waterfalls when both edges join a straight one-level descent.
Lateral neighbors can be dry banks or parallel waterfalls with the same direction and
upper/lower heights, allowing adjacent drops to grow into wider falls. Bent slopes,
flat lateral water junctions, uphill links and disconnected water patches are not forced
together. This also connects nearby terrace ponds when stream density is set to None.
It reuses existing flow marks, draws no
random numbers and preserves the ordinary bed/water normalization pipeline.

## Water elevations and climate

The internal construction datum is rebased before export: seawater is at four,
inland freshwater is at five or higher, and connected river mouths descend to the
sea. Land and water receive the same offset, preserving slopes and leaving four
levels below sea level for landscaping. Ordinary flat inland ground starts at
five; banks can meet the water at four. Generation reserves the offset before
grading and temporary five-bit encoding, so export never wraps a finished hill.
Existing cities retain their saved elevations.
Saltwater marks the main broad channel as seawater without adding another coast
or changing its carved width. A temporary channel flag is cleared before export.
Streams and separate lakes remain freshwater.
Saltwater channels export ordinary water/coastal-bank kinds, preserving their sloped
shores. The generation-only river identity does not select freshwater river geometry
for a saline channel or flatten its shore tiles.

Water normalization admits flat surfaces and single-level cardinal drops whose
shared edge corners match. Incompatible river bends and junctions are terraced
downward. Occasional seeded stretches cut one level from existing raised ground
within three tiles of carved freshwater, never below the water surface or the
flat land datum. Streams and scattered ponds do not seed these decorative cuts.
Other stretches and flat land stay unchanged. Nearby dry banks are then
graded against final water surfaces; no artificial hills are added around water.
Drawing uses finished cached terrain and elevation fields.

Broad rivers and selected lakes retain their carved beds. Level reaches of
smaller streams and scattered ponds keep their ground at the water surface,
without the extra one-level bed deepening applied to carved waterways.
Carved river/lake beds derive their submerged bank slopes from neighboring ground.
They remain at least one level below level freshwater, including river mouths at
sea level. Ground-level streams/ponds keep flat beds beside raised banks, and the
interiors of saltwater channels and the ocean also retain depth below their shores. The
final saddle repair leaves completed water beds unchanged. Downhill river
sections retain their normalized incline and waterfall/rapids appearance. This
changes newly generated terrain; existing maps and water placed during play retain
their current placement rules.

Forests now use coherent noise scores and a coverage threshold controlled by
Trees, replacing the old repeated random placement/density growth loops. Native
decoration assigns climate-specific species and age, rough ground and rock patches.
Desert coverage uses moisture; snow uses elevation relative to a low-frequency
snow-line field. Cached tile climate is rebuilt after landscaping. Mixed terrain
uses desert-style tropical grass around sandy regions, with the existing half-grass
transitions toward temperate and Arctic regions. At least three grass tiles separate
desert from snow-capable ground.

## Dialog and preview

Coast, River and Landform use persistent grouped dropdowns with radio sections and
optional modifiers. Coast includes edge checkboxes. River includes stream density;
Landform includes lake amount. Variety combines regional variety and smoothness.
Height, Water and Trees sliders remain visible. Percentages for Snow and Desert
are available below Terrain. Captions abbreviate selections and ellipsize overflow.
The preview distinguishes dark-blue seawater and lighter-blue freshwater, and
reports water/tree coverage and elevation range.

The returned native terrain byte stores the underlying land slope in bits 0–3 for every tile.
Water flags determine presence and salinity. Carved level water retains its separate height above
the bed; ground-level streams and ponds use matching ground and water heights. Sloped freshwater
follows the bed and stores its base water level, without a waterfall marker.
Generation uses existing temporary buffers while normalizing rivers, then discards its intermediate
combined codes before returning the map. Native freshwater banks are drawn from neighboring water flags.
The last retile pass reads unchanged water elevations directly, without a separate whole-map slope
cache pass. Elevations are rebased and encoded during the existing output copy. Dry-land salinity is
cleared when carved water is tiled, so no later ocean cleanup scan is needed. Shared-edge water
constraints use one corner-matching calculation in both river normalization and bank containment.
A generation-only flag distinguishes genuine water inclines from submerged bank slopes while
encoding elevations; it is cleared from the returned flags. Sloping a riverbed never lowers its water surface.

## Sources and retained code

The generator originated as a port from OpenSC2K commit
`c1b5c3c9a0adaa656ab197bf3028e578a41b72bc`. River/lake/landform geometry and native
water/slope conversion still retain portions of that implementation:

https://github.com/nicholas-ochoa/OpenSC2K/tree/c1b5c3c9a0adaa656ab197bf3028e578a41b72bc/native/core/sim/src/sim

OpenSC2K is MIT licensed; retain `LICENSE.OpenSC2K`. The shared OpenSimplex2S noise
implementation comes from FastNoiseLite; retain `LICENSE.FastNoiseLite`.

OpenTTD informs multi-scale relief, regional curves, coastal profiles and downhill
stream routing. This is a city-oriented adaptation, not identical map generation.
Retain `LICENSE.OpenTTD`. Relevant source:

https://github.com/OpenTTD/OpenTTD/blob/master/src/tgp.cpp

https://github.com/OpenTTD/OpenTTD/blob/master/src/landscape.cpp

https://github.com/OpenTTD/OpenTTD/blob/master/src/pathfinder/yapf/yapf_river_builder.cpp

The original 128-square terrain command retains its elevation, smoothing, carving and progress
stages, but now builds native City tiles throughout. It no longer switches to stock storage or
converts the finished map. Ocean seeds publish their water plane before terrain rebuilding;
salinity propagation visits water only, and channels store native bed slopes and water levels.
Shared generation/gameplay writers no longer select a stock storage path. Stock decoding remains
at file boundaries, including the existing pre-import loaded-state rebuild.
A successful build does not establish bit-for-bit generation parity or visual validation across seeds.

## Recovered draft

The complete previously uncommitted generator is preserved in commit `81d315b3`
on `codex/recover-terrain-generator-draft`. Branch `codex/repair-terrain-generator`
repairs its removed coast references, generator entry point and obsolete grading
stack and builds with the current owners. It retains older height, forest and
stream algorithms for reference. Normal generation retains the newer relief,
connected streams, scattered ponds and mixed-biome fixes; the recovered
sea-relative grading is integrated here with the four-level export offset
restored. Removed coast presets stay removed.
