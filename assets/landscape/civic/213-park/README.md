# Park (213)

Four genuine quarter-turn views of one fixed Blender layout, finished with the built-in Imagegen tool.
Source paintings, prompts, model and geometry guides are retained.

Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/213-park`.
The final 384×448 sprites have baseline 448 and exact native 3×3 ground ownership.
Painted ground, borders and shadows are preserved; shared paving only fills missing boundary pixels.
`bake-result.json` records zero native mask errors, sprite overflow and opaque artwork trimming.
Smoke and steam, where present, are currently static.

All four source views were generated together in one Imagegen call. `source-crops.json` records the exact sheet rectangles.
The missing source edge pixels use grass sampled from the painting, preserving the lawn instead of adding a paved rim.
