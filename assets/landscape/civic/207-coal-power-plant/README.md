# Coal power plant — kind 207

Four complete painted quarter-turn views, each 512×640 RGBA, baseline 640, for a
4×4-tile footprint at 128 pixels per tile. `sprites.json` is the native pack manifest.
`final-rotations-preview.png` shows all views with the same canvas and baseline.

The original toolbar illustration in `design-reference.png` defines the design and
painting style: stepped cream generator hall, two red brick chimneys, one shorter
steel flue, conveyor and coal yard, a small side annex and planted margins.
One fixed Blender model supplies four real quarter-turn geometry guides; Imagegen
supplies hand-painted detail. Smoke is part of the static paintings.

The baker retains the painted yard, footpaths, borders and shadows. No opaque art
is trimmed. All four ground masks match the exact native 4×4 union of 16 terrain
masks: 65,536 owned pixels, zero mismatches and zero sprite overflow. See
`bake-result.json` for measured source calibration and actual validation results.

Rebuild from the repository root:

```sh
blender --background --python-exit-code 1 --python tools/build_civic_rotation_guides.py -- \
  assets/landscape/civic/207-coal-power-plant
python3 tools/bake_civic_sprites.py assets/landscape/civic/207-coal-power-plant
```

`model.json` and `rotation-guide.blend` retain the fixed architecture. Small Blender
guide materials are not the final art. `painted-source-N.png` and adjacent prompts
record the built-in Imagegen output; the 90-degree source includes a documented
correction removing an accidentally generated third brick chimney.

## Shared smoke animation

The runtime uses `smoke-base-N.png` plus the shared sixteen-frame smoke/steam loops.
Original `view-N.png` paintings remain as reference inputs. Run `python3 tools/bake_power_smoke.py`
after rebaking the civic source. Frame sources, prompts and emitter anchors are in `../power-smoke`.
