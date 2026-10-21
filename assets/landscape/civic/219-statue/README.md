# Statue — kind 219, one tile

Four clockwise 0/90/180/270-degree views are installed through `sprites.json`.
Each final `view-N.png` is 128×256 RGBA, baseline 256, at 128 pixels per tile.
ImageGen supplied original illustrated art and detailed quarter-turn views from fixed
pose/layout guides. Small painted details can vary; no geometry is rendered during play.

Rebuild the fixed guides and finished artwork offline:

```sh
python3 tools/build_civic_guides.py --design assets/landscape/civic/219-statue/design.json
python3 tools/bake_civic_sprites.py assets/landscape/civic/219-statue
```

`painted-N.png` masters and prompts accompany the final resources. The strict bake keeps
content inset 4 pixels before applying the exact native one-tile foundation. Each ground
owns 4,096 pixels; all four views have zero mask differences, zero site overflow and zero
opaque pixels trimmed. `bake-result.json` records measurements. The build independently
validates actual bitmaps and masks. `final-rotations-preview.png` compares the set.
