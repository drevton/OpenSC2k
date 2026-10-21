# Native power-line artwork

128-pixel tile pitch, matching the shared native ground projection. Upright galvanized
steel lattice towers replace the original short poles. Suspension and heavy suspension
forms have two orientations; angle/end and junction forms carry orthogonal arms; water
crossings use braced supports. Small individual concrete feet fit each of the fourteen
terrain shapes. Towers stay upright on slopes; they are never sheared with the ground.
Cable-only spans have transparent backgrounds and no ground base. Straight runs leave
three cable-only tiles between foundations; ends, bends and junctions retain supports.
Conductors use a thicker steel-gray profile to stay visible on unsupported tiles.

`python3 tools/build_power_assets.py` regenerates the checked-in PNG resources and
updates the landscape catalog. The ordinary build packs PNGs using Pillow. Geometry,
lighting, material sampling and conductor curves run offline only. Roads/rails retain
ownership of their surface artwork; power crossings compose their matching plain
surfaces with the independent overhead cable resources. Rebuilding roads or rail does
not overwrite the installed power crossing resources. The full landscape importer
runs the power bake after road/rail sources.

There are 358 component resources: 272 cable halves (four directions, four endpoint
support profiles, seventeen grades) and 86 towers (six forms across fourteen ground
shapes, plus two water forms). The grade bank covers quarter-level anchor differences
from -8 to +8 (two full terrain levels). Grades outside that bank use its nearest limit;
imported discontinuous cliffs are not represented by additional cable artwork.
Resource IDs 3973–4330 belong to bitmap artwork, independently of the 256 City tile kinds.
IDs 14–28 and 92 borrow the authored tower rasters for catalog extents and fallbacks;
native drawing uses stored support metadata, connections derived from immediate neighbors,
and these components.

See [PREVIEW.png](PREVIEW.png) for tower families and connected spans, and
[SLOPES.png](SLOPES.png) for connected climbs in all four planar directions.

## Material provenance

`galvanized-steel.png` was generated with the built-in ImageGen tool. Exact geometry and
all final tile rasters are produced by `tools/build_power_assets.py`; no generated image
is read or transformed at runtime. The generation prompt was:

> Use case: stylized-concept. Asset type: seamless source material texture for offline-baked isometric power transmission tower sprites in a city-building game. Create a square, flat, evenly lit texture of aged galvanized steel, viewed perfectly head-on, neutral medium cool gray with fine irregular zinc spangle, subtle small scuffs and sparse darker weathering. Fine-scale restrained variation, suitable for very slender lattice beams after downsampling. Absolutely no objects, towers, poles, bolts, seams, panel boundaries, shadows, gradients, perspective, letters, labels, borders, logos, or watermark. The entire image is one seamless repeatable material swatch. No transparency.
