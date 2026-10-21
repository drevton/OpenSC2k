# Nuclear Power Plant (203)

Four genuine quarter-turn views of one fixed Blender layout, finished with the built-in Imagegen tool.
Source paintings, prompts, model and geometry guides are retained.

Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/203-nuclear-power-plant`.
The final 512×640 sprites have baseline 640 and exact native 4×4 ground ownership.
Painted ground, borders and shadows are preserved; shared paving only fills missing boundary pixels.
`bake-result.json` records zero native mask errors, sprite overflow and opaque artwork trimming.

## Shared smoke animation

The runtime uses `smoke-base-N.png` plus sixteen-frame steam loops masked offline against
each tower's painted front rim. `steam-mask-N-M.png` stores the visibility masks used when packing.
Original `view-N.png` paintings remain as reference inputs. Run `python3 tools/bake_power_smoke.py`
after rebaking the civic source. Frame sources, prompts and emitter anchors are in `../power-smoke`.
