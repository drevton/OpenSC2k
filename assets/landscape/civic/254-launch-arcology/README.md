# Launch Arcology (254)

The production files are `approved-view.png`, `approved-bake.json` and
`approved-bake-result.json`, on a 512×1152 canvas at 128 pixels per tile.
Four directions share the selected single view; alternate views are mirrored.
The older four-view paintings and `bake.json` are historical authoring studies.

## Proportions

The former installed sprite occupied 1,089 vertical pixels against a stock
reference of about 940 at the same tile scale. The revised habitat is narrower,
with visibly longer engineered support legs and a ground-level entrance pavilion.
The dome remains round. The lakes are smaller, with more green land around them;
the frame has fewer fine bronze/graphite members instead of the bright red grid. `target_roof_height: 940` matches stock by placing the crown 940 pixels above
the site's front baseline; this includes the empty ground margin below the feet.
Uniform offline scaling preserves the dome and structural proportions. The bake
result records actual cutout bounds and scale. Gameplay only draws baked pixels.

`landscape-revision/generation-history.json` preserves the latest ImageGen prompt;
`scale-revision/generation-history.json` records the earlier support revision. Both preserve
input snapshots and selected raw outputs. The explicit 2:1 virtual ground corners
register a ground-free cutout to a centered 4×4 site; no painted lot is required.
All footings must remain inside the native site prism with zero clipped pixels.

The site owns exactly 65,536 ground pixels, independent of the structure size.
The finished cutout contains no pavement diamond. `ground: concrete` selects the
existing reusable ground pass, avoiding duplicate supports or a second baked slab.
The ground is not rescaled when the building size changes.

## Rebuild

With `tools/requirements-artwork.txt` installed, run from the repository root:

```sh
python3 tools/bake_civic_sprites.py assets/landscape/civic/254-launch-arcology --approved
python3 tools/pack_landscape_assets.py --app build/mac/Release/sc2k.app --zstd
```

No ImageGen access is needed to rebake the checked-in sources. The stock reference
metadata remains under `legacy_height_reference` in `sprites.json`.
