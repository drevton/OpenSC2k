# OpenSC2k

A native city simulator written in C, with integer simulation, software rendering and SDL2 presentation.
This repository contains the standalone game, native artwork and audio, and the tools that bake its resource packs.
It includes no original SimCity game files, comparison adapters, executable extracts or development captures.
Existing SC2 city files can still be imported through the game's file codecs.

## Build and play

Requires a C11 compiler, CMake 3.21 or newer, SDL2, zlib, Zstandard's `zstd` executable, Python 3 and Pillow.
On macOS, install Xcode command-line tools and the dependencies, then run:

```sh
python3 -m pip install Pillow
python3 tools/dev.py build
open build/mac/Release/sc2k.app
```

`Play.command` opens that application. `Debug.command` enables the debug city options.
On Linux, the same build helper writes the executable to `build/native/sc2k`.
CMake builds the game directly as well:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --target sc2k -j6
```

Windows cross-builds use `tools/windows-x64.cmake`; `python3 tools/dev.py build-windows --help`
lists the SDL2 and zlib SDK arguments.

## Assets

Normal builds incrementally pack the checked-in source PNGs, WAVs, MIDI files and manifests.
They perform no downloads and need no original game installation. The player loads the resulting
`intro.sc2assets`, `graphics.sc2assets` and `audio.sc2assets` packs.
For release compression, configure with `-DSC2K_COMPRESS_ASSETS=ON` to use Zstandard level 15.
Each archive has a compressed index and one or more compressed content blocks;
see [asset packs](docs/asset-compression.md).

Asset source paintings, active bake recipes, prompts, provenance and third-party licenses accompany the artwork.
Discarded studies and external reference screenshots are omitted. Approved building recipes use the
`approved-original.png` and `approved-foreground.png` sources; the runtime manifests select the finished views.
The packaging tools use those finished views directly, so installing a compiler does not require Blender.
Optional authoring tools use Pillow and NumPy (`tools/requirements-artwork.txt`); vehicle model rendering needs Blender.
OpenGFX2/OpenSFX/OpenMSX import scripts are explicit authoring operations for those open asset projects.
Intro and app-icon sources live in `assets/app/sources/`.

See [audio](assets/audio/README.md), [fonts](assets/fonts/interface-font.md),
[buildings](assets/landscape/buildings/README.md), [terrain](assets/landscape/opengfx2/README.md),
[advisors](assets/ui/advisors/README.md) and [neighbors](assets/ui/neighbors/README.md).
Licenses and attribution are retained beside third-party code and assets and packaged into `licenses.txt`.

## Source organization

- `src/city/`: deterministic simulation, terrain generation and city-file codecs.
- `src/game/`: game lifetime, scheduling, commands and settings.
- `src/drawing/`: rendering, palettes, sprites, clipping and damage.
- `src/ui/`: input, tools, windows and platform integration.
- `src/audio/`: playback and mixing.
- `src/assets/`: immutable resource loading and catalogs.
- `src/base/`: common numeric, text, file and archive utilities.
- `assets/`: native artwork, fonts, music, effects and source metadata.
- `tools/`: builds, asset generation and packing.
- `thirdparty/`: vendored Lua and Zstandard sources with their licenses.

See [tile storage](docs/tile-storage.md), [configuration](docs/configuration.md),
[commands](docs/commands.md) and [native saves](docs/sc2k-native.md) for the runtime contracts.

## Settings

Settings opened from the intro edit global defaults for new cities. Inside a city, gameplay settings edit
that city and are saved with it. The scope menu switches between city settings and global defaults and
can copy gameplay settings in either direction. Display, interface and audio preferences remain global.
See [configuration](docs/configuration.md) for details.
