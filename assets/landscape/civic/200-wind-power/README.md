# Wind power plant (200)

Four genuine quarter-turn views of one fixed lattice windmill, illustrated with the built-in Imagegen tool.
The Blender model fixes the fan plane, tail, tower and site. Painted sources and prompts are retained.

Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/200-wind-power`.
Each final sprite is 128×320 with baseline 320 and an exact native 1×1 ground mask.
The original painted grass, concrete pad and shadows are retained. The shared paving texture fills only missing boundary pixels.
All views have zero mask mismatches, overflow and opaque trimming in `bake-result.json`.
## Animated turbines

New construction before 1980 keeps the red/silver lattice tower with its sixteen blue paddles.
From 1980 onward it uses a cream-white tubular mast and three long blades. Terrain bit 4 stores
this choice at construction, so existing turbines do not change design as the calendar advances.
Native saves preserve the bit; stock map records omit it. Imported stock turbines retain the old
design because stock files have no construction date for each individual wind turbine.

The stationary bodies and front-facing rotor textures in [animation](animation/) were made with
the built-in Imagegen tool. Source and refinement prompts are retained alongside the source PNGs.
The offline baker projects each rotor into four real quarter-turn planes and sixteen phases;
the hub positions are fixed artist landmarks in `animation/wind.json`. Rear views bake the
stationary shaft/mast occlusion into the rotor resource. Ground uses the exact existing foundation
mask. No runtime image rotation, coverage mask, map array or per-turbine animation allocation is used.

Rebuild with `python3 tools/bake_wind_power.py`. The assembled still and animation are written to
`build/art-reference/wind-power/`. Rotors run at 12 frames/second at Normal speed, half at Slow,
double at Fast and four times at Unlimited. Pausing freezes the blades. Animation reuses the
power-smoke presentation clock and existing visible-effect damage handling; smoke keeps its own
three-frame/second cadence.
