#include "game/commands.h"
#include "ui/windows/controls.h"
#include "city/engine/city_types.h"
#include <stddef.h>

#include "game/state.h"
#include "audio/state.h"
#include "city/state.h"
#include "drawing/state.h"
#include "assets/state.h"
#include "ui/state.h"
#include "ui/dialogs/game_options.h"

static const CityDropdownItem kScrollwheelChoices[] = {
  {"Scroll map", CITY_SCROLLWHEEL_SCROLL, CITY_DROPDOWN_PLAIN},
  {"Zoom map", CITY_SCROLLWHEEL_ZOOM, CITY_DROPDOWN_PLAIN},
  {"Off", CITY_SCROLLWHEEL_OFF, CITY_DROPDOWN_PLAIN},
};

// One schema supplies labels, tooltips, defaults, storage, City commands and config keys.
const CitySetting kCitySettings[] = {
  {"Individual facility records", "Give every facility its own microsim record, name and chronological age. "
      "Off restores stock shared records and the 150-record limit for comparisons.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_INDIVIDUAL_FACILITIES,
      .default_value = true, .key = "game.individual_facilities", .owner = SETTING_CITY, .type = SETTING_BOOL,
      .offset = offsetof(City, individual_facilities), .maximum = 1,
      .command = CITY_COMMAND_SET_INDIVIDUAL_FACILITIES,
  },
  {"Spread simulation across frames", "Process large-city growth, environment, power and water in batches. "
      "Preserves scan order and random numbers. Local performance setting; "
      "128 x 128 cities keep the original schedule.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_SIMULATION_BATCHING,
      .default_value = false,
      .key = "game.spread_simulation", .owner = SETTING_GAME, .type = SETTING_BOOL,
      .offset = offsetof(Game, spread_simulation), .maximum = 1,
  },
  {"OpenTTD tree birth and death", "Trees grow, die, spread nearby and seed empty land using an independent "
      "random stream. Changes simulation; saved with this city. Off keeps the original tree simulation.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_TREE_LIFECYCLE,
      .default_value = false,
      .key = "game.tree_lifecycle", .owner = SETTING_CITY, .type = SETTING_BOOL,
      .offset = offsetof(City, tree_lifecycle), .maximum = 1,
      .command = CITY_COMMAND_SET_TREE_LIFECYCLE, .parameter = 0,
  },
  {"Require clearing before terraforming", "Refuse terrain changes that would remove roads, railway tracks, "
      "power lines, buildings or underground construction. Show the reason and mark the blocking tile. "
      "Changes simulation; saved with this city.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_TERRAIN_PROTECTION,
      .default_value = true,
      .key = "game.protect_terrain_structures", .owner = SETTING_CITY, .type = SETTING_BOOL,
      .offset = offsetof(City, protect_terrain_structures), .maximum = 1,
      .command = CITY_COMMAND_SET_TERRAIN_PROTECTION, .parameter = 0,
  },
  {"Landscape individual corners", "Raise or lower a shared terrain corner, like OpenTTD. Off edits the entire "
      "tile diamond. Local tool choice; included in each landscaping action.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_TERRAIN_POINT_EDIT,
      .default_value = false,
      .key = "interface.terrain_point_edit", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, terrain_point_edit), .maximum = 1,
  },
  {"Automatically fill lowered land with seawater", "Fill land at or below sea level with water after "
      "landscaping. Off keeps newly lowered land dry. Local tool choice; included in each landscaping action.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_TERRAIN_SEAWATER,
      .default_value = true,
      .key = "interface.terrain_seawater", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, terrain_no_seawater), .maximum = 1, .inverted = true,
  },
  {"Auto-detect interface size", "Choose interface size automatically for the display.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_AUTO,
      .default_value = true,
      .key = "display.automatic", .owner = SETTING_DISPLAY, .type = SETTING_I32,
      .offset = offsetof(DrawingOptions, automatic), .maximum = 1,
      .effects = SETTING_SYNC_DISPLAY,
  },
  {"Snap windows", "Snap dragged windows to nearby windows and screen edges.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_SNAP,
      .default_value = true,
      .key = "display.snapping", .owner = SETTING_DISPLAY, .type = SETTING_I32,
      .offset = offsetof(DrawingOptions, snapping), .maximum = 1,
      .effects = SETTING_SYNC_DISPLAY,
  },
  {"Scale bevels", "Scale window and control borders with the interface.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_BEVELS,
      .default_value = true,
      .key = "display.scale_bevels", .owner = SETTING_DISPLAY, .type = SETTING_BOOL,
      .offset = offsetof(DrawingOptions, scale_bevels), .maximum = 1,
      .effects = SETTING_REDRAW_INTERFACE,
  },
  {"Traditional sprite font", "Use OpenTTD-style sprite glyphs. Off selects scalable Noto Sans.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_SPRITE_FONT,
      .default_value = true,
      .key = "display.sprite_font", .owner = SETTING_DISPLAY, .type = SETTING_BOOL,
      .offset = offsetof(DrawingOptions, sprite_font), .maximum = 1,
      .effects = SETTING_REDRAW_INTERFACE,
  },
  {"Anti-alias scalable fonts", "Smooth scalable font edges. Only available when the sprite font is off.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_FONT_ANTIALIAS,
      .default_value = true,
      .key = "display.font_antialias", .owner = SETTING_DISPLAY, .type = SETTING_BOOL,
      .offset = offsetof(DrawingOptions, font_antialias), .maximum = 1,
      .effects = SETTING_REDRAW_INTERFACE,
  },
  {"Fullscreen", "Switch between a desktop window and fullscreen.",
      .group = CITY_SETTINGS_DISPLAY, .action = GAME_OPTIONS_FULLSCREEN,
      .default_value = false,
      .key = "display.fullscreen", .owner = SETTING_DISPLAY, .type = SETTING_I32,
      .offset = offsetof(DrawingOptions, fullscreen), .maximum = 1,
      .effects = SETTING_SYNC_DISPLAY,
  },
  {"Music", "Play or stop the selected jukebox programme.",
      .group = CITY_SETTINGS_SOUND, .action = GAME_OPTIONS_MUSIC_ENABLED,
      .default_value = true,
      .key = "music.playing", .owner = SETTING_JUKEBOX, .type = SETTING_I32,
      .offset = offsetof(Jukebox, playing), .maximum = 1,
  },
  {"Sound effects", "Enable construction, vehicle and other city sounds.",
      .group = CITY_SETTINGS_SOUND, .action = GAME_OPTIONS_EFFECTS_ENABLED,
      .default_value = true,
      .key = "audio.enabled", .owner = SETTING_AUDIO, .type = SETTING_U32,
      .offset = offsetof(CityAudioState, enabled), .maximum = 1,
  },
  {"Ambient sounds", "Play routine airplane, helicopter, ship, train and bird sounds, traffic and distant sirens, "
      "and explosions that do not trigger disasters. Construction sounds and disaster warnings remain enabled.",
      .group = CITY_SETTINGS_SOUND, .action = GAME_OPTIONS_AMBIENT_SOUNDS,
      .default_value = true,
      .key = "audio.ambient_sounds", .owner = SETTING_AUDIO, .type = SETTING_BOOL,
      .offset = offsetof(CityAudioState, ambient_sounds), .maximum = 1,
      .effects = SETTING_STOP_EFFECTS,
  },
  {"Overlapping sound effects", "Mix up to eight city effects, keeping loops independent of construction "
      "and explosion sounds. Stops affect only the requested sound. Off restores the single-effect controller.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_OVERLAPPING_EFFECTS,
      .default_value = true,
      .key = "audio.overlapping_effects", .owner = SETTING_AUDIO, .type = SETTING_BOOL,
      .offset = offsetof(CityAudioState, overlapping_effects), .maximum = 1,
      .effects = SETTING_STOP_EFFECTS,
  },
  {"Correct city asset values", "sc2kfix: value subways, power plants, highways and arcologies using tool costs. "
      "Changes simulation and bond limits. Off preserves stock calculations.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_ASSET_VALUES,
      .default_value = true,
      .key = "fixes.asset_values", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_ASSET_VALUES,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_ASSET_VALUES,
  },
  {"Water treatment asset area", "sc2kfix: water treatment plants occupy four tiles, not nine. "
      "Use the correct area when calculating city assets.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_TREATMENT_VALUE,
      .default_value = true,
      .key = "fixes.treatment_value", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_TREATMENT_VALUE,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_TREATMENT_VALUE,
  },
  {"Update assets every day", "sc2kfix: recalculate city asset value each simulation day rather than "
      "waiting for the normal statistics refresh.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_DAILY_VALUATION,
      .default_value = true,
      .key = "fixes.daily_valuation", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_DAILY_VALUATION,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_DAILY_VALUATION,
  },
  {"Restore loaded neighbor links", "sc2kfix: rebuild the industrial bonus for rail and highway connections "
      "now and when loading a city.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_LOADED_CONNECTIONS,
      .default_value = true,
      .key = "fixes.loaded_connections", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_LOADED_CONNECTIONS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_LOADED_CONNECTIONS,
  },
  {"Military runway orientation", "sc2kfix: choose military runway orientation using military runway counts, "
      "independently of civilian airports.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_MILITARY_RUNWAYS,
      .default_value = true,
      .key = "fixes.military_runways", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_MILITARY_RUNWAYS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_MILITARY_RUNWAYS,
  },
  {"Separate missile silo plots", "Allow each silo plot its own level altitude, reject overlapping plots, "
      "and check underground clearance across the entire plot. Takes effect on future offers.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_SILO_PLOTS,
      .default_value = true,
      .key = "fixes.silo_plots", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_SILO_PLOTS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_SILO_PLOTS,
  },
  {"Tunnel demolition rubble", "sc2kfix: leave rubble at both demolished tunnel entrances instead of empty land.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_TUNNEL_RUBBLE,
      .default_value = true,
      .key = "fixes.tunnel_rubble", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_TUNNEL_RUBBLE,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_TUNNEL_RUBBLE,
  },
  {"Clear deleted label storage", "sc2kfix: clear the entire fixed name slot when removing a label or facility name.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_LABEL_CLEANUP,
      .default_value = true,
      .key = "fixes.label_cleanup", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_LABEL_CLEANUP,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_LABEL_CLEANUP,
  },
  {"Bond credit rating", "sc2kfix: calculate offered bond interest from outstanding bonds and current city assets.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_BOND_RATING,
      .default_value = true,
      .key = "fixes.bond_rating", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_BOND_RATING,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_BOND_RATING,
  },
  {"Military base placement", "sc2kfix: retry unsuitable sites and restore army roads and coastal naval bases. "
      "Affects future military offers.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_MILITARY_PLACEMENT,
      .default_value = true,
      .key = "fixes.military_placement", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_MILITARY_PLACEMENT,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_MILITARY_PLACEMENT,
  },
  {"Military building growth", "sc2kfix: restore army Top Secret buildings and building ratios; prevent military "
      "buildings from overlapping roads, runways, docks, silos and occupied underground tiles.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_MILITARY_GROWTH,
      .default_value = true,
      .key = "fixes.military_growth", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_MILITARY_GROWTH,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_MILITARY_GROWTH,
  },
  {"Demolition power cleanup", "sc2kfix: clear obsolete powered/powerable flags after demolition on land, "
      "including power-line tunnel entrances.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_POWER_CLEANUP,
      .default_value = true,
      .key = "fixes.power_cleanup", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_POWER_CLEANUP,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_POWER_CLEANUP,
  },
  {"Charge for clearing grass", "Charge one coin per tile when bulldozing empty grass. When off, clearing grass "
      "is free. Cleared ground becomes brown and gradually recovers.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_FIX + CITY_FIX_GRASS_CLEARING_COST,
      .default_value = false,
      .key = "fixes.grass_clearing_cost", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_GRASS_CLEARING_COST,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_GRASS_CLEARING_COST,
  },
  {"Restored cheat codes", "Enable cheats from the DOS, Mac and demo releases. Enter codes with cheat CODE in the "
      "developer console. Cheated cities are excluded from pristine executable comparison.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_RESTORED_CHEATS,
      .default_value = true,
      .key = "fixes.restored_cheats", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_RESTORED_CHEATS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_RESTORED_CHEATS,
  },
  {"Building construction stages", "Choose the eventual zoned building when construction starts and show "
      "foundations, structure and finishing as work progresses. Off starts new sites using stock construction.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_BUILDING_CONSTRUCTION,
      .default_value = true,
      .key = "fixes.building_construction", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_BUILDING_CONSTRUCTION,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_BUILDING_CONSTRUCTION,
  },
  {"On-ramp road connections", "Connect roads to the facing of an on-ramp. Off retains stock four-way "
      "road ends at on-ramps.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_RAMP_CONNECTIONS,
      .default_value = true,
      .key = "fixes.ramp_connections", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_RAMP_CONNECTIONS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_RAMP_CONNECTIONS,
  },
  {"Repair Add All Inventions", "The inventions command clears every invention year and rebuilds tool availability.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_ALL_INVENTIONS,
      .default_value = true,
      .key = "fixes.all_inventions", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_ALL_INVENTIONS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_ALL_INVENTIONS,
  },
  {"Black map background", "Use black instead of grey around the city map in all viewports.",
      .group = CITY_SETTINGS_DISPLAY, .action = GAME_OPTIONS_BLACK_BACKGROUND,
      .default_value = true,
      .key = "display.black_background", .owner = SETTING_DISPLAY, .type = SETTING_BOOL,
      .offset = offsetof(DrawingOptions, black_background), .maximum = 1,
      .effects = SETTING_REDRAW_MAP,
  },
  {"Land-value city center", "Show the calculated city center on the land-value map.",
      .group = CITY_SETTINGS_DISPLAY, .action = GAME_OPTIONS_DRAWING_FIX + DRAWING_FIX_CITY_CENTER,
      .default_value = false,
      .key = "display.city_center", .owner = SETTING_DISPLAY, .type = SETTING_MASK,
      .offset = offsetof(DrawingOptions, fixes), .maximum = 1, .mask = 1u << DRAWING_FIX_CITY_CENTER,
      .effects = SETTING_REDRAW_INTERFACE,
  },
  {"Advanced Alt-query", "Hold Alt when querying a tile to see map layer values and simulation flags.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_ADVANCED_QUERY,
      .default_value = false,
      .key = "interface.advanced_query", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, advanced_query), .maximum = 1,
  },
  {"Middle-click centering", "Center the main city map on the tile beneath the middle mouse button.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_MIDDLE_CENTER,
      .default_value = false,
      .key = "interface.middle_center", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, middle_center), .maximum = 1,
  },
  {"Function of scrollwheel", "Choose Zoom map, Scroll map or Off for mouse-wheel and two-finger scrolling. "
      "This interface setting affects all cities. Default: Zoom map. Pinch gestures still zoom.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_SCROLLWHEEL,
      .default_value = CITY_SCROLLWHEEL_ZOOM,
      .key = "interface.scrollwheel", .owner = SETTING_UI, .type = SETTING_U8,
      .offset = offsetof(CityInterfaceState, scrollwheel_mode), .maximum = 2,
      .choices = kScrollwheelChoices,
  },
  {"Developer console", "Open the console with the key left of 1 or Settings; enabling this also allows Ctrl+F12. "
      "It supports cheats, key bindings and Lua.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_DEVELOPER,
      .default_value = false,
      .key = "interface.developer_enabled", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, developer_enabled), .maximum = 1,
  },
  {"Lua scripting", "Allow Lua in the developer console. City mutations use city.command; scripts have an "
      "instruction budget. Native APIs replace executable memory access.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_LUA,
      .default_value = false,
      .key = "interface.lua_enabled", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, lua_enabled), .maximum = 1,
  },
  {"Music in background", "Keep music audible when the game loses focus. Off mutes it until focus returns.",
      .group = CITY_SETTINGS_SOUND, .action = GAME_OPTIONS_BACKGROUND_MUSIC,
      .default_value = true,
      .key = "audio.music_background", .owner = SETTING_AUDIO, .type = SETTING_BOOL,
      .offset = offsetof(CityAudioState, music_background), .maximum = 1,
      .effects = SETTING_UPDATE_VOLUME,
  },
  {"Custom MIDI soundfont", "Use a custom SF2 or DLS bank. "
      "Choose one with soundfont FILE.sf2 in the developer console. "
      "Off restores the system bank.",
      .group = CITY_SETTINGS_SOUND, .action = GAME_OPTIONS_SOUNDFONT,
      .default_value = false,
      .key = "audio.custom_soundfont", .owner = SETTING_AUDIO, .type = SETTING_BOOL,
      .offset = offsetof(CityAudioState, custom_soundfont), .maximum = 1,
  },
  {"Always save SC2K", "Use the extended city format even for a stock-size city. SC2K preserves native settings "
      "and extended state. Off still uses SC2K when the city requires it.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_EXTENDED_SAVE,
      .default_value = false,
      .key = "game.save_extended", .owner = SETTING_GAME, .type = SETTING_BOOL,
      .offset = offsetof(Game, save_extended), .maximum = 1,
  },
  {"Correct saved map position", "sc2kfix: save the map window's actual center and zoom so reopening a city "
      "does not jump to a different location.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_SAVE_VIEW_POSITION,
      .default_value = true,
      .key = "display.save_view_position", .owner = SETTING_DISPLAY, .type = SETTING_BOOL,
      .offset = offsetof(DrawingOptions, save_view_position), .maximum = 1,
  },
  {"Recover zero-length save headers", "sc2kfix: recover .sc2 saves with a zero FORM length using the actual file "
      "size. Does not change the source file or repair damaged chunks.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_REPAIR_SAVE_HEADERS,
      .default_value = true,
      .key = "game.repair_save_headers", .owner = SETTING_GAME, .type = SETTING_BOOL,
      .offset = offsetof(Game, repair_save_headers), .maximum = 1,
  },
  {"Diagnostic log", "Append console output and native errors to sc2k.log in ~/.opensc2k.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_LOG,
      .default_value = false,
      .key = "game.diagnostic_log", .owner = SETTING_GAME, .type = SETTING_BOOL,
      .offset = offsetof(Game, diagnostic_log), .maximum = 1,
      .effects = SETTING_CONFIGURE_DIAGNOSTICS,
  },
  {"Crash diagnostics", "Record fatal signals and fault addresses, "
      "then let the operating system create its crash report.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_CRASH,
      .default_value = false,
      .key = "game.crash_diagnostics", .owner = SETTING_GAME, .type = SETTING_BOOL,
      .offset = offsetof(Game, crash_diagnostics), .maximum = 1,
      .effects = SETTING_CONFIGURE_DIAGNOSTICS,
  },
  {"Save backups", "Preserve the previous saved city as filename.bak before overwriting. "
      "If backup creation fails, saving stops before changing the city file.",
      .group = CITY_SETTINGS_INTERFACE, .action = GAME_OPTIONS_BACKUP_SAVES,
      .default_value = false,
      .key = "game.backup_saves", .owner = SETTING_GAME, .type = SETTING_BOOL,
      .offset = offsetof(Game, backup_saves), .maximum = 1,
  },
  {"Demolition effect elevation", "sc2kfix: anchor demolition dust to the building's water or land elevation "
      "rather than the clicked tile's water flag.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_DEMOLITION_ELEVATION,
      .default_value = true,
      .key = "display.demolition_elevation", .owner = SETTING_DISPLAY, .type = SETTING_U8,
      .offset = offsetof(DrawingOptions, demolition_elevation), .maximum = 1,
  },
  {"Preserve terrain above buried utilities", "Keep surface slopes and river/lake beds intact when laying pipes "
      "or subways, without a grading surcharge. Allow utilities to cross banks in either direction. "
      "Off restores stock terrain preparation and admission.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_BURIED_UTILITY_TERRAIN,
      .default_value = true,
      .key = "fixes.underwater_utilities", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_BURIED_UTILITY_TERRAIN,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_BURIED_UTILITY_TERRAIN,
  },
  {"Reuse existing infrastructure for free", "Allow drawing over existing pipes, plain roads, rails and highways "
      "without an error or another charge. Unchanged highway drags are silent; drags continue through junctions. "
      "Off restores stock behavior.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_REUSE_INFRASTRUCTURE,
      .default_value = true,
      .key = "fixes.reuse_infrastructure", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_REUSE_INFRASTRUCTURE,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_REUSE_INFRASTRUCTURE,
  },
  {"Detailed generated landscapes", "Add varied tree species, rough ground and rock clusters to newly generated "
      "terrain. Off preserves the original generated landscape. Existing terrain stays unchanged.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_LANDSCAPE_DETAILS,
      .default_value = true,
      .key = "fixes.landscape_details", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_LANDSCAPE_DETAILS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_LANDSCAPE_DETAILS,
  },
  {"Deterministic transport root cost", "Initialize the route's starting cost instead of using an unwritten stock "
      "stack byte. Off permits captured stock values in comparison tools; normal gameplay still uses zero.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_ROUTE_ROOT_COST,
      .default_value = true,
      .key = "fixes.route_root_cost", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_ROUTE_ROOT_COST,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_ROUTE_ROOT_COST,
  },
  {"Scroll by individual pixels", "Move the camera smoothly. Off restores SimCity 2000's horizontal camera "
      "alignment for the current zoom level.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_DRAWING_FIX + DRAWING_FIX_PIXEL_SCROLLING,
      .default_value = true,
      .key = "display.pixel_scrolling", .owner = SETTING_DISPLAY, .type = SETTING_MASK,
      .offset = offsetof(DrawingOptions, fixes), .maximum = 1, .mask = 1u << DRAWING_FIX_PIXEL_SCROLLING,
      .effects = SETTING_REDRAW_INTERFACE,
  },
  {"Bounded disaster location search", "Avoid an endless aircraft disaster search when the central map has no "
      "unmarked location. Off restores the original random search.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_BOUNDED_DISASTER_SEARCH,
      .default_value = true,
      .key = "fixes.bounded_disaster_search", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_BOUNDED_DISASTER_SEARCH,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_BOUNDED_DISASTER_SEARCH,
  },
  {"Larger moving-object pool", "Increase the event pool and vehicle limits according to map area. "
      "Reserve entries for emergency units and effects. Off restores stock allocation limits.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_EXPANDED_EVENTS,
      .default_value = true,
      .key = "fixes.expanded_events", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_EXPANDED_EVENTS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_EXPANDED_EVENTS,
  },
  {"Bridge ramps on dry ground", "Build new road and rail bridge ramps above the existing dry banks, "
      "as in OpenTTD, without raising the ground or filling canal tiles. Existing bridges keep their terrain.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_FIX + CITY_FIX_DRY_BRIDGE_APPROACHES,
      .default_value = true,
      .key = "fixes.dry_bridge_approaches", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_DRY_BRIDGE_APPROACHES,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_DRY_BRIDGE_APPROACHES,
  },
  {"Compact highway placement", "Place straight highways in two-cell segments without snapping to a two-tile grid. "
      "Diagonal drags use two-by-two bends. Direction follows the drag or adjoining highways. "
      "Off restores two-by-two highway placement.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_COMPACT_HIGHWAYS,
      .default_value = true,
      .key = "fixes.compact_highways", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_COMPACT_HIGHWAYS,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_COMPACT_HIGHWAYS,
  },
  {"Water spreads onto flat land", "Allow adjacent open water to flood flat clear land at or below its waterline. "
      "Water can always reclaim shore slopes through reachable edges. Stock city imports start with this off.",
      .group = CITY_SETTINGS_INFRASTRUCTURE, .action = GAME_OPTIONS_FIX + CITY_FIX_WATER_SPREADING,
      .default_value = true,
      .key = "fixes.water_spreading", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_WATER_SPREADING,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_WATER_SPREADING,
  },
  {"Native landscape behavior", "Use native landscape artwork, terrain editing and placement improvements. "
      "Route trips along bridge surfaces and enforce their height and direction. "
      "Keep sailboats off shore banks and rapids. "
      "Tile storage always uses the native encoding, including when this option is off.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_NATIVE_LANDSCAPE,
      .default_value = true,
      .key = "fixes.native_landscape", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_NATIVE_LANDSCAPE,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_NATIVE_LANDSCAPE,
  },
  {"Gradual road and rail paving", "New roads and railroads start with bare ground, which gradually recovers. "
      "Completed buildings on free sides encourage stone paving if their kind qualifies or land value is at least "
      "128 of 255. Two connected paved neighbors also sustain paving. "
      "Requires native landscape behavior. Off stops ground changes; existing ground remains unchanged.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_TRANSPORT_PAVING,
      .default_value = true,
      .key = "fixes.transport_paving", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_TRANSPORT_PAVING,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_TRANSPORT_PAVING,
  },
  {"Avoid duplicate traversal queue entries", "Do not enqueue a tile again while it is already waiting for traversal. "
      "Applies to every map size. Transport path searches may still revisit tiles. "
      "Off restores the original duplicate-entry behavior.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_ROUTE_QUEUE_DEDUPLICATION,
      .default_value = true,
      .key = "fixes.route_queue_deduplication", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_ROUTE_QUEUE_DEDUPLICATION,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_ROUTE_QUEUE_DEDUPLICATION,
  },
  {"Preserve terrain beneath highways", "Build elevated highway inclines and junctions above the existing hillside. "
      "Off restores stock highway grading. Existing terrain stays unchanged.",
      .group = CITY_SETTINGS_SIMCITY_FIXES, .action = GAME_OPTIONS_FIX + CITY_FIX_HIGHWAY_TERRAIN,
      .default_value = true,
      .key = "fixes.highway_terrain", .owner = SETTING_CITY, .type = SETTING_MASK64,
      .offset = offsetof(City, fixes), .maximum = 1, .mask = 1ull << CITY_FIX_HIGHWAY_TERRAIN,
      .command = CITY_COMMAND_SET_FIX, .parameter = CITY_FIX_HIGHWAY_TERRAIN,
  },
  // General controls share this schema without appearing twice in the advanced browser.
  {.key = "display.scale", .owner = SETTING_DISPLAY, .type = SETTING_I32,
      .offset = offsetof(DrawingOptions, scale),
      .minimum = 100, .maximum = 500, .default_value = 100, .hidden = true, .preserve_initial = true},
  {.key = "audio.master_volume", .owner = SETTING_AUDIO, .type = SETTING_U8,
      .offset = offsetof(CityAudioState, master_volume),
      .minimum = 0, .maximum = 100, .default_value = 100, .hidden = true, .preserve_initial = true},
  {.key = "interface.auto_goto_disabled", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, auto_goto_disabled),
      .minimum = 0, .maximum = 1, .default_value = 0, .hidden = true, .preserve_initial = true},
  {.key = "interface.newspaper_delivery_disabled", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, newspaper_delivery_disabled),
      .minimum = 0, .maximum = 1, .default_value = 0, .hidden = true, .preserve_initial = true},
  {.key = "interface.extra_newspaper_disabled", .owner = SETTING_UI, .type = SETTING_BOOL,
      .offset = offsetof(CityInterfaceState, extra_newspaper_disabled),
      .minimum = 0, .maximum = 1, .default_value = 0, .hidden = true, .preserve_initial = true},
  {.key = "music.programme", .owner = SETTING_JUKEBOX, .type = SETTING_I32,
      .offset = offsetof(Jukebox, programme),
      .minimum = 0, .maximum = JUKEBOX_PROGRAMMES - 1, .default_value = 0, .hidden = true, .preserve_initial = true},
  {.key = "music.shuffle", .owner = SETTING_JUKEBOX, .type = SETTING_I32,
      .offset = offsetof(Jukebox, shuffle),
      .minimum = 0, .maximum = 1, .default_value = 0, .hidden = true, .preserve_initial = true},
  {.key = "music.volume", .owner = SETTING_JUKEBOX, .type = SETTING_I32,
      .offset = offsetof(Jukebox, volume),
      .minimum = 0, .maximum = 100, .default_value = 50, .hidden = true, .preserve_initial = true},
  {.key = "music.effects_volume", .owner = SETTING_JUKEBOX, .type = SETTING_I32,
      .offset = offsetof(Jukebox, effects_volume),
      .minimum = 0, .maximum = 100, .default_value = 80, .hidden = true, .preserve_initial = true},
  {.key = "music.collapsed", .owner = SETTING_UI, .type = SETTING_I32,
      .offset = offsetof(CityInterfaceState, music_collapsed),
      .minimum = 0, .maximum = 1, .default_value = 0, .hidden = true, .preserve_initial = true},
  {.key = "music.pinned", .owner = SETTING_UI, .type = SETTING_I32,
      .offset = offsetof(CityInterfaceState, music_pinned),
      .minimum = 0, .maximum = 1, .default_value = 0, .hidden = true, .preserve_initial = true},
  {.key = "game.speed", .owner = SETTING_CITY, .type = SETTING_I16,
      .offset = offsetof(City, settings.speed),
      .maximum = CITY_SPEED_UNLIMITED, .default_value = CITY_SPEED_NORMAL, .hidden = true,
      .command = CITY_COMMAND_SET_SIMULATION_SPEED,
      .toggle = false},
  {.key = "game.no_disasters", .owner = SETTING_CITY, .type = SETTING_U32,
      .offset = offsetof(City, indicators.event_flag),
      .maximum = 1, .default_value = 0, .hidden = true, .command = CITY_COMMAND_TOGGLE_NO_DISASTERS,
      .toggle = true},
  {.key = "game.auto_budget", .owner = SETTING_CITY, .type = SETTING_U32,
      .offset = offsetof(City, indicators.auto_budget_enabled),
      .maximum = 1, .default_value = 0, .hidden = true, .command = CITY_COMMAND_TOGGLE_AUTO_BUDGET,
      .toggle = true},

};

const int kCitySettingsCount = sizeof(kCitySettings) / sizeof(kCitySettings[0]);
_Static_assert(sizeof(kCitySettings) / sizeof(kCitySettings[0]) <= GAME_OPTIONS_FIX - GAME_OPTIONS_SETTING,
    "advanced settings must fit the settings controls");

static const void *CitySettingOwnerAddress(const Game *game, CitySettingOwner owner) {
  switch (owner) {
  case SETTING_GAME: return game;
  case SETTING_CITY: return game->city;
  case SETTING_UI: return game->ui;
  case SETTING_AUDIO: return game->audio;
  case SETTING_DISPLAY: return &game->drawing->options;
  case SETTING_JUKEBOX: return &game->audio->jukebox;
  }
  return NULL;
}

int ReadCitySettingValue(const Game *game, const CitySetting *setting, bool defaults) {
  if (defaults && setting->owner == SETTING_CITY) return ReadGameCityDefault(game, setting);
  const u8 *field = (const u8 *)CitySettingOwnerAddress(game, setting->owner) + setting->offset;
  int value;
  switch (setting->type) {
  case SETTING_BOOL: value = *(const bool *)field; break;
  case SETTING_U8: value = *field; break;
  case SETTING_I16: value = *(const i16 *)field; break;
  case SETTING_I32: value = *(const i32 *)field; break;
  case SETTING_U32: value = (int)*(const u32 *)field; break;
  case SETTING_MASK:
    value = (*(const u32 *)field & setting->mask) != 0;
    break;
  case SETTING_MASK64:
    value = (*(const u64 *)field & setting->mask) != 0;
    break;
  }
  return setting->inverted ? !value : value;
}

void WriteCitySettingValue(Game *game, const CitySetting *setting, int value, bool defaults) {
  if (setting->inverted) value = !value;
  if (defaults && setting->command == CITY_COMMAND_SET_FIX) {
    game->default_city_fixes = (game->default_city_fixes & ~setting->mask) | (value ? setting->mask : 0);
    return;
  }
  // Authoritative City fields are changed by resolved City commands only.
  if (setting->owner == SETTING_CITY) return;
  u8 *field = (u8 *)CitySettingOwnerAddress(game, setting->owner) + setting->offset;
  switch (setting->type) {
  case SETTING_BOOL: *(bool *)field = value != 0; break;
  case SETTING_U8: *field = value; break;
  case SETTING_I16: *(i16 *)field = value; break;
  case SETTING_I32: *(i32 *)field = value; break;
  case SETTING_U32: *(u32 *)field = value; break;
  case SETTING_MASK: *(u32 *)field = (*(u32 *)field & ~setting->mask) | (value ? setting->mask : 0); break;
  case SETTING_MASK64: *(u64 *)field = (*(u64 *)field & ~setting->mask) | (value ? setting->mask : 0); break;
  }
}

bool RunCitySettingCommand(Game *game, const CitySetting *setting, int value) {
  if (setting->toggle && ReadCitySettingValue(game, setting, false) == value) return true;
  bool fix = setting->command == CITY_COMMAND_SET_FIX;
  return RunCityCommand(game, setting->command, INVALID_TILE,
      fix ? setting->parameter : setting->toggle ? 0 : value, fix ? value : 0, 0, NULL, NULL);
}
