# Editable graphics archive

The game loads `graphics/landscape/opengfx2/landscape.dat` at startup. The file may
be raw or one checksummed Zstandard frame. Decompression streams through the
existing bounded asset reader; it does not allocate a second decoded archive.
A present but invalid override fails instead of falling back to bundled artwork.

After the first build containing this loader, graphics changes require **packing
assets and restarting the game**, with no C compilation or executable relink:

```sh
python3 tools/pack_landscape_assets.py --app build/mac/Release/sc2k.app --zstd
```

`Play.command` opens the existing app without rebuilding or overwriting custom
graphics. It builds once only if the executable is missing. Use
`python3 tools/dev.py build` explicitly after C source changes.

Omit `--zstd` for an uncompressed archive. The compressed form has the same path;
the loader recognizes the Zstandard frame signature. To keep a separate mod root:

```sh
python3 tools/pack_landscape_assets.py --output /path/to/mod/landscape/opengfx2/landscape.dat --zstd
SC2K_GRAPHICS_OVERRIDE=/path/to/mod build/mac/Release/sc2k.app/Contents/MacOS/sc2k
```

The override replaces the landscape pack; other assets retain their normal
locations. No file reads or decompression happen while drawing. Live file
watching/reload is not implemented.

## Build dependencies

Install the Python tools with `python3 -m pip install -r tools/requirements-artwork.txt`.
Packing uses Pillow; rebaking also uses NumPy. `--zstd` needs the `zstd` executable.
The checked-in source paintings, masks, textures, recipes and baked sprites are
sufficient to rebuild the installed artwork without ImageGen, an API key or files
from an ignored build directory.

## Source artwork and bindings

Production building/civic folders contain `approved-view.png`, portable
`approved-bake.json`, immutable selected painting inputs, exact foundation masks,
and bake results. They are the approved single directions, including daytime
corrections. `sprites.json` maps the four direction IDs to that one file for now;
this is an explicit fallback while actual rotations are deferred. Do not describe
these shared bindings as four correctly rotated views.

`tools/install_building_studies.py` records the import and snapshots its bake
inputs. The original studies and their prompts remain preserved. To rebake selected
production kinds, use `python3 tools/install_building_studies.py --rebake 131`
(or several IDs). It preserves the historical recipes and updates the approved
outputs in a temporary offline workspace. The packer validates
the exact native tile union and rejects opaque pixels outside the site's prism.
Broad reusable ground stays separate, while approved authored sites keep their
painted details. The old windmill currently uses its approved static view; modern
three-blade turbines retain their existing animation. The new old-windmill rotor
animation remains deferred with the remaining directions.

`encoding: "indexed"` bakes an image-specific 256-colour RGB palette into the
archive. Quantization is offline, without dithering; per-pixel alpha is preserved.
RGB/RGBA sources remain supported. Indexed PNGs carry their own palette by default;
`global_palette: true` explicitly denotes the existing global-index selectors or
material intensity maps. Explicit `palette` PNGs can supply an alternate palette.
Animation runs retain their separate global animation namespace.

Optional `--bindings overrides.json` replaces existing IDs without code changes.
A rule may select one `id`, an `ids` list, or a masked range:

```json
[
  {"id": 1000, "ref": 1001, "mirror": true},
  {"match": {"first": 1002, "last": 1007, "mask": 1, "value": 0}, "ref": 1001}
]
```

These are example IDs; use actual IDs from the source catalog or pack metadata.
A file replacement can specify `file`, `palette`, `encoding`, `scale_divisor`,
`baseline`, `offset_x` and `mirror`. Paths in replacements are relative to that
JSON file. References inherit the target's image and positioning; mirror flags
combine with XOR. A `palette` on a reference recolours its existing index pixels
without storing another image. Missing IDs and reference cycles are errors. Pattern matching
currently selects **asset IDs at pack time**, not arbitrary City state at draw
time. It follows NewGRF's separation of images, bindings and selection without
implementing the NewGRF format or its callback VM.

## Binary command stream

Header: eight bytes `SC2KGFX\0`, followed by the integer format byte `1`.
Commands are one byte. Unsigned integers use canonical unsigned LEB128, at most
five bytes for u32. Signed X offsets use zigzag encoding. There are no per-record
strings, filenames, repeated dimensions on aliases, or reserved fixed-size slots.
Commands reference already-defined images, palettes or assets; the source packer
resolves arbitrary forward JSON references and rejects cycles.

| Opcode | Payload |
| --- | --- |
| 0 End | End definitions; domain metadata follows. |
| 1 Palette | ID, byte colour-count-minus-one, RGB triples. |
| 2 Image | ID, width, height, palette ID (zero means no private palette), payload byte count, run stream. |
| 3 Sprite | Asset ID, image ID, scale divisor, baseline, zigzag X offset, flags byte. |
| 4 Reference | Asset ID, target asset ID, flags byte. |
| 5 Reference range | First asset ID, count, target asset ID, flags byte. |
| 6 Water colours | Five little-endian RGB u32 values. |
| 7 Bridge palettes | Thirteen palette IDs in bridge-style order. |

Flags bit 0 mirrors horizontally. Bit 1 adds a palette ID immediately after the
flags byte, overriding the image/target palette (zero restores the global
namespace). This permits recolouring aliases without duplicating index pixels.
Other bits are rejected. Images are deduplicated
by actual dimensions and encoded pixel bytes; palettes are
shared by content. Repeated bindings become compact references/ranges. Assets
retain shared image/palette allocations, and Drawing borrows them through retained
references instead of copying all source pixels into its base cache. Scaled runs
retain the same palette. Drawing handles private/global palettes, RGB, alpha,
mirroring and animation; building/gameplay callers still request ordinary IDs.

Run streams contain height+1 little-endian row offsets and the existing compact
mode/length rows. Alpha is per pixel, not repeated in every RGB palette entry.
The whole command stream, pixel runs, row offsets and metadata are compressed
together by Zstandard. The decoder's window is bounded to 8 MiB.

The existing bounded smoke-emitter, construction-metadata and ground-material
sections follow the End command. Their layouts remain domain metadata rather
than instructions executed on every draw. Prior project archive formats need no
backward compatibility; stock SC2 save compatibility is unrelated to this format.
