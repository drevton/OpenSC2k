# Asset packs

Every build ships three resource packs, all using the same indexed binary format:

- `intro.sc2assets`: loading-screen artwork, loaded independently before other assets.
- `graphics.sc2assets`: sprites, illustrations, icons and fonts.
- `audio.sc2assets`: sound effects and MIDI music together.

The executable icon and SDL library are ordinary package files. Third-party license texts,
credits and source metadata are combined in one UTF-8 `licenses.txt`; original license wording
is retained, and structured attribution metadata is rendered as readable text.
Source artwork, source WAVs and intermediate bake formats remain authoring inputs; they are not
runtime resource files. Native startup never selects loose checkout files when a pack is missing
or corrupt. Explicit graphics/audio development overrides retain their existing startup-only behavior.
The retired `SC2KRES1` extracted-executable archive loader has been removed from native code.
Original resources for diagnostic comparison remain confined to the optional comparison adapters.

## Shared format and loader

`tools/package_assets.py` writes all three packs. `src/assets/asset_file.c` reads them through
one archive loader and validates the index, IDs, category membership, ranges, decoded lengths,
checksums and complete input consumption. All fields are little endian. The 32-byte header is:

| Offset | Field |
| --- | --- |
| 0 | Eight bytes `SC2KPAK1` |
| 8 | Compressed index size, u64 |
| 16 | Decoded index size, u64 |
| 24 | Asset entry count, u32 |
| 28 | Content block count, u32 |

The Zstandard-compressed index starts with sorted 24-byte entries: asset ID (u32), content block
(u32), decoded offset (u64) and size (u64). It ends with one 24-byte record per block: compressed
offset, compressed size and decoded size (all u64). Offsets are relative to the first content byte,
immediately after the index. Blocks are contiguous complete checksummed Zstandard frames, with
an 8 MiB maximum decoder window. Numeric IDs come from `src/assets/asset_catalog.inc`.

The default is one content block per pack. Optional `SC2K_ASSET_BLOCK_LAYOUT` specifies a build-time
layout with `intro`, `graphics` and `audio` keys; each member name appears exactly once in its pack.
Fonts, sprite commands, PCM and MIDI need different in-memory decoders but share file I/O and pack
indexing. Their typed payloads are retained within indexed ranges, not opened as separate files.

## Incremental builds and compression

CMake rebakes only changed source categories. Packaging uses Zstandard level 1 for fast development
builds; `SC2K_COMPRESS_ASSETS=ON` selects level 15 with the same file format and loader. A host `zstd`
command is required to build; users of the app need no installed Zstandard library. The decoder is
vendored and statically linked, and its license ships with the package.

Packaging caches input timestamps/sizes and layouts under the build directory's `packed-assets`.
An unchanged pack is copied from cache; changed packs reuse unchanged content blocks by SHA-256.
Code-only builds do not recompress assets. Known obsolete loose runtime binaries and the previous
`music.sc2assets` / `sounds.sc2assets` files are removed. Scattered attribution files from older
builds are replaced by `licenses.txt`; their source originals remain in the repository.

## Ownership and loading

Assets retains decoded blocks until shutdown. The intro pack loads first for the loading screen;
full startup reuses it and loads graphics/audio before play. This reorganizes the existing roughly
84 MiB of decoded assets, independent of map size; it adds no duplicate runtime asset buffer.
Graphics parsers read bounded memory cursors. Effect IDs resolve to borrowed PCM; CoreAudio and WinMM MIDI playback borrow retained track bytes. Drawing, audio callbacks and gameplay do no resource file I/O.

```c
ByteSpan GetAsset(CityAssets *assets, u32 id);
```

This inline array lookup does no allocation, decompression, searching or locking. Assets also owns
font glyph images and authored sprite variants; Drawing retains generated render caches. Optional
SoundFont user files and imported city/tile-set files keep their own host-file codecs.
