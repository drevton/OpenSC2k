# Configuration

The game reads `~/.opensc2k/config.toml` once at startup. Display, advanced options,
local gameplay preferences, audio, playlists and console key bindings share this file.
The old options, display, audio, jukebox and keybindings files are no longer read.
Set `OPENSC2K_CONFIG_FILE` to use a different config filename, including an absolute
path, `~/…`, or a path relative to the directory where the game was launched.
Config and log paths allocate their text rather than imposing a fixed path capacity.

Cities default to `~/.opensc2k/Cities`. Optionally set `files.save_directory` to
an absolute path, `~/…`, or a path relative to the config file's directory.
An omitted or empty value selects the default. Missing directories are created.

The format is deliberately small: one dotted key and scalar value per line. It uses
UTF-8, `#` comments, quoted strings, decimal signed 64-bit integers and `true`/`false`.
Boolean settings also accept 0 and 1. Strings support `\"`, `\\`, `\n`, `\r` and `\t`.
Tables, arrays, dates, floating point and multiline strings are unsupported; this is
not a full TOML implementation. Blank lines, spacing, line endings, comments and
unknown keys survive writing. An edited setting replaces only its value.
Duplicate keys, malformed text, invalid UTF-8 and invalid known values reject the
configuration. The game keeps the file untouched and uses its defaults for that run.

```toml
# Scale is a percentage. Automatic scale takes priority when enabled.
files.save_directory = "~/Games/OpenSC2K/Cities"

display.automatic = false
display.scale = 200
interface.scrollwheel = 1 # 0 = scroll, 1 = zoom, 2 = off

audio.master_volume = 80
music.playing = true
music.playlist1 = "1,4,12" # Track indices 0–30; at most 32 entries.

fixes.asset_values = true
game.tree_lifecycle = false
game.protect_terrain_structures = true
bindings.slot1 = "K B" # Console equivalent: bind K B
```

Missing settings inherit defaults and are appended to the document. Changes update
memory immediately and are written together after three seconds; shutdown flushes
any pending edits. The writer uses an exclusive temporary file in the same directory
and replaces the destination after successful output. Failed writes retain pending
changes and retry after 30 seconds, with another attempt at exit.

City state remains authoritative in saved games. The configured simulation fixes
supply stock-city defaults; native saves retain their saved fixes. Other City options
in this file supply defaults for newly created cities. Changing a shared option in
the UI runs its resolved City command before recording the local default. Reading or
writing the config inside a City command is forbidden. Temporary dialog pauses do
not update the configured speed. Terrain tool choices and simulation batching are local
UI/Game preferences; city saves do not store them.

On macOS, two-finger map panning uses AppKit's precise scrolling distance in screen
points, converted to framebuffer pixels for the current display. Small fractions
accumulate in both the main map and additional viewports. Mouse wheels retain their
step-based movement; the scrollwheel setting still selects scrolling, zooming or off.
Pinch zoom and the existing map scroll direction are unchanged.
The host re-enables AppKit momentum after SDL2 initializes, so flicks continue with
the system's decaying scroll events after finger release. This startup default is
process-local; it does not write macOS preferences or add a separate camera animation.

## Adding an advanced option

Add an entry to `kCitySettings` in `src/game/options.c`. Its label, tooltip, group,
config key, default, owner, storage type, offset and value range describe both its UI
and persistence. The shared descriptor type lives in `src/game/settings.h`.
`maximum = 1` gives a checkbox. An immutable `choices` array gives a
dropdown; integer values without choices cycle from `minimum` to `maximum`. `action` is
optional for a new advanced-only setting: the row index already identifies it.
No separate count, getter switch, toggle branch or serialization entry is needed.

Use `hidden` for scalar settings that already have dedicated controls; they still
use the same configuration loader, writer and validation. `preserve_initial` keeps
the initialized field value when its key is absent.

Use `mask` for flags and `inverted` for fields whose stored meaning is reversed.
The `effects` flags request redraw, display synchronization, volume updates or other
existing presentation work. Options requiring specialized operations, such as
switching fullscreen or selecting a soundfont, retain explicit application handling.
A City setting specifies its `command`; setters never write directly through its
City field binding. Fix descriptors also specify their fix ID in `parameter`.

The generic row renderer, hit testing, search, tooltips, Reset All, validation,
configuration loading and saving all consume this table. New simulation behavior
still needs its actual City command and implementation.
