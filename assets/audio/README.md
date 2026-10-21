# Native sound effects

All 30 legacy City sound IDs (500–529) and native bridge effect 531 use this project's own `sound-effects.dat` pack.
ID 530 remains the activation request and is not a recording.
There is no original SimCity WAV fallback, no sc2kfix audio alternative and no
runtime synthesis. The six active authored cues were synthesized offline, saved as
ordinary source WAVs in `generated/`, and baked into the pack. The remaining
recordings are curated OpenSFX samples and six Freesound excerpts. OpenSFX authors
and licenses are in `opensfx/credits.json`; their PCM waveforms are copied without
modification. Freesound authors, titles, source/preview URLs, licenses, input hashes
and editing recipes are in `freesound/sources.json` and `freesound/CREDITS.md`.
Those excerpts are trimmed, downmixed, resampled, normalized and faded offline.
The public HQ MP3 previews are used; authenticated originals were not downloaded.
Education placement uses a one-second recorded electric school bell with a short fade out.
The former synthesized `generated/school-bell.wav` is retained as source material but is no longer packed.

Only the native pack and combined `licenses.txt` ship in the app; the text file includes
mapping metadata, credits and license texts.
Loose source WAVs remain in the repository for editing. A normal build rebakes
`build/mac/generated-assets/audio/sound-effects.dat` only when source WAVs or the
mapping/baker change, or outputs are missing. Unchanged builds reuse it. The app
packages that generated pack and its mapping metadata. Builds never synthesize
sounds, import OpenSFX or download assets.
Newspaper/notice images likewise ship as the existing `illustrations.dat` pack;
their source PNGs remain development material.

## Development overrides and incremental bakes

Resolution for an ID is: loose source WAV, matching entry in the incremental
pack, then the base pack. An incremental pack contains only the IDs it replaces;
all other IDs continue to use the base pack. A present but invalid override fails
startup rather than silently selecting another sound.

Bundled PCM is used by default. Set `SC2K_AUDIO_OVERRIDES=/path/to/overrides` to enable
development sources (for example, this checkout's `assets/audio`), preserving
source paths such as `generated/fire.wav`. Unset it or use an empty directory to hear
only packaged PCM. No build-machine path is compiled into the game.
Loose WAVs must be mono, 16-bit little-endian PCM, 44,100 Hz.
Edits apply on the next application launch. All effects and the independent UI click
are loaded during startup; overlapping voices borrow the same retained recording.
No City RNG, event timing or volume controls change. The original replay and
single-effect behavior remains available with overlapping effects disabled.

Run from the checkout:

```sh
# Normal build: bake only if inputs changed, then package.
python3 tools/dev.py build

# Refresh the checked-in base pack/metadata when publishing source or mapping edits.
python3 tools/bake_sound_effects.py

# Bake only these IDs; the base pack stays untouched.
python3 tools/bake_sound_effects.py --ids 502 506 520

# Deliberately regenerate the six authored WAV sources and rebake.
python3 tools/bake_sound_effects.py --regenerate
```

`--ids` writes the ignored local `sound-effects.override.dat` and accompanying
JSON. Repeating it replaces that incremental pack; list all IDs you want included.
The runtime first looks for that pack in the development override root, then beside
the bundled base pack. A normal build includes a local incremental pack when present;
remove it before making a distribution that should use only the base sounds.
The CMake target `sc2k_sound_effects` can bake just the build pack without compiling
the game: `cmake --build build/mac --config Release --target sc2k_sound_effects`.
`--output PATH` can save a pack elsewhere. Importing upstream samples is a separate,
explicit maintenance step: `python3 tools/import_opensfx.py`.
The six Freesound WAVs can be recreated explicitly with `python3 tools/import_freesound.py`
(requires ffmpeg only for that import). It uses a local cache and pinned download hashes;
network access and conversion are never part of a normal build. Their CC0 / CC BY 4.0
licenses, authorship and editing notes are included in the app's combined `licenses.txt`,
alongside the existing CC0 text. The source documents remain in this directory.

## Release packaging

Every app build puts music and sound effects together in `audio.sc2assets`. Intro and graphics
use two other packs with the same indexed format, described in
[asset packaging](../../docs/asset-compression.md). Intermediate `sound-effects.dat` files and
loose MIDI tracks are build inputs, not shipped runtime resources. Default compression is
Zstandard level 1; `SC2K_COMPRESS_ASSETS=ON` selects level 15. The audio block is decoded once
into Assets during startup. Effect IDs resolve to borrowed PCM spans, and MIDI tracks are
passed from retained memory to CoreAudio. Windows uses SDL sound effects and a WinMM MIDI stream, also fed from retained asset memory. No file loading, disk caching or decompression
occurs when an effect starts. Explicit development sources and incremental sound overrides
retain their existing precedence.

## Native format

All numeric fields are little endian. The 24-byte header contains magic
`SC2KSFX\0` (8 bytes), entry count (u32), sample rate (u32), channel count (u16),
bits per sample (u16) and total file size (u32). Each 80-byte directory entry stores
City sound ID, absolute payload offset, PCM frame count and match kind (four u32s),
then a NUL-terminated development source path in 64 bytes. Entries are sorted by ID.
Match kind is 0 for a role match, 1 for an approximation, 2 for an authored cue.
The base pack has all 31 effect IDs; incremental packs have a subset. Shared recordings
reuse the same payload. Payloads are raw signed mono 16-bit PCM at 44,100 Hz.
The startup loader resolves effect IDs into the retained decoded pack, without allocating
a second PCM bank. The player does not read OpenTTD CAT/OBS formats.

## Overlapping playback

The Advanced setting **SimCity 2000 fixes → Overlapping sound effects** is on by
default in native play. Headless comparison keeps the original single-effect
controller. Up to eight voices can play together, plus the independent UI click
and music. Each voice has its own sample position and repetition count; stopping
one sound leaves other effects playing. Repeated loop requests keep their sample
clock, finite loops stop after the requested count, and repeated one-shots are
limited to two instances per sound with at least 100 ms between starts. If all
voices are occupied, a new effect can replace the oldest ordinary one-shot;
persistent loops and disaster warnings are protected.

SDL supplies signed 16-bit native-endian interleaved stereo at 44,100 Hz. Its
callback reads the original little-endian mono recordings, accumulates effects,
UI clicks and decoded music in wide registers, and writes into SDL's buffer.
An immediate-attack limiter with a gradual 100 ms release prevents clipping at
the final 16-bit conversion. Normal levels use a separate integer-only mixing
loop with a range check; gain multiplication and release calculations run only
while the limiter is active. Once gain recovers to within 0.009 dB of unity,
playback returns to the normal loop, including within the same callback.
A lone source with the limiter inactive has a separate direct copy/gain path:
stereo at full volume is copied, mono is expanded into SDL's stereo output, and
there is no per-frame voice scan or limiter range check. With zero active sources,
the limiter resets immediately to unity and SDL's buffer is zero-filled.
There is no separately allocated mixing buffer,
reserved 4 MiB block or converted stereo copy of each effect. Voice metadata is
256 bytes on a 64-bit host, at every map size. Recordings stay in Assets until shutdown;
voices borrow them without copying or allocating recordings at playback time.

The host uses SDL's device lock when changing voices. All native recordings have
already been loaded before the device starts. The callback performs no file
I/O, allocation, freeing or City/controller callbacks. Completed mixed voices
are reclaimed by the normal audio poll without issuing the old single-sound
completion command or restarting unrelated effects.

## Roles and approximations

`tools/bake_sound_effects.py` is the authoritative mapping code, with explicit
`approximation` markers for roles without a dedicated recording. `effects.json`
records source file hashes, frame counts and explanations for all IDs.

| ID | Role | Replacement | Kind |
| --- | --- | --- | --- |
| 500 | construction | OpenSFX short road/building placement cue | match |
| 501 | placement rejected | Negative year cue; no rejection recording | approximation |
| 502 | earthquake | Original low rumble and debris impacts | generated |
| 503 | tree or zone placement | Generic construction placement | approximation |
| 504 | explosion | Explosion | match |
| 505 | query | Generic interface beep | approximation |
| 506 | police or hospital | Original emergency vehicle wail | generated |
| 507 | fire | Original filtered noise and crackles | generated |
| 508 | bulldozer work | Bulldozer engine recorded by mr101986 | match |
| 509 | fire dispatch | Original alternating fire-engine tones | generated |
| 510 | helicopter flight | Helicopter | match |
| 511 | water | Water placement splat | match |
| 512 | demolition | Wooden house demolition recorded by relwin | match |
| 513 | civic placement | Original three-note completion chime | generated |
| 514 | power lines | Electric sparking | match |
| 515 | aircraft crash | Big crash; not an aircraft-specific recording | approximation |
| 516 | helicopter stage | Helicopter ignition recorded by Paul368; exact stage role unproved | approximation |
| 517 | ship | Ship horn | match |
| 518 | airplane takeoff | Jet takeoff | match |
| 519 | airplane landing | Plane wheels touching ground | match |
| 520 | disaster warning | Original slow civil-warning siren | generated |
| 521 | bus depot | Bus start | match |
| 522 | prison | Prison cell door recorded by kd_jack | match |
| 523 | education | Electric school bell recorded in a school hall by sbyandiji | match |
| 524 | train | Train departure horn | match |
| 525 | military dispatch | All units proceed to your post-assignments, recorded by klankbeeld | match |
| 526 | unknown 526 | Unidentified original role; neutral beep placeholder | approximation |
| 527 | sailboat or zoo | Outdoor bird cue shared by both roles | approximation |
| 528 | unknown 528 | Unidentified original role; neutral beep placeholder | approximation |
| 529 | terrain generation | Terrain placement; replaces the spoken announcement | approximation |
| 531 | bridge construction | OpenSFX bridge smithy/anvil cue | match |

Construction uses the same recordings as OpenTTD: `osfx_31.wav` (about 0.34 seconds)
for roads and ordinary non-rail construction, and `osfx_39.wav` for completed bridges.
The bridge effect has its own native ID so civic buildings retain their existing cue.
The jackhammer (`osfx_33.wav`) belongs to OpenTTD's ambient town roadworks animation,
not ordinary road placement; it is retained as an imported source but no longer mapped
to construction. Bridge timing uses its 2.43-second sample duration without extending
the legacy timing arrays. These sources remain unmodified; credits ship with the pack.

Upstream references: [road construction](https://github.com/OpenTTD/OpenTTD/blob/master/src/road_gui.cpp),
[bridge construction](https://github.com/OpenTTD/OpenTTD/blob/master/src/bridge_gui.cpp),
and [ambient roadworks](https://github.com/OpenTTD/OpenTTD/blob/master/src/road_cmd.cpp).
