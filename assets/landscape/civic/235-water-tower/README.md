# Water Tower — kind 235, 2×2 tiles

Four clockwise 0/90/180/270-degree illustrated views are installed through `sprites.json`.
Each final `view-N.png` is 256×384 RGBA, baseline 384, at 128 pixels per tile.
The original ImageGen artwork follows four fixed layout/pose guides. Small painted details
can vary. No geometry, lighting or texture synthesis runs during play.

Rebuild fixed guides and finished artwork offline:

```sh
python3 tools/build_civic_guides.py --design assets/landscape/civic/235-water-tower/design.json
python3 tools/bake_civic_sprites.py assets/landscape/civic/235-water-tower
```

`painted-N.png` masters and prompts accompany the final resources. The strict bake keeps
content inset 4 pixels and fits the exact native four-tile foundation. Ground owns 16,384 pixels;
all four views have zero mask differences, zero site overflow and zero opaque pixels trimmed.
`bake-result.json` records measurements; the build independently validates the actual images.
`final-rotations-preview.png` compares the completed set.
