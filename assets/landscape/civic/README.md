# Civic and special building artwork

The catalog covers the one-tile Small Park at kind13 and all 57 native civic/special kinds
in the 198–255 range, with four baked views each. ID199 is a retired hydro variant,
represented by native kind198. `catalog.json` records footprints and completed views.

Approved single-view paintings use resource-level horizontal mirroring for directions 90 and
270, sharing image bytes across all four directions. Separate authored views retain their own
artwork. Hydro is explicitly kept as four authored views because its upstream wall and downstream spillways differ. The packer mirrors registered smoke emitters along with each plant.

Each completed building has a `sprites.json` manifest with four clockwise 0/90/180/270-degree
views at 128 pixels per tile, one canvas and baseline, plus `bake.json` and `bake-result.json`.
Artwork comes from ImageGen. Fixed rotation guides and offline registration constrain the
main geometry; small painted details can vary. These are original designs inspired by classic
city-builder artwork, not reproductions of the original game tiles.

`tools/bake_civic_sprites.py` fits painted artwork to the native foundation. The authoritative
mask is the union of native flat terrain tile masks at 128 pixels per tile with half-open
pixel ownership. Upper building pixels may rise above its rear edge, but cannot extend below
the front boundary or outside the site's horizontal extent. Ground can be transparent in the
combined sprite: the renderer first paints real terrain beneath the bounded footprint.

Ordinary civic buildings separate the broad ground from the building bitmap, retaining useful
walkways, steps, vehicle markings, lamps and service equipment in the foreground. Decorative
perimeter planting is sparse. Schools retain their chalk games and playground markings;
the rail depot contains no embedded tracks and sits beside independently built rail tiles.
The hospital, police station and fire station use asphalt across their entire footprints.

`site.json` and each sprite manifest select terrain or one of six reusable 128-pixel materials:
stone, concrete, asphalt, gravel, brick and compacted earth. The material bitmaps and ImageGen
prompts live in `../buildings/ground/`. Assets owns the immutable material choices; no City or
save fields are needed. Parks and the five arcology/landmark sites instead retain their complete
authored ground, including interior paths, plazas, ponds and functional landscaping.

The packer combines fixed paving materials with their building foregrounds and checks opaque
coverage of the exact native footprint. Existing complete painted sites are detected by the same
coverage check. A flag in the existing ground metadata byte records coverage across all four views;
the renderer then omits redundant flat ground diamonds beneath visible completed buildings.
Exposed terrain sides, construction, hidden/custom buildings, water and uneven ground retain
their required terrain drawing. Climate-dependent terrain is not baked into a fixed material.

`ground-separation-prompt.txt` records each extraction/edit brief. The ImageGen input sheet,
output sheet, panel registration and verification accompany the artwork. The baker's
`--split-foreground` option registers those edits to the original source canvases using masked
colour matching, retains the original footprint and height configuration, then rebakes all four
views. Surface markings keep their exact existing geometry through offline colour-key extraction.
Only pre-baked sprite streams are used during play.

`tools/pack_landscape_assets.py` discovers completed manifests and independently checks each
actual ground mask, four image dimensions, angle order and allowed sprite extent before packing.
It rejects incomplete or invalid sets. Large artwork stays in data resources. Rendering selects
a baked view from building orientation plus map rotation (map rotation alone for Small Park).
No model evaluation, texture lookup or image synthesis runs during play. Clipping and damage account for all four view extents.
Original-landscape diagnostics and custom sprite overrides retain their existing drawing path.

Views may optionally name an `animation_mask`: an 8-bit grayscale PNG at the exact sprite size.
Zero keeps the painted RGB pixel; values 1–75 select compact animation palette IDs, with source
alpha preserved. The packer emits the renderer's animated run commands. Ordinary civic RGB
colors do not accidentally opt into water animation. No animation is inferred from appearance.

Oil and coal smoke and nuclear cooling steam use two shared sixteen-frame overlays at three
frames per second on Normal speed, scaled with game speed and frozen while paused.
`power-smoke/smoke.json` supplies fixed emitter positions for each rotation;
the building layers stay still. `tools/bake_power_smoke.py` registers their source artwork.
The existing visible-effect damage capture repaints plume bounds in each map window.

Rebuild the normal app with `python3 tools/dev.py build`. The build packs manifests automatically.
ImageGen source masters and prompts accompany completed artwork. Studies and rejected
variants are local working material, not installed assets.

The arcology designs retain a tall landmark scale. Their original LARGE.DAT sprites measure
196, 182, 210 and 235 pixels high for Plymouth, Forest, Darco and Launch, respectively, at
32 pixels per tile. At the new 128-pixel tile width those references are 784, 728, 840 and
940 pixels. These are occupied sprite heights including foundations, not storey counts.
The new manifests retain the reference measurements and record each baked view's actual
occupied height; asymmetric towers can change the projected extent across rotations.
The Launch design uses a broad hemispherical glass canopy enclosing two hills and a lake
above its habitat drum and support structure. Its canopy stays round across all four views.
Its offline `structure_registration` additionally locks the canopy/drum silhouette and
occupied height to the approved first view, independently of ground fitting. Source landmarks
align the upper artwork and smoothly meet unchanged supports above the footprint. The packer
checks the actual four upper alpha masks for equality before admitting the sprites.
