# Native railway artwork

The railway set uses 128×64 RGBA surface overlays and 128×80 inclines, at the
same artwork detail as the native roads. Steel running faces, rusted rail feet,
weathered timber sleepers, tie plates and fasteners sit on painted stone ballast.
Track gauge is 29% of a tile's map-plane width; sleepers occupy 49.4%, and the
ballast bed occupies 63%, with a narrow irregular transparent fringe.

`ballast-material-master.png` is the original ImageGen material. Its complete
prompt is in `ballast-material-prompt.txt`. The offline builder projects that
material in the ground plane: curves never stretch or clamp a row of texture.
Rail gauge and sleeper spacing are geometric, and ImageGen never controls joins
or tile ownership. Supersampled artwork is clipped to the shared 128px master
ground masks in `../opengfx2/detail128/terrain-masks.png`, also used by terrain,
water and roads. Mask sampling uses nearest neighbor. Shared edges cannot overlap.

`manifest.json` contains 61 finished sprites:

| IDs | Artwork |
| --- | --- |
| 44–45 | Straight tracks in both directions |
| 46–49 | Four inclines at the native 16px height step |
| 50–53 | Four circular curves |
| 54–57 | Three-way switches |
| 58 | Four-way junction with through tracks, four curved turnouts, frogs and check rails |
| 59–62 | Flat slope approaches, retaining the original aliases |
| 69–70 | Embedded rails in matching asphalt, with new crossing signs |
| 71–72 | Track beneath power lines |
| 77–78 | Track beneath elevated highways |
| 90–91 | Matching girder bridge decks with retained OpenGFX2 front occlusion |
| Native highway merge IDs | Six highway/rail crossing variants beside ramps |
| Native rail detail IDs | 28 diagonal bends and reusable approach halves |

The four-way junction retains both straight routes and adds four curved
turnouts, connecting every adjacent pair of approaches at the same rail gauge.
The artwork shows the turning routes already admitted by the connected tile.

Alternating bends select a straight diagonal middle and quintic end transitions.
Ends next to straight track use a rounded cubic extending into that tile, with
monotone heading and matching tangents. Sleeper positions follow arc length.
Eight curved approach halves replace eight full approaches and eight stored
combinations. Four ordinary straight-half run sprites are derived from the existing
straight track PNGs during packing. The renderer draws two normal sprites for an
approach; an ordinary straight remains one draw. Selection examines only
nearby connected tiles on the same rendered plane, including raised-flat ground
and level foundations. Slopes and custom artwork keep their own selection.
Stored track IDs, train routing and saved cities are unchanged.

Rebuild after changes to the material or road/highway artwork:

```sh
python3 tools/build_rail_assets.py
python3 tools/dev.py build --log /tmp/sc2k-rails-build.log
```

Pillow and NumPy are offline dependencies. The full landscape builder loads the
rail manifest after roads so rail composites win. A standalone road installer
preserves the railway-owned records; rebuild rail composites after changing road
materials. Runtime loads only finished sprite runs into the existing asset and
zoom caches. `rail-preview.png` shows individual pieces and their connected joins.

Rail bridge structures retain the existing OpenGFX2 artwork and licensing from
`../opengfx2`; track materials and geometry are newly generated for this project.

Builders refresh the individual PNG references in `../opengfx2/sprites.json`.
Run `python3 tools/dev.py build` to regenerate the runtime pack from those checked-in sources.
