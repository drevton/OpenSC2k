# Launch Arcology (254)

Four original Imagegen paintings from fixed quarter-turn layout guides. Master tile width is 128 pixels.
The baked 4×4 foundation uses the exact native terrain mask, with painted ground and shadows retained.
Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/254-launch-arcology`.
See `bake-result.json` for edge checks and `final-rotations-preview.png` for the baked views.
Fine painted details can vary between angles.

The round canopy and habitat drum use view0 as their size reference. All four baked views
have an occupied height of 1010 pixels, including the foundation, and exactly the same
upper alpha silhouette. `structure_registration` in `bake.json` records the measured
antenna, crown, canopy-rim and drum-bottom rows and the upper width in each original bake.
The offline baker aligns those landmarks and smoothly joins the supports to the original
pixels at row740, above the ground footprint. Ground, footings and the 512-pixel-wide
foundation stay fixed. The packer independently rejects a mismatched upper silhouette.
There is no registration, stretching or geometry work during gameplay.

Huge broad glass garden dome over an elevated habitat drum. Three major red supports, glazed central lift, rear-left service spine, front-right blue annex. Main entrance v-max visible 0/3. Two rocky hills and a lake fixed inside dome. Approximate original occupied height 940 master pixels. The glass cap is a broad hemisphere, not an egg, pointed dome or tall capsule; total height comes from the raised structure.
