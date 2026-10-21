# Native civilian road vehicles

Seven original Blender models: hatchback, sedan, station wagon, van, pickup, SUV
and sports coupe. `blender/vehicles.blend` is the editable model library;
`tools/render_road_vehicles_blender.py` reproduces it and all rendered sources.
Models and materials are original procedural work; no external vehicle meshes,
photographs or textures are used. The previous ImageGen paintings, prompts and
hashes remain in `sources/` as historical authoring material, and are not packed.

## Geometry and registration

All models use one orthographic camera at 30 degrees elevation, matching the
map's 2:1 ground projection. Their widths range from .175 to .190 of a tile
(less than 9% variation); lengths, roof heights and body shapes distinguish the
types. Each model has its own belt height, wheel radius, axle locations and roof
positions. The van uses a lower, narrower cabin above a deeper body; the coupe
has a small rear quarter window and a longer front door window. The pickup has
a recessed bed and an upright cab rear. Smooth longitudinal body sections form rounded noses, bonnets, shoulders
and rear quarters. Wheel wells are cut into the smooth body without pulling the side panels up, with rolled fender lips,
recessed radial tyres and smaller alloy wheels. Compound curved roofs and bowed
windscreens/side glass replace the former box cabins; thin seals, flush lamps
and wraparound bumpers keep the details in proportion. There is no per-frame
fitting or perspective adjustment.

Each model has 48 screen-space headings. Exact axes and road diagonals are
retained; intermediate frames halve the former angular gaps. Each heading has
straight, left and right front-wheel settings. The tyres pivot about their
own steering axes inside the fixed wheel wells. Sources are rendered at 256×256
and baked at 75% scale into a 64×64 canvas. The
world origin, at the centre of the wheel footprint, is always at **(32, 48)**.
The 48×48 downsample is placed at (8, 12), preserving this anchor. The source
baseline is 48 and scale divisor is 4. The packer removes transparent
canvas margins on a four-source-pixel grid and adjusts the existing baseline and
horizontal offset. This preserves the sampling grid and ground anchor at every
map scale, while keeping cached sprite extents close to the visible car. Runtime
positioning uses this shared anchor directly, including terrain/bridge height, rather than estimating a
heading-dependent offset from the silhouette.

Blender renders opaque paint and opaque tinted glass. Sprite alpha represents only
antialiased silhouette coverage. Coverage is downsampled with an area average,
without the faint halo of a Lanczos alpha filter. Coverage within 24/255 of
transparent or opaque is snapped to that endpoint; only the remaining mixed
edge pixels store partial alpha. Colour detail still uses Lanczos. Ordinary cars remain opaque; cars
are created outside the displayed area during normal traffic updates, and old
cars retire outside the displayed area. A newly revealed region is populated
before its first paint, including when the game is paused.

## Palettes and pack

The 1,008 frames share 32 body palettes. Indices 224–255 are body shading;
0–223 preserve glass, wheels, lights and trim. The palette bank is 32 KiB of
immutable Assets data. The named families in `PAINTS` remain balanced:

| Family | Count |
| --- | --- |
| Blue | 5 |
| Red | 5 |
| Green | 5 |
| Yellow/orange/gold | 5 |
| Earth | 5 |
| Neutral | 7 |

Twenty-five palettes use metallic highlight ramps. The runtime ships only the
packed sprites and palettes in `landscape.dat`; normal builds require neither
Blender nor the source models/renders.

Regenerate explicitly:

```sh
blender --background --threads 8 --python tools/render_road_vehicles_blender.py -- --samples 32
python3 tools/bake_road_vehicles.py
python3 tools/dev.py build
```

`blender/manifest.json` records dimensions, camera, anchor, model dimensions,
headings and SHA-256 hashes of the renders. The baker checks these registrations.
`preview.png` shows every body palette; `directions.png` shows all headings.
`blender/review.png` shows larger front, side and rear studio renders of all seven
models, using blue paint. These development views use a tighter camera; game
sprites retain the shared map camera and registration.

Reproduce the large visual review separately from the game sprites:

```sh
blender --background --threads 8 --python tools/render_road_vehicles_blender.py -- --review
python3 tools/bake_road_vehicles.py --review-directory /tmp/sc2k-road-vehicle-review
```

Custom screen headings and body pitch can be rendered without overwriting the
registered game sources:

```sh
blender --background --python tools/render_road_vehicles_blender.py -- --model coupe --heading 37 --pitch 8
```

Custom studies go to `/tmp/sc2k-road-vehicle-preview`, or a supplied `--output`
directory. Runtime uses the 48 flat headings and three steering settings with interpolated
road height; additional pitched sprite families can be generated from these models.

## Presentation state and traffic

`Drawing.road_traffic` owns 1,024 cars and 1,024 spatial hash buckets, about 18 KiB
at every map size. Each car remains 16 bytes, with a five-bit path ID and
two-bit rotation. Path zero marks an inactive slot; real paths are one-based.
Entry and exit are derived from that path; there is no separate
route tile, route side or future junction exit.
No cars occupy saved City event slots or change simulation RNG or traffic data.
Drawing has its own random stream. Cars submit sprites to the existing compact
drawing queue; the spatial hash remains available for movement and collisions.

Only large and magnified views retain cars, within exposed viewports plus an
eight-tile margin. Camera changes reuse existing snapshot history to seed newly
revealed strips according to traffic density. The pool is shared across views;
there is no map-wide visited array or additional camera-history buffer.
Ongoing arrivals originate in the margin, with collision and headway checks.

Paths follow reciprocal road connections, bridges and underpasses, using the
pavement's shared curve geometry. The front axle midpoint is the navigation
point and always belongs to the car's tile. At the boundary the car chooses and
caches its new tile's path, surface profile, base speed and straight lookahead.
Subsequent movement samples the table without resolving that tile again.
Road edits refresh these caches and remove cars whose route was deleted.
Each model's front and rear axle offsets include the 75% sprite scale. Body yaw
is retained at 1/65536-turn precision across tiles. Short integer midpoint steps
turn the body according to front-axle displacement and wheelbase, keeping rear
axle motion along the body rather than making it slide sideways around a curve.
Collision uses the resulting body centre. Sprite placement balances the remaining
raster-heading error around the midpoint between both axles, rather than making
either axle take the whole heading change. Selection compares projected axle
positions at full retained heading precision. Front-wheel artwork engages at
17.5 degrees of steering and releases at 12.5 degrees, preventing threshold
flicker. Steering occupies the car record's former padding byte.
Lane offsets start at 3/16 tile; smooth endpoint adjustments keep the complete
path inside its own tile while preserving the shared boundary tangents. Tight inner diagonal
bends whose offsets would fold use a cubic joining those same endpoints and tangents.
Drawing interpolates the baked Q8 coordinates at Q12 precision through camera projection.
Collision and surface heights derive their positions from the same path cursor;
the former cached X/Y bytes make room for retained yaw within each 16-byte car.
The 22 shared paths have 33 samples each, with Q8 coordinates and byte headings;
the immutable generated tables occupy 3,514 bytes. All runtime movement uses
32-bit integer arithmetic. The sports coupe uses cached three-tile lookahead
and nearby hashed cars to accelerate on clear straights. Oriented collision footprints include a bumper
gap and inspect neighbouring hash buckets, separating different elevation planes.

An approaching car reserves the junction, with other cars waiting outside it.
On entry it chooses its exit and checks sufficient downstream space to leave.
Cars do not spawn inside junctions or into another car's reserved exit space.
A waiting car may choose another clear exit; no artificial pause is inserted inside the junction.

Visual queues are bounded by the same density-to-lane population rate used for
seeding: density/256 cars per incoming lane, approaching one car per lane per
tile at maximum density. This is twice the former density/512 target. Collision
footprints retain the bumper gap; spawning adds only .15 tile of forward
clearance, rather than an additional .65 tile. Ongoing margin arrivals get up
to 48 attempts every .04 seconds to sustain the increased population.
Admission checks a 3×3 neighbourhood, keeping bridge levels separate.
After a three-second birth grace, a car checks that local population once per
second. This also corrects creeping queues whose cars never stop for long. If
it exceeds the density target, that car fades out over 0.6 seconds and retires.
Cars already departing are excluded from the count so other cars do not retire
for the same excess. Moving cars and queues within the
target remain opaque. Fade timing reuses the existing age field and one spare
flag bit; there is no new pool, timer array or map allocation.

Traffic freezes with the world. All cameras share the same pool and positions;
damage covers each car's old/new visible sprite bounds in framebuffer pixels.
Cache preparation trims the remaining transparent padding after scaling and adjusts
the existing anchors, preserving every visible pixel. Bounds use the same fractional
position rounding as drawing, so no map-pixel margin is needed. Ordinary map surfaces
finish before cars are drawn. Bridge decks sort at the back of their footprint and their
separate prepared front rails/trusses sort at the front. Cars on the deck lie between
these layers; cars on an underpass sort beneath the complete deck. The same ordering
covers bridge approaches, all camera rotations and both map view types.

Legacy animated-palette traffic remains suppressed on native roads and highways.
Moving cars use streets, bridges and road underpasses; highway carriageways do
not yet carry these moving vehicles.

## Motion inspection

After a Release build, run `python3 tools/capture_road_car_motion.py /tmp/sc2k-road-motion`.
The focused native fixture uses ordinary road networks, `AdvanceRoadCar`, real
sprite caches and the real car renderer at 30 FPS and magnified scale. It checks
both curve directions and an offset bend, all seven models and all four cameras,
including the following straight tile. Twelve MP4s show the sedan cases; CSV and
JSON measurements cover all 84 cases. The fixture loads no save, opens no window
and closes automatically. It requires ffmpeg and leaves the app bundle unchanged.
The measurements bound both raster axle-anchor errors and detect front-axle
boundary jumps or rear-axle sideways motion. Final integer pixel rounding remains
visible at high magnification; the measurements do not claim subpixel rendering.
