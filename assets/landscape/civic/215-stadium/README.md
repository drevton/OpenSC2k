# Stadium (215)

Four genuine quarter-turn views of one fixed Blender layout, finished with the built-in Imagegen tool.
Source paintings, prompts, model and geometry guides are retained.

Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/215-stadium`.
The final 512×640 sprites have baseline 640 and exact native 4×4 ground ownership.
Painted ground, borders and shadows are preserved; shared paving only fills missing boundary pixels.
`bake-result.json` records zero native mask errors, sprite overflow and opaque artwork trimming.
Smoke and steam, where present, are currently static.

The four source angles share one Imagegen sheet; `source-crops.json` records the exact extraction rectangles.
