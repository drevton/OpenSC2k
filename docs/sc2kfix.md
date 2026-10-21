# Optional fixes and settings

Open **Game Options → Advanced** (G). Category and type dropdowns filter a shared settings
registry; the text filter searches names, descriptions and categories. Groups expand/collapse,
mouse wheel, scrollbar and arrow/Page Up/Page Down keys scroll the list. Tooltips describe scope
and delayed effects. Reset all values restores interface defaults and turns optional fixes off.

Settings remain with City, Drawing, UI or Audio according to ownership. Changes to City fixes
use command133 (`CITY_COMMAND_SET_FIX`), not UI writes. Interface interaction state belongs to UI.
Preferences retain defaults for future cities; SC2X retains the active city's fix flags in metadata
and native resume data. Legacy SC2 has no field for these flags and uses the current preferences.
Older SC2X native resumes remain readable and retain stock behavior. Unrecognized fix bits fail
validation. Stock checkpoint export rejects enabled optional City fixes; no oracle equality is
claimed for repaired behavior.

## Comparison hydro valuation repair

The comparison baseline explicitly repairs stock hydro kind198 asset value from $4000 to $400,
matching kind199. `NativeStockVerifier` and the isolated valuation check select
`call_stdcall(..., repair_hydro_value=True)`; generic Unicorn calls remain unpatched by default.
The guarded hook replaces only the hydro multiply result at `0x46A378`. It leaves the on-disk
executable, x86 code bytes and shared $4000 coal construction price unchanged. Reports record
`original_bug_repairs.hydro_asset_value`; these runs are repaired-oracle evidence, not pristine
stock valuation equality. Native hydro valuation is now $400 even with the broader asset fix off.

## Source and current support

Research baseline: [sc2kfix](https://github.com/sc2kfix/sc2kfix/tree/bc1a7cfca505ed73d005ad3401df8cdbad368027),
commit `bc1a7cfca505ed73d005ad3401df8cdbad368027`. Algorithm corrections informed by its
`hooks/hook_graphscanstats.cpp`, `hooks/hook_sc2k1996_miscellaneous.cpp`,
`hooks/hook_terrainhandling.cpp`, `hooks/hook_tilegrowthorplacement.cpp`, and `modules/military.cpp`.
Copyright OpenCity Foundation, MIT; [license](licenses/sc2kfix.txt) also ships in the Mac app.

Independently switchable implementations:

- City assets: subway value, power plant tool costs, highway costs and arcology costs.
- Four-tile treatment-plant valuation, separate from the general asset correction.
- Daily city asset recalculation.
- Industrial rail/highway neighbor connection recount when enabled and on future loads.
- Military runway orientation using military counts rather than civilian airport counts.
- Silo plots with individual level elevations, no overlap and complete underground clearance.
  This adopts the placement corrections, not upstream's entire military replacement algorithm.
- Rubble at both demolished tunnel entrances.
- Complete deletion of label/facility-name storage.
- Demolition effects using the anchor tile's water/land elevation.

Existing native equivalents include palette cycling on truecolor displays, staged sign editing
(cancel does not erase the sign), retained city/mayor names, OpenMSX event music, and
native fonts rather than the original sign-dialog font selection.

The sc2kfix replacement-art pack, its five sprite-repair settings and three hangar variants have
been removed. Seasonal/weather palette effects and the four cosmetic terrain palettes have also
been removed. Native baked artwork supplies sprite graphics.

Additional independently switchable native implementations:

- Preserve terrain above buried utilities: subway and pipe placement keep surface slopes and water beds intact.
  Quoted and charged costs omit grading. This extends the existing water-preservation setting and keeps its saved bit.
- Bond offers use the debt-to-assets credit rating rather than the hardcoded rate.
- Military offers retry unsuitable locations; army bases receive internal road strips, and
  coastal naval yards can be placed. Searches are bounded and use City RNG. This is a native
  adaptation, not byte-for-byte reproduction of upstream's host-rand/rotation-dependent search.
- Military growth restores army-base Top Secret buildings and the upstream parking-lot/hangar/building
  ratios, retaining hangar fallback when the selected building cannot fit. It also rejects roads,
  silo footprints and occupied underground space where required.
- Demolition clears obsolete power capability bits on cleared land.
- A city-center cross on the land-value overlay.
- Sound effects now use the native [sound asset pack](../assets/audio/README.md). The former five
  sc2kfix audio alternatives and their settings have been removed.
- Save backups for both SC2 and SC2X: the exact host destination is copied to `.bak` before a
  truncating open. Backup failure stops the save before touching that destination.

- The host window title stays `OpenSC2k`; city details remain in the game interface.
- Floating status visibility, middle-button centering and Alt-click advanced tile inspection.
  Advanced inspection reports native layer values without comparing UI records.

Windows installer, registry, Steam launch, MFC dialogs/timers and SCURK fixes target components
this native app does not run. Original intro/WillTV playback and libsmacker are excluded at the user’s request; future videos will be new assets.
The upstream DLL update checker has no native app release channel to check, so it is not presented as an app updater.
Native C/C++ DLL mod loading and arbitrary executable-memory inspection likewise do not apply to this engine.

- Correct saved map position uses the actual window center and zoom, including the missing-window default.
- Zero-length FORM header recovery uses the actual .sc2 stream length without modifying the source file.
  Other malformed lengths and damaged chunks keep ordinary load behavior. This is a separate file preference.

## Focused parity audit (2026-10-10)

The target is equivalent gameplay fixes, as requested, rather than identical upstream RNG/state outcomes.
The audit found and repaired additional omissions: shared placement rejected military zones even with the
military-growth option enabled; failed placement could still report success; legacy saves used X twice for
the saved viewport; zero-length FORM headers could stop loading after the first chunk.

Release compilation and `check_city_fixes` passed. This headless native check covers all 256 tile IDs with
counts 0–33 against independently transcribed upstream valuation rules, bond-rate limits, all 256 tile IDs
for industrial connections, every ordinary label slot, 528 army growth-count combinations with actual
placement, map-edge military placement, blocked silo/building footprints, runway orientation across all
four rotations, invention-command admission and clearing, all three saved zoom levels, and header recovery
including truncated chunks, unsupported extensions, unchanged source bytes and the disabled/nonzero paths.
All eight repaired sprite archives also match the pinned upstream files byte for byte.

These checks establish the listed behavior. They do not establish complete gameplay parity across every
branch, long-running disaster sequence, naval placement layout, renderer or audio path. Military search
and cheats remain the documented native adaptations. Original videos and Windows-only components remain
excluded. No full stock replay or visible UI test was run for this audit.

## Developer tools

Options → Developer console opens the native canvas console. The key left of 1 toggles it;
Ctrl+F12 also opens it once enabled. `help` lists commands.
Lua scripting has a separate Advanced switch. `run path.lua` executes a text script; ordinary input is
Lua code. `city.summary()`, `city.tile(layer, tile)` and `city.tilexy(x, y)` inspect native state.
`city.command(id, tile, a, b, c)` calls the same command path as the UI and returns result and error.
No executable addresses, arbitrary memory writes or OS/file libraries are exposed. Each evaluation
has a one-million-instruction budget. The Game owns and releases its Lua interpreter.

Cmd+V on macOS, Ctrl+V elsewhere, or Shift+Insert pastes into the prompt without running it.
Pasted line breaks and tabs become spaces in the single-line prompt; Enter executes the result.
Up/Down browse the last 32 commands from this app run, skipping consecutive duplicates. Down past
the newest entry restores the unfinished input from before browsing. History survives closing the
console and `clear`, but is not saved to disk.
The existing bounded output buffer supports the mouse wheel, Page Up/Down, Ctrl+Home/End and a
draggable scrollbar. Scrolling counts wrapped display rows, so long lines do not hide newer output.
Submitting a command returns to the newest output; `clear` clears output without removing history.

`bind K B` assigns K to the existing budget shortcut; `unbind K` restores K; `bindings` lists overrides.
Bindings are stored in `bindings.slotN` entries in `~/.opensc2k/config.toml`. Remapping applies to the map's
shortcuts; text editors and dialog answers retain their controls. Escape and F12 are reserved.

With Restored cheat codes enabled, `cheat CODE` accepts fund, cass, imacheat, noah, moses, newhouse,
mrsoleary, priscilla, gilmartin, joke, webb, damn, darn, heck and mrsdoyle. Native adaptations use City
RNG, a bounded house-placement search and native notices. The joke uses new text. Church growth and
the house anchor persist in SC2X; legacy SC2 cannot retain that extra state. `inventions` requires its
independent repair switch and clears all invention years before rebuilding availability. Cheated state
is rejected by pristine checkpoint export even if its switches are subsequently disabled.

Improved message wording is independently configurable and borrows immutable replacement strings.
Lua source and its MIT license are included under thirdparty/lua; the license ships in the app.

## Music

Music uses the packaged MIDI tracks and the selected system or custom soundfont.
Master, music and effects have separate volume controls.
Music in background can mute music on focus loss without changing simulation cadence.

## Diagnostics

Diagnostic log appends native errors and console output to `sc2k.log` beside config.toml.
Crash diagnostics independently records fatal signals and fault addresses, then leaves the full
crash report to the operating system. Both switches take effect immediately and restore the prior
stderr destination and signal handlers when disabled or on normal shutdown. No telemetry is sent.


Custom MIDI soundfont accepts SF2/DLS banks through the native CoreAudio instrument.
Set a path with `soundfont /path/to/bank.sf2` in the developer console; `soundfont default`
restores the system bank. The independent Advanced switch reuses the chosen path.
Master volume and the chosen bank persist in `~/.opensc2k/config.toml`; see [configuration](configuration.md).
Always save SC2X is an independent file-format preference; cities requiring extended storage
continue to use SC2X when it is off.

## Native equivalents and exclusions

| Upstream feature | Native implementation |
| --- | --- |
| Real-time growth rendering | Explicit tile damage renders changes on the next presentation frame. |
| New City preview, names and terrain variety | Existing generator preview, mayor/name editors and terrain feature controls. |
| Music selection, shuffle and track restoration | Jukebox with all 19 original tracks, playlists and shuffle. |
| MIDI driver and soundfont | Native CoreAudio synthesis with configurable SF2/DLS banks; no Windows driver layer. |
| Portable settings and save format preference | Native preference files and optional SC2X default. |
| Original intro/WillTV and movie scaling | Excluded by request; no libsmacker dependency or original video playback. |
| Installer, registry, launch repair and SCURK | Components absent from the native app. |
| Win32 DLL hooks, address inspector and injected C/C++ mods | Replaced by native engine interfaces and the command-based Lua API. |
| Upstream DLL update checker | Excluded: its releases cannot update this native executable. |

This is feature adaptation to the native engine, not binary compatibility with the sc2kfix plugin.
