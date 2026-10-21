# Police Station — kind 210, 3×3tiles

Four clockwise 0/90/180/270-degree illustrated views are installed through `sprites.json`.
Each final `view-N.png` is 384×384 RGBA, baseline 384, at 128 pixels per tile.
The architecture is original; ImageGen paints fixed quarter-turn geometry guides, using
previous painted views to retain materials and features. Small decorative details can vary.
The guide is not final artwork and is never rendered during play.

`design.json` records fixed guide masses. Regenerate guides offline with:

```sh
python3 tools/build_civic_guides.py --design assets/landscape/civic/210-police-station/design.json
```

The four `painted-N.png` masters and their prompts are retained. `bake.json` registers them
onto `foundation-mask.png` with a 4 pixel content inset, preserving warm painted paving.
Rebuild finished sprites with:

```sh
python3 tools/bake_civic_sprites.py assets/landscape/civic/210-police-station
```

Every ground alpha matches the authoritative native nine-tile union exactly: 36,864 pixels.
All four views have zero ground-mask mismatches, zero pixels outside the site prism and
zero opaque foreground pixels trimmed. Strict baking rejects rather than trims opaque
artwork. Actual measurements are in `bake-result.json`. `final-rotations-preview.png` shows
the completed set. The normal build validates the actual mask and bitmaps before packing.
