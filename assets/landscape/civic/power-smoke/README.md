# Shared power-plant smoke

Two Imagegen loops supply sixteen frames each: dark chimney smoke and white cooling-tower steam.
Both use a 128×128 canvas and three frames per second at Normal speed. Dark smoke uses anchor
(40,128); broad cooling-tower steam uses (64,128), so fitting its outlet does not shrink the plume. Slow
uses 1.5 fps, Fast 6 fps and Unlimited 12 fps. Pausing freezes the current frame and fractional
phase; resuming or changing speed does not catch up elapsed time. Every rotation reuses
these images. `smoke.json` contains immutable chimney positions for oil, nuclear and coal plants;
it also selects the existing leftward drift on the coal plant's rear view.

`python3 tools/bake_power_smoke.py` extracts and registers the source sheets, writes the shared
frames and produces smoke-free plant layers. Original plant views remain as resource references.
After registration, a soft opacity falloff thins the sides of each billow while retaining dense
centers. This is baked into the existing frames; source artwork, colors and anchors stay fixed.
Only the smoke-removal regions use the cleaned Imagegen insert; the rest of each plant stays fixed.
Run this after rebaking a plant with `tools/bake_civic_sprites.py`.

The dark-smoke outlet stays broad enough for the chimney opening. Steam uses broad, rounded
billows and a tall rising plume without a flat skirt or narrow waist, with its anchor lowered into
the cooling-tower mouth. Steam frames split at transparent gutters rather than assumed exact grid
quarters, and register from the broad lower quarter instead of the shifting lowest wisps.
The extraction anchor stays fixed; steam's presentation baseline is 110 rather than 128, placing
the rounded root 18 artwork pixels deeper in the opening. The baker extracts a front-rim visibility
mask for each tower and view. The landscape packer applies those masks to the steam frames, so
steam stops at the painted lip and the complete building is drawn once. There is no runtime
masking or separate foreground drawing.
The offline insert copies each complete oval rim to avoid cut edges and remnants of the old smoke
beside the mouth. The baker writes animation, still and sixteen-frame mouth contact sheets for
all four views under `build/art-reference/power-smoke/`.

The corrected source sheets use the built-in Imagegen tool. Final prompts:
[dark smoke](smoke-base-width-prompt.txt) and [cooling-tower steam](steam-base-width-prompt.txt).

The landscape packer checks every frame at every emitter against the native site mask, then packs
the frames and emitter metadata into `landscape.dat`. Assets loads them once. Drawing selects
frames from presentation time and reuses the existing per-window visible-effect damage storage.
No simulation fields, RNG, save format, runtime image synthesis or per-frame allocations are added.
