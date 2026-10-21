# Tarmac (230)

The active artwork is `approved-view.png` with its `approved-ground.png` foundation.
`sprites.json` and `approved-bake.json` select the approved painting for the resource pack.
The older Blender sources and quarter-turn paintings are retained as authoring material.

The 128×192 sprite has baseline 192 and a baked 1×1 ground diamond. The parked
plane is painted into this sprite. Tarmac and parked fighter sprites paint in the
ground pass, so neighboring hangars cover aircraft pixels that extend behind them.
Moving aircraft remain separate depth-sorted event sprites.
Normal builds repack these sources with `tools/pack_landscape_assets.py`.
