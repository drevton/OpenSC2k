# Zoo (218)

Four genuine quarter-turn views of one fixed Blender layout, finished with the built-in Imagegen tool.
Source paintings, prompts, model and geometry guides are retained.

Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/218-zoo`.
The final 512×640 sprites have baseline 640 and exact native 4×4 ground ownership.
Painted ground, borders and shadows are preserved; shared paving only fills missing boundary pixels.
`bake-result.json` records zero native mask errors, sprite overflow and opaque artwork trimming.
Smoke and steam, where present, are currently static.

The four source angles share one Imagegen sheet; `source-crops.json` records the exact extraction rectangles.
A targeted Imagegen correction keeps the giraffe and elephant in fixed opposing poses throughout all rotations. The selected correction prompt is retained.
