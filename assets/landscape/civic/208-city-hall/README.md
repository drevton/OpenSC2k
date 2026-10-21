# City Hall artwork

`view-0.png` through `view-3.png` are clockwise 0/90/180/270-degree sprites, each 384×448
RGBA with baseline 448. The square site covers 3×3 tiles at 128 pixels per tile.
`final-rotations-preview.png` compares all four at the same scale and baseline.

The illustrated masters are `square-geometry-*-study.png`. ImageGen repainted four fixed
Blender rotation guides using the preferred original artwork for style. ImageGen then removed
the continuous paved base to produce registered foreground cutouts; prompts are saved alongside.
One rear extraction invented an entrance and was rejected; the accepted rear cutout uses a
narrower paving-only instruction.

Each cutout returned one pixel narrower than its input. The `foreground-registered-*.png`
copies add one transparent column on the right without resizing or moving the artwork;
`foreground-registration.json` records this registration.

Rebuild from the repository root:

```sh
python3 tools/bake_civic_sprites.py assets/landscape/civic/208-city-hall
```

The shared offline baker normalizes the original painted ground coordinates while preserving
vertical walls. `retain_painted_ground` keeps the original warm paving and contact shadows;
the shared paving texture fills missing corners only. The native terrain masks determine exact
half-open foundation ownership. The 384×448 mask contains 36,864 owned pixels. All four baked
ground layers match the native nine-tile union with zero differences, and all combined sprites
have zero pixels outside the site's vertical extrusion. No opaque foreground pixels required
clipping. Details are recorded in `bake-result.json`.

Use the combined `view-*.png` sprites as final artwork. With painted-ground retention enabled,
`ground-*.png` are masked painted underlays and may contain portions of the original building.

The native landscape pack discovers `sprites.json` and installs all four views. The main layout follows the fixed
guides; fine window, lantern and planting details can still vary between generated views.
