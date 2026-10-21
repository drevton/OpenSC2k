# Runway (221)

The active artwork is `approved-view.png` with its `approved-ground.png` foundation.
`sprites.json` and `approved-bake.json` select the approved painting for the resource pack.
The older Blender sources and quarter-turn paintings are retained as authoring material.

Views 0 and 2 run along the native X axis; views 1 and 3 mirror the painting to run
along Y. The pack shares the same pixel data through its mirror references. Building
orientation selects the view for imported cities, generated airports and camera rotation.

The 128×128 sprite has baseline 128 and a baked 1×1 ground diamond. Runways and
crossings paint in the ground pass, before airport buildings and moving aircraft.
Normal builds repack these sources with `tools/pack_landscape_assets.py`.
