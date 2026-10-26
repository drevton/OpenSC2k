# Neighbor population illustrations

Eight original stylized rendered miniatures replace the stock NEIGHBOR.BMP in native
play. They represent population stages, not specific neighboring cities:

| Stage | Population | Illustration |
| --- | --- | --- |
| 0 | Empty city, including the local city | Unsettled countryside and a pond |
| 1 | Below 2,000 | Two tiny cottages in open countryside |
| 2 | 2,000–9,999 | A small hamlet surrounded by open land |
| 3 | 10,000–49,999 | Small city |
| 4 | 50,000–99,999 | Large city |
| 5 | 100,000–499,999 | Metropolis |
| 6 | 500,000–999,999 | Huge city with dense midrises and office towers |
| 7 | 1,000,000+ | Megacity with a broad urban footprint and tall skyline |

NeighborArtworkStage selects all eight stages directly, including stage 0 for an empty local city. Thus Starter City (130,029) uses stage 5,
whereas Petaluma (1,339,170) uses stage 7. Artwork does not affect simulation or RNG.

Sources were created with built-in ImageGen, using the project's own house,
apartment and office illustrations as style references. Full prompts, references
and source hashes live in manifest.json. The replacement set uses a common meadow
painting as its edit reference; stages 6 and 7 extend the existing metropolis painting.
Update hashes when replacing a source painting.
No original game pixels are used in the pack.

Run `python3 tools/bake_neighbor_artwork.py` to refresh neighbors.dat, baked PNGs
and preview.png. The preview shows the actual baked images. Normal builds bake
incrementally to the build directory when source PNGs, manifest or baker change;
the app ships only that generated pack. No generation or downloads during builds.

Each image is 512×256, four times the original bitmap cell's 128×64 dimensions.
Five isometric diamond images form the neighbor map: the local city is in the
center and its neighbors are offset by half an image width and height along each
diagonal. Images keep their authored orientation and proportions. Drawing uses
only axis-aligned UI scaling, with no rotation, shear, corner warping or separately
painted ground. Source paintings are opaque 2:1 rectangles. The bake downsamples
the whole frame to 512×256 and cuts the surrounding meadow with one shared exact
diamond stencil. Do not crop to each painting's content bounds or fit its silhouette:
that changes the ground footprint and creates gaps. The stencil samples pixel centers;
half-width/half-height translations meet without gaps or overlap. All eight ground
footprints match, including the empty region. The diamond describes the ground,
not a height limit. Stages 6 and 7 include transparent foreground layers extracted
from their full scene paintings. The bake composites those buildings and trees
over stage 0's terrain, preserving their full silhouettes beyond the ground diamond.
Their foreground widths (410 and 460 baked pixels) grow the urban footprint while
keeping the common terrain at 512×256. Foreground layers retain the source canvas
and camera; keep their generation prompts, widths, top offsets and hashes in manifest.json.
The megacity foreground starts 12 baked pixels below the canvas top so building
bases rest on the ground diamond. Roofs and upper floors may overhang its back
edges; building bases must not float beyond them. The bake rejects offsets that
would clip the foreground against the canvas bounds.
This needs no runtime mask or separately painted ground.
White outlined labels are drawn last, with population above city name.
Each image uses the original 128×64 logical size: at 400% UI scale the
512×256 bake is drawn one-to-one when the map fits the screen.
On narrower or shorter screens the entire map shrinks together to keep all five visible.
Assets retains all eight images as ordinary RunSprite objects with independent
palettes. The neighbor window uses the same DrawCanvasSprite preparation and
DrawRunSprite painting path as advisors and illustrations. It retains up to five
prepared sprites, reusing a preparation when multiple cities select the same stage.
Only source or display-size changes rebuild a preparation; redraws reuse it.
Closing the window releases its prepared sprites. Source images keep their full
512×256 canvas, so silhouettes are never cropped to content bounds.

The pack uses the same RunSprite encoding as tiles and buildings, through the
existing encode function in tools/pack_landscape_assets.py. Blank rows use equal
row offsets, leading transparency uses an initial skip, internal transparent runs
have no pixel payload and trailing transparent pixels are omitted. Static color
runs contain one-byte indices into that image's palette. Maximum-coverage palette
selection preserves rare roof and facade colors in the smallest settlements.

Startup validates these streams and retains them directly. It no longer expands
them into eight rectangular indexed buffers, removing 1,048,576 pixel bytes and
leaving the approximately 548 KiB of source runs and palettes. All state is bounded
by the artwork and window sizes, independent of city map dimensions.

Pack format: the same SC2KPIC1 records as advisors and illustrations, read by the
same LoadCityPictureAssets loader. The header stores an eight-byte magic and a
little-endian u32 count. Each stage record stores ID (0–7), width, height and palette
count as u16, payload size as u32, 256 RGB palette entries, then native sprite row
offsets and runs. All eight 512×256 canvases are retained. Palette index zero is
reserved for transparency and never appears in a visible run. The loader validates
dimensions, stage order, palettes, sprite streams and EOF. Invalid packs fail
rather than falling back to old graphics.

The current map window already uses authored assets/ui/map-data/icons.dat.
The obsolete resource-247 decoder, startup load, caches and diagnostic setter
have been removed; no replacement artwork is needed for that old strip.
