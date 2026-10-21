# Fire station — kind 211, 3×3 tiles

The installed first view is `approved-view.png`, 384×600 RGBA at 128 pixels per
tile, baseline 600. Four sprite IDs share this single view; additional rotation
artwork remains deferred. Older `view-N.png` files are historical sources.

The station has a square main plan centered on its lot, with a narrow garage
apron and real in-ground lawns in both front corners. Red masonry dominates;
restrained slate-grey and charcoal detailing replaces the broad cream trim.
The central garage/flame-emblem bay projects slightly ahead of its flanking bays.
It keeps approximately stock proportions and three occupied floors. All garage doors are closed, with no exterior engines, directional drive-out
lanes or implied road connection at a particular property edge. The full lot has
authored concrete work pads, neutral stone pedestrian paving, drains, service
access and a few safety bollards. The surface is flat, with no raised plinth.

`grey-trim-revision/stock-vs-revised.png` compares stock and revised art at equal plot
width. `grey-trim-revision/generation-history.json` records exact built-in ImageGen
prompts, references and raw-output hashes. Selected raw paintings, their immediate
input references and the registered foreground are checked in. Rejected trial
outputs are retained in the persistent Scratch artwork archive, outside Git.

## Exact footprint and projection

`approved-foundation-mask.png` owns exactly the union of nine native terrain tile
masks: 36,864 pixels. The normal baker rejects mask differences and structural
clipping. The completed painting fills all native ground corners with this exact
mask; it does not shrink the plot to fit the building.

The source painting's paving and architecture have slightly different projected
axes. `approved-bake.json` records `ground_corners` for the original paving and
`foreground_ground_corners` measured from the long grey facade/parapet edges.
The offline affine registration keeps walls vertical and makes both building axes
parallel to the game's 2:1 tile grid. `grey-trim-revision/registration.json` records the
foreground alignment and measured slopes.

`ground_excludes_foreground` subtracts the extracted building matte from the
painted ground before registration, so independently aligned layers cannot expose
old wall bases. The neutral ground texture fills covered areas beneath the new
building. The resulting sprite contains the authored ground; `ground` is `painted`
in `sprites.json`. All projection, masking, filtering and palette work is offline.
The game only draws baked pixels and does not inspect neighboring roads.

## Rebuild

Install `tools/requirements-artwork.txt`, then from the repository root:

```sh
python3 tools/install_building_studies.py --rebake 211
python3 tools/pack_landscape_assets.py --app build/mac/Release/sc2k.app --zstd
```

The portable recipe includes every painting, exact mask and texture required by
the bake. No ImageGen access or historical review worktree is needed to rebake.
`approved-bake-result.json` records footprint, clipping and projection results.
