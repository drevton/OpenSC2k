# Gas power plant (201)

Four genuine quarter-turn views of one fixed layout, illustrated with the built-in Imagegen tool.
The Blender model and geometry guides fix the building, holder and flue positions; painted sources and prompts are retained.

Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/201-gas-power-plant`.
Each final sprite is 512×640 with baseline 640 and an exact native 4×4 ground mask.
The baker retains painted paving and shadows; the shared texture only fills missing edge pixels.
All four views have zero mask mismatches, overflow or opaque trimming in `bake-result.json`.
