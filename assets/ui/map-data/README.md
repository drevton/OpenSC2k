# Painted map toolbar icons

All 18 native data-map options render fixed RGB colors into the existing preview buffer,
independently of the compatibility palette. Terrain, trees, water and structures share a muted
background. Roads, rails, utility lines and selected facilities use a light highlight.
Traffic, crime and pollution rise from yellow to red; density rises through blue;
land value and service coverage rise through green. Growth uses green for increases,
red for declines and a neutral background for stable values. Electricity supply is green,
water supply is cyan, and missing services are red. Zero metric levels retain the background.
Top-down and isometric previews use the same colors; the city-view data tiles already carry
their own authored RGB artwork.

All 23 map-window icons use original painted raster artwork, matching the main
toolbar's dimensional materials, lighting and color accents. Transparent source
sheets live in `painted/`; `manifest.json` records explicit subject crops and the
stable `CITY_MAP_ICON_*` atlas IDs. `prompts.json` preserves the full built-in
ImageGen prompts and the main-toolbar style reference. The old `icons.svg` is
historical artwork and is no longer baked.

Rebuild from the repository root with Node.js and `sharp`:

```sh
node tools/generate_map_icons.cjs --map-only
```

`python3 assets/ui/map-data/build.py` delegates to the same packer. The full
`node tools/generate_map_icons.cjs` command also rebuilds bridges and toolbars.
`preview.png` shows every map icon in atlas order, reading left to right:

| ID | Subject | ID | Subject |
| --- | --- | --- | --- |
| 0 | Structures | 12 | Population density |
| 1 | Roads | 13 | Population growth |
| 2 | Power | 14 | Crime |
| 3 | Water | 15 | Police coverage |
| 4 | Population | 16 | Police station |
| 5 | Safety | 17 | Fire coverage |
| 6 | Pollution | 18 | Fire station |
| 7 | Land value | 19 | School |
| 8 | Civic services | 20 | College |
| 9 | Zones | 21 | Show data on city |
| 10 | Rail | 22 | Isometric map |
| 11 | Traffic | | |

The packer trims transparent margins, fits each subject into a 124-pixel square
with Lanczos filtering, then adds a two-pixel transparent border. The runtime
still loads the same atlas once through Assets, without new drawing buffers.
Only `icons.dat` and this README are packaged; PNG sources are development assets.

Data format: `SC2KICO3`, shared default references and sparse resolution overrides, then
u32 square side, payload offset and payload length per image. Payloads use ordinary RunSprite
row offsets and RGB/RGBA/transparent runs with straight alpha.

The icon pack includes 1x, 2x and 4x detail (32, 64 and 128 pixels), selected independently
of map zoom through the shared sparse sprite-variant mechanism. Optional
`variants/<icon>-1x.png`, `-2x.png` and `-4x.png` sources replace individual resolutions;
otherwise the packer generates them from the painted source. Window caches retain the
prepared button sprites, painted through the normal sprite drawer.
