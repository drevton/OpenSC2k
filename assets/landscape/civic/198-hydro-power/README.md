# Hydroelectric Plant (198)

Four genuine quarter-turn views of one pale-concrete dam: three spillways, four piers,
a railed maintenance deck and one copper-roof control house. Downstream views show water
in the spillways; upstream views show the solid retaining wall. No view is a mirrored alias.

The new design uses the previous fixed Blender layout and four-view paintings as geometry
references. The built-in imagegen tool created the new paintings, then matched their finish
to the current approved fire station and police station: matte materials, restrained edges,
neutral concrete, weathered copper and subdued blue water. Raw final paintings are
`design-v2-source-0.png` through `design-v2-source-3.png`; the initial design reference,
full prompts and source hashes are retained beside them. Earlier sources and the former
approved single-view painting remain available as references.

`bake.json` registers the generated paintings to the existing four-view footprint using
explicit ground corners. `design-v2-provenance.json` records the calibration and source hashes.
Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/198-hydro-power`.
The baker checks native ground ownership, site overflow and opaque trimming in each view;
`final-rotations-preview.png` and `bake-result.json` show the results.

Final sprites contain only the structure and its spillway water; the native animated water
surface and terrain skirts draw beneath. The renderer chooses the camera-relative river slope:
view 0 drains east (shape 2). Hydro geometry retains that slope for adjoining banks and terrain
faces. Stored legacy facing still serves SC2 projection and diagnostic drawing.

The clean animated underlay uses `water-slope-1.png` through `water-slope-4.png`, without the
rocky whitewater of ordinary rapids. Rebuild them offline with
`python3 tools/build_landscape_assets.py --hydro-water-only`.
The single-view building installer deliberately excludes hydro, and its hydro rebake command
uses the four-view recipe. Normal builds pack the baked sprites into the graphics archive.
