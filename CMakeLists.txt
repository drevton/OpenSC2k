cmake_minimum_required(VERSION 3.21)
project(OpenSC2k VERSION 0.2.0 LANGUAGES C)
set(SC2K_VERSION_STAGE 1) # 0 development, 1 alpha, 2 beta, 3 stable.
set(SC2K_VERSION_DISPLAY "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if(PROJECT_VERSION_PATCH GREATER 0)
  string(APPEND SC2K_VERSION_DISPLAY ".${PROJECT_VERSION_PATCH}")
endif()
string(APPEND SC2K_VERSION_DISPLAY " Alpha")
configure_file(src/base/version.h.in generated/base/version.h @ONLY)
# Large maps need optimized tile loops even with a plain single-config build.
# Preserve an explicitly selected configuration, including sanitizer builds.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build configuration" FORCE)
endif()
if(APPLE)
  enable_language(OBJC)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options($<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>:-Werror=constant-conversion>)
# Original x86 integer add/subtract/multiply retain their low 32 bits.
# Keep this defined in optimized builds without arithmetic plumbing.
add_compile_options($<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>:-fwrapv>)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options("$<$<COMPILE_LANGUAGE:C,OBJC>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>")
endif()
if(NOT APPLE)
  link_libraries(m)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "from PIL import Image"
  RESULT_VARIABLE SC2K_PILLOW_RESULT ERROR_QUIET)
if(NOT SC2K_PILLOW_RESULT EQUAL 0)
  message(FATAL_ERROR "Landscape packing requires Pillow: ${Python3_EXECUTABLE} -m pip install Pillow")
endif()
set(SC2K_LANDSCAPE_CATALOG "${CMAKE_SOURCE_DIR}/assets/landscape/opengfx2/sprites.json")
set(SC2K_GENERATED_GRAPHICS_DIR "${CMAKE_BINARY_DIR}/generated-assets")
set(SC2K_ROAD_CAR_PATHS "${CMAKE_BINARY_DIR}/generated/road-car-paths.inc")
set(SC2K_RAIL_PATHS "${CMAKE_BINARY_DIR}/generated/rail-paths.inc")
add_custom_command(OUTPUT "${SC2K_ROAD_CAR_PATHS}" "${SC2K_RAIL_PATHS}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/bake_road_car_paths.py" "${SC2K_ROAD_CAR_PATHS}"
    --rail-output "${SC2K_RAIL_PATHS}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/bake_road_car_paths.py"
    "${CMAKE_SOURCE_DIR}/assets/landscape/vehicles/blender/manifest.json"
  COMMENT "Baking compact road and rail paths" VERBATIM)
set(SC2K_LANDSCAPE_PACK "${SC2K_GENERATED_GRAPHICS_DIR}/landscape/opengfx2/landscape.dat")
file(GLOB SC2K_LANDSCAPE_IMAGES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/assets/landscape/opengfx2/sprites/*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/roads/*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/rails/*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/bridges/*.png")
file(GLOB SC2K_WORLD_ASSETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/landscape/world/*.png")
file(GLOB SC2K_ROAD_VEHICLE_ASSETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/landscape/vehicles/*.png")
file(GLOB SC2K_CIVIC_ASSETS CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/approved-bake.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/approved-view.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/approved-ground.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/power-smoke/smoke.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/sprites.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/bake.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/view-*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/smoke-base-*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/animation/*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/animation/*.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/ground-*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/civic/*/*mask*.png")
file(GLOB SC2K_BUILDING_ASSETS CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/approved-bake.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/approved-view.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/approved-ground.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/sprites.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/bake.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/view-*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/ground-*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/*/*mask*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/construction/kit.json"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/construction/*.png"
  "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/ground/*.png")
set(SC2K_LANDSCAPE_INPUTS "${SC2K_GENERATED_GRAPHICS_DIR}/landscape-inputs.txt")
string(JOIN "\n" SC2K_LANDSCAPE_INPUT_TEXT
  ${SC2K_LANDSCAPE_IMAGES} ${SC2K_CIVIC_ASSETS} ${SC2K_BUILDING_ASSETS} ${SC2K_ROAD_VEHICLE_ASSETS}
  ${SC2K_WORLD_ASSETS})
file(GENERATE OUTPUT "${SC2K_LANDSCAPE_INPUTS}" CONTENT "${SC2K_LANDSCAPE_INPUT_TEXT}\n")
add_custom_command(OUTPUT "${SC2K_LANDSCAPE_PACK}"
    "${SC2K_GENERATED_GRAPHICS_DIR}/landscape/opengfx2/tile-graphics.dat"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/pack_landscape_assets.py"
    --catalog "${SC2K_LANDSCAPE_CATALOG}" --output "${SC2K_LANDSCAPE_PACK}"
    --input-list "${SC2K_LANDSCAPE_INPUTS}" --depfile "${SC2K_LANDSCAPE_PACK}.d"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/pack_landscape_assets.py" "${SC2K_LANDSCAPE_CATALOG}"
    "${CMAKE_SOURCE_DIR}/tools/bake_map_graphics.py"
    "${CMAKE_SOURCE_DIR}/assets/landscape/map-graphics.json"
    "${CMAKE_SOURCE_DIR}/src/city/maps/tile_storage.h"
    "${CMAKE_SOURCE_DIR}/src/assets/graphics.h"
    "${CMAKE_SOURCE_DIR}/src/drawing/maps/tables.c"
    "${CMAKE_SOURCE_DIR}/assets/landscape/civic/catalog.json"
    "${CMAKE_SOURCE_DIR}/assets/landscape/buildings/catalog.json"
    "${CMAKE_SOURCE_DIR}/assets/landscape/bridges/traffic-foreground.json"
    "${CMAKE_SOURCE_DIR}/assets/landscape/vehicles/blender/manifest.json"
    "${CMAKE_SOURCE_DIR}/assets/landscape/opengfx2/detail128/terrain-masks.png"
    "${CMAKE_SOURCE_DIR}/assets/landscape/world/catalog.json" "${SC2K_LANDSCAPE_INPUTS}"
  DEPFILE "${SC2K_LANDSCAPE_PACK}.d"
  COMMENT "Packing landscape PNGs" VERBATIM)
add_custom_target(sc2k_landscape DEPENDS "${SC2K_LANDSCAPE_PACK}")
# Baked PCM is a normal incremental build input. Generating authored sounds and
# downloading upstream sources remain explicit authoring operations.
set(SC2K_SOUND_PACK "${SC2K_GENERATED_GRAPHICS_DIR}/audio/sound-effects.dat")
set(SC2K_SOUND_METADATA "${SC2K_GENERATED_GRAPHICS_DIR}/audio/sound-effects.json")
file(GLOB_RECURSE SC2K_SOUND_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/audio/*.wav")
add_custom_command(OUTPUT "${SC2K_SOUND_PACK}" "${SC2K_SOUND_METADATA}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/bake_sound_effects.py"
    --output "${SC2K_SOUND_PACK}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/bake_sound_effects.py" ${SC2K_SOUND_SOURCES}
  COMMENT "Baking native sound effects from WAV sources" VERBATIM)
add_custom_target(sc2k_sound_effects DEPENDS "${SC2K_SOUND_PACK}" "${SC2K_SOUND_METADATA}")
set(SC2K_ADVISOR_PACK "${SC2K_GENERATED_GRAPHICS_DIR}/ui/advisors/advisors.dat")
file(GLOB SC2K_ADVISOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/ui/advisors/sources/*.png")
add_custom_command(OUTPUT "${SC2K_ADVISOR_PACK}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/bake_advisor_portraits.py"
    --output "${SC2K_ADVISOR_PACK}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/bake_advisor_portraits.py"
    "${CMAKE_SOURCE_DIR}/tools/pack_landscape_assets.py"
    "${CMAKE_SOURCE_DIR}/assets/ui/advisors/manifest.json" ${SC2K_ADVISOR_SOURCES}
  COMMENT "Baking advisor portraits" VERBATIM)
add_custom_target(sc2k_advisors DEPENDS "${SC2K_ADVISOR_PACK}")
set(SC2K_NEIGHBOR_PACK "${SC2K_GENERATED_GRAPHICS_DIR}/ui/neighbors/neighbors.dat")
file(GLOB SC2K_NEIGHBOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/ui/neighbors/sources/*.png")
add_custom_command(OUTPUT "${SC2K_NEIGHBOR_PACK}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/bake_neighbor_artwork.py"
    --output "${SC2K_NEIGHBOR_PACK}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/bake_neighbor_artwork.py"
    "${CMAKE_SOURCE_DIR}/tools/pack_landscape_assets.py"
    "${CMAKE_SOURCE_DIR}/assets/ui/neighbors/manifest.json" ${SC2K_NEIGHBOR_SOURCES}
  COMMENT "Baking neighbor population illustrations" VERBATIM)
add_custom_target(sc2k_neighbors DEPENDS "${SC2K_NEIGHBOR_PACK}")

option(SC2K_COMPRESS_ASSETS "Use level 15 instead of fast compression for the three asset packs" OFF)
set(SC2K_ASSET_BLOCK_LAYOUT "" CACHE FILEPATH "Optional JSON content-block layout for release assets")

option(SC2K_SANITIZE "Enable undefined behavior diagnostics" OFF)
find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
  set(SC2K_SDL_TARGET SDL2::SDL2)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set(SC2K_SDL_TARGET PkgConfig::SDL2)
endif()
find_package(ZLIB REQUIRED)

add_library(sc2k_zstd STATIC thirdparty/zstd/zstd.c)
target_include_directories(sc2k_zstd PUBLIC thirdparty/zstd)
# Asset decoding happens at startup; one Huffman implementation supports the same pack format.
target_compile_definitions(sc2k_zstd PRIVATE HUF_FORCE_DECOMPRESS_X1)

add_library(sc2k_core STATIC
  "${SC2K_ROAD_CAR_PATHS}"
  "${SC2K_RAIL_PATHS}"

  # assets
  src/assets/asset_file.c
  src/assets/assets.c
  src/assets/audio.c
  src/assets/facilities.c
  src/assets/fonts.c
  src/assets/graphics.c
  src/assets/names.c
  src/assets/news.c
  src/assets/run_sprite.c
  src/assets/sprites.c

  # audio
  src/audio/backend.c
  src/audio/effects.c
  src/audio/jukebox.c
  src/audio/state.c

  # base
  src/base/config.c
  src/base/file.c
  src/base/fixed.c
  src/base/text.c
  src/base/time.c
  src/base/utf8.c
  src/base/version.c

  # city/disasters
  src/city/disasters/dispatch.c
  src/city/disasters/effects.c
  src/city/disasters/tile_effects.c

  # city/economy
  src/city/economy/budget.c
  src/city/economy/budget_advisor.c
  src/city/economy/coefficients.c
  src/city/economy/economy.c
  src/city/economy/facilities.c
  src/city/economy/history.c

  # city/engine
  src/city/engine/cheats.c
  src/city/engine/city_name.c
  src/city/engine/commands.c
  src/city/engine/initialization.c
  src/city/engine/questions.c
  src/city/engine/random.c
  src/city/engine/simulation.c
  src/city/engine/simulation_clock.c
  src/city/engine/weather.c

  # city/events
  src/city/events/aircraft.c
  src/city/events/boats.c
  src/city/events/disaster_actors.c
  src/city/events/emergency.c
  src/city/events/movement.c
  src/city/events/news.c
  src/city/events/trains.c

  # city/growth
  src/city/growth/zoning.c

  # city/infrastructure
  src/city/infrastructure/bridges.c
  src/city/infrastructure/highways.c
  src/city/infrastructure/lines.c
  src/city/infrastructure/networks.c
  src/city/infrastructure/power_lines.c
  src/city/infrastructure/utilities.c
  src/city/infrastructure/spans.c
  src/city/infrastructure/surface_lines.c
  src/city/infrastructure/tunnels.c
  src/city/infrastructure/transport.c

  # city/maps
  src/city/maps/environment.c
  src/city/maps/ground.c
  src/city/maps/buildings.c
  src/city/maps/information.c
  src/city/maps/labels.c
  src/city/maps/kinds.c
  src/city/maps/layers.c
  src/city/maps/tables.c
  src/city/maps/state.c
  src/city/maps/terrain.c
  src/city/maps/tile_queue.c
  src/city/maps/trees.c
  src/city/maps/water.c

  # city/saveload
  src/city/saveload/archive.c
  src/city/saveload/codec.c
  src/city/saveload/events_format.c
  src/city/saveload/container.c
  src/city/saveload/input.c
  src/city/saveload/legacy_text.c
  src/city/saveload/load.c
  src/city/saveload/load_chunks.c
  src/city/saveload/map_legacy.c
  src/city/saveload/map_native.c
  src/city/saveload/misc.c
  src/city/saveload/misc_state.c
  src/city/saveload/path.c
  src/city/saveload/runtime.c
  src/city/saveload/save.c
  src/city/saveload/save_archive.c
  src/city/saveload/save_chunks.c
  src/city/saveload/sc2k.c
  src/city/saveload/scenario.c
  src/city/saveload/scenario_format.c
  src/city/saveload/schema.c
  src/city/saveload/summary.c

  # city
  src/city/military.c
  src/city/state.c

  # city/terrain
  src/city/terrain/climate.c
  src/city/terrain/elevation.c
  src/city/terrain/features.c
  src/city/terrain/generator.c
  src/city/terrain/map.c
  src/city/terrain/noise.c
  src/city/terrain/settings.c
  src/city/terrain/water.c

  # city/tools
  src/city/tools/demolition.c
  src/city/tools/placement.c
  src/city/tools/terrain.c

  # drawing
  src/drawing/bitmaps.c
  src/drawing/newspaper.c
  src/drawing/startup.c
  src/drawing/cursors.c
  src/drawing/frame_overlay.c
  src/drawing/palette.c

  # drawing/fonts
  src/drawing/fonts/fonts.c

  # drawing/maps
  src/drawing/maps/cost_labels.c
  src/drawing/maps/map_animation.c
  src/drawing/maps/map_coordinates.c
  src/drawing/maps/map_damage.c
  src/drawing/maps/map_data.c
  src/drawing/maps/map_depth.c
  src/drawing/maps/map_effects.c
  src/drawing/maps/map_events.c
  src/drawing/maps/map_overlays.c
  src/drawing/maps/map_sprites.c
  src/drawing/maps/map_terrain.c
  src/drawing/maps/map_traversal.c
  src/drawing/maps/map_vehicles.c
  src/drawing/maps/map_view.c
  src/drawing/maps/placement.c
  src/drawing/maps/road_cars.c
  src/drawing/maps/sprite_cache.c
  src/drawing/maps/tables.c

  # drawing/raster
  src/drawing/raster/canvas_primitives.c
  src/drawing/raster/screen_damage.c

  # drawing/sprites
  src/drawing/sprites/preparation.c
  src/drawing/sprites/sources.c
  src/drawing/sprites/sprites.c

  # drawing
  src/drawing/state.c

  # game
  src/game/audio.c
  src/game/city_lifecycle.c
  src/game/errors.c
  src/game/graphics.c
  src/game/observation.c
  src/game/settings_catalog.c
  src/game/palette.c
  src/game/settings.c
  src/game/timing.c
  src/game/update.c

  # ui/charts
  src/ui/charts/charts.c

  # ui/cursor
  src/ui/cursor/cursor.c
  src/ui/cursor/input.c

  # ui/dialogs
  src/ui/dialogs/budget.c
  src/ui/dialogs/lifetime.c
  src/ui/dialogs/exit.c
  src/ui/dialogs/file_browser.c
  src/ui/dialogs/game_options.c
  src/ui/dialogs/information.c
  src/ui/dialogs/map_data.c
  src/ui/dialogs/messages.c
  src/ui/dialogs/name_editor.c
  src/ui/dialogs/scenario_window.c
  src/ui/dialogs/span_host.c

  # ui
  src/ui/hotkeys.c
  src/ui/input.c
  src/ui/map.c
  src/ui/map_camera.c
  src/ui/map_saved_view.c

  # ui/menus
  src/ui/menus/actions.c
  src/ui/menus/menu.c
  src/ui/menus/catalog.c

  # ui/music
  src/ui/music/jukebox.c

  # ui/news
  src/ui/news/newspaper_capture.c
  src/ui/news/newspaper_drawing.c
  src/ui/news/newspaper_presentation.c
  src/ui/news/newspaper_window.c

  # ui
  src/ui/signs.c

  # ui/startup
  src/ui/startup/startup_canvas.c
  src/ui/startup/startup_host.c
  src/ui/startup/startup_layout.c

  # ui
  src/ui/command_feedback.c
  src/ui/state.c
  src/ui/updates.c

  # ui/text
  src/ui/text/information.c
  src/ui/text/status.c
  src/ui/text/text.c

  # ui/tools
  src/ui/tools/definitions.c
  src/ui/tools/cost_labels.c
  src/ui/tools/map_input.c
  src/ui/tools/placement.c
  src/ui/tools/toolbar.c

  # ui/windows
  src/ui/windows/controls.c
  src/ui/windows/desktop.c
  src/ui/windows/events.c
  src/ui/windows/tooltips.c
  src/ui/windows/window_pixels.c
  src/ui/windows/windows.c
)
target_include_directories(sc2k_core PUBLIC src src/city "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(sc2k_core PUBLIC ${SC2K_SDL_TARGET} sc2k_zstd)
if(APPLE)
  target_link_libraries(sc2k_core PUBLIC "-framework AudioToolbox" "-framework CoreFoundation")
endif()
add_dependencies(sc2k_core sc2k_landscape sc2k_sound_effects sc2k_advisors sc2k_neighbors)
target_compile_options(sc2k_core PRIVATE -Wall -Wextra)
file(GLOB SC2K_LUA_SOURCES CONFIGURE_DEPENDS thirdparty/lua/*.c)
add_library(sc2k_lua STATIC ${SC2K_LUA_SOURCES})
target_include_directories(sc2k_lua PUBLIC thirdparty/lua)
if(UNIX)
  target_compile_definitions(sc2k_lua PRIVATE LUA_USE_POSIX)
  target_link_libraries(sc2k_lua PUBLIC ${CMAKE_DL_LIBS})
endif()
add_executable(sc2k MACOSX_BUNDLE
  src/base/diagnostics.c
  src/city/saveload/load_stream.c
  src/city/saveload/save_stream.c
  src/city/saveload/workspace.c
  src/drawing/display.c
  src/drawing/maps/map_cache.c
  src/game/application.c
  src/game/city_bootstrap.c
  src/game/commands.c
  src/game/frame.c
  src/game/input_script.c
  src/game/scripting.c
  src/ui/city_lifecycle.c
  src/ui/command_presentation.c
  src/ui/console.c
  src/ui/dialogs/budget_host.c
  src/ui/dialogs/file_dialog.c
  src/ui/dialogs/options_host.c
  src/ui/dialogs/presentation.c
  src/ui/input_dispatch.c
  src/ui/map_views.c
  src/ui/map_visibility.c
  src/ui/sdl.c
)
if(APPLE)
  target_sources(sc2k PRIVATE src/ui/macos.m)
  target_link_libraries(sc2k PRIVATE "-framework Cocoa")
endif()
target_link_libraries(sc2k PRIVATE sc2k_core sc2k_lua ${SC2K_SDL_TARGET})
# Optimize the complete Release player together and discard unused functions/data.
# Debug configurations keep their existing optimization and symbol settings.
include(CheckIPOSupported)
check_ipo_supported(RESULT SC2K_RELEASE_IPO_SUPPORTED LANGUAGES C)
include(CheckCCompilerFlag)
check_c_compiler_flag(-Oz SC2K_HAS_OZ)
set(SC2K_RELEASE_OPTIMIZATION -Os)
if(SC2K_HAS_OZ)
  set(SC2K_RELEASE_OPTIMIZATION -Oz)
endif()
foreach(target sc2k sc2k_core sc2k_lua sc2k_zstd)
  target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:${SC2K_RELEASE_OPTIMIZATION}>)
  if(SC2K_RELEASE_IPO_SUPPORTED)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
  if(NOT APPLE)
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>)
  endif()
endforeach()
target_link_options(sc2k PRIVATE $<$<CONFIG:Release>:${SC2K_RELEASE_OPTIMIZATION}>)
if(APPLE)
  target_link_options(sc2k PRIVATE $<$<CONFIG:Release>:-Wl,-dead_strip,-S,-x>)
else()
  target_link_options(sc2k PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()
target_sources(sc2k PRIVATE src/game/main.c)
add_dependencies(sc2k sc2k_sound_effects sc2k_advisors sc2k_neighbors)
set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/assets/ui/toolbar/icons.dat"
    "${SC2K_LANDSCAPE_PACK}"
    "${SC2K_ADVISOR_PACK}"
    "${SC2K_NEIGHBOR_PACK}"
    "${CMAKE_SOURCE_DIR}/assets/landscape/opengfx2/demolition.dat"
    "${SC2K_SOUND_PACK}"
    "${CMAKE_SOURCE_DIR}/assets/audio/README.md"
    "${SC2K_SOUND_METADATA}"
    "${CMAKE_SOURCE_DIR}/assets/audio/opensfx/credits.json"
    "${CMAKE_SOURCE_DIR}/assets/audio/opensfx/LICENSE"
    "${CMAKE_SOURCE_DIR}/assets/audio/opensfx/LICENSE-CC-BY-SA-3.0.txt"
    "${CMAKE_SOURCE_DIR}/assets/audio/opensfx/LICENSE-CC0-1.0.txt"
    "${CMAKE_SOURCE_DIR}/assets/audio/freesound/CREDITS.md"
    "${CMAKE_SOURCE_DIR}/assets/audio/freesound/sources.json"
    "${CMAKE_SOURCE_DIR}/assets/audio/freesound/LICENSE-CC-BY-4.0.txt")
target_compile_options(sc2k PRIVATE -Wall -Wextra)
# Xcode Run defaults to Debug. Keep symbols while optimizing rendering and diagnostic transport.
foreach(target sc2k_core sc2k)
  if(TARGET ${target})
    target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-O2>)
  endif()
endforeach()
if(APPLE)
  set(SC2K_APP_ICON "${CMAKE_SOURCE_DIR}/assets/app/OpenSC2K.icns")
  set(SC2K_APP_NAMES "${CMAKE_SOURCE_DIR}/assets/app/en.lproj/InfoPlist.strings")
  set_source_files_properties("${SC2K_APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties("${SC2K_APP_NAMES}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_sources(sc2k PRIVATE "${SC2K_APP_ICON}" "${SC2K_APP_NAMES}")
  set_target_properties(sc2k PROPERTIES MACOSX_BUNDLE_ICON_FILE "OpenSC2K.icns"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/ui/macos-info.plist.in")
  set(SC2K_PACKAGE_ROOT "$<TARGET_BUNDLE_DIR:sc2k>/Contents/Resources")
else()
  set(SC2K_PACKAGE_ROOT "$<TARGET_FILE_DIR:sc2k>")
endif()
if(WIN32)
  set_target_properties(sc2k PROPERTIES OUTPUT_NAME OpenSC2k)
  target_sources(sc2k_core PRIVATE src/audio/windows_midi.c)
  target_link_libraries(sc2k_core PUBLIC winmm)
  enable_language(RC)
  target_sources(sc2k PRIVATE assets/app/windows.rc)
  target_include_directories(sc2k PRIVATE assets/app)
  if(MINGW)
    # Runtime libraries carry debug sections even in Release; ship code without those symbols.
    target_link_options(sc2k PRIVATE -static-libgcc -static $<$<CONFIG:Release>:-s>)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND SC2K_RELEASE_IPO_SUPPORTED)
      # One code-generation unit also shares constants across the complete player.
      target_link_options(sc2k PRIVATE $<$<CONFIG:Release>:-flto-partition=one>)
    endif()
  endif()
  if(TARGET SDL2::SDL2)
    add_custom_command(TARGET sc2k POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:sc2k>"
      VERBATIM)
  endif()
endif()
find_program(SC2K_ZSTD_EXECUTABLE NAMES zstd REQUIRED)
set(SC2K_COMPRESSION_LEVEL 1)
if(SC2K_COMPRESS_ASSETS)
  set(SC2K_COMPRESSION_LEVEL 15)
endif()
set(SC2K_COMPRESSION_ARGS --zstd "${SC2K_ZSTD_EXECUTABLE}" --level ${SC2K_COMPRESSION_LEVEL})
if(SC2K_ASSET_BLOCK_LAYOUT)
  list(APPEND SC2K_COMPRESSION_ARGS --block-layout "${SC2K_ASSET_BLOCK_LAYOUT}")
  set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS "${SC2K_ASSET_BLOCK_LAYOUT}")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/asset-compression.txt"
  CONTENT "${SC2K_COMPRESS_ASSETS}\n${SC2K_ASSET_BLOCK_LAYOUT}\n")
set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS "${CMAKE_BINARY_DIR}/asset-compression.txt")
set(SC2K_PACKED_GRAPHICS
  fonts/overlay-font.dat fonts/interface-font.dat fonts/interface-coverage.dat fonts/newspaper-font.dat
  landscape/opengfx2/demolition.dat
  ui/toolbar/icons.dat ui/bridges/icons.dat ui/map-data/icons.dat
  ui/opengfx2/cursors/canvas-cursors.dat ui/opengfx2/music/transport.dat
  ui/illustrations/illustrations.dat ui/intro/intro.dat)
foreach(asset IN LISTS SC2K_PACKED_GRAPHICS)
  set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/assets/${asset}")
endforeach()
set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/tools/package_assets.py"
  "${CMAKE_SOURCE_DIR}/src/assets/asset_catalog.inc")
file(GLOB_RECURSE SC2K_ATTRIBUTION_FILES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/assets/*LICENSE*" "${CMAKE_SOURCE_DIR}/assets/*README*"
  "${CMAKE_SOURCE_DIR}/assets/*CREDITS*" "${CMAKE_SOURCE_DIR}/assets/*credits.json"
  "${CMAKE_SOURCE_DIR}/assets/*sources.json" "${CMAKE_SOURCE_DIR}/assets/*themes.list"
  "${CMAKE_SOURCE_DIR}/assets/*grant.txt" "${CMAKE_SOURCE_DIR}/assets/*upstream-readme.md"
  "${CMAKE_SOURCE_DIR}/thirdparty/*LICENSE" "${CMAKE_SOURCE_DIR}/docs/licenses/*.txt"
  "${CMAKE_SOURCE_DIR}/src/city/terrain/LICENSE.*")
set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS ${SC2K_ATTRIBUTION_FILES})
file(GLOB SC2K_MUSIC_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/music/openmsx/*.mid")
set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS ${SC2K_MUSIC_FILES})
set(SC2K_SOUND_OVERRIDE_ARGS)
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/audio/sound-effects.override.dat")
  set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/audio/sound-effects.override.dat")
  list(APPEND SC2K_SOUND_OVERRIDE_ARGS --sound-override
    "${CMAKE_SOURCE_DIR}/assets/audio/sound-effects.override.dat")
endif()
if(APPLE)
  # Resolve bundled libraries and remove host search paths before stamping/signing the player.
  add_custom_command(TARGET sc2k POST_BUILD
    COMMAND ${CMAKE_COMMAND} "-DSC2K_APP=$<TARGET_BUNDLE_DIR:sc2k>"
      -P "${CMAKE_SOURCE_DIR}/tools/bundle_macos.cmake" VERBATIM)
  set_property(TARGET sc2k APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/tools/bundle_macos.cmake")
endif()
add_custom_command(TARGET sc2k POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/package_assets.py"
    --source-root "${CMAKE_SOURCE_DIR}" --destination "${SC2K_PACKAGE_ROOT}"
    --landscape "${SC2K_LANDSCAPE_PACK}" --advisors "${SC2K_ADVISOR_PACK}"
    --neighbors "${SC2K_NEIGHBOR_PACK}" --sounds "${SC2K_SOUND_PACK}" ${SC2K_SOUND_OVERRIDE_ARGS}
    --music "${CMAKE_SOURCE_DIR}/assets/music/openmsx"
    --cache "${CMAKE_BINARY_DIR}/packed-assets" ${SC2K_COMPRESSION_ARGS}
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/stamp_revision.py" "$<TARGET_FILE:sc2k>"
    --source-root "${CMAKE_SOURCE_DIR}"
  COMMENT "Packaging intro, graphics and audio resources" VERBATIM)
if(APPLE)
  add_custom_command(TARGET sc2k POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:sc2k>" VERBATIM)
endif()
set_target_properties(sc2k PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "OpenSC2k"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "local.sc2k.reconstruction"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "local.sc2k.reconstruction")
if(SC2K_SANITIZE)
  foreach(target sc2k_core sc2k sc2k_lua sc2k_zstd)
    target_compile_options(${target} PRIVATE -fsanitize=undefined,float-cast-overflow
      -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=undefined,float-cast-overflow)
  endforeach()
endif()

target_link_libraries(sc2k_core PUBLIC ZLIB::ZLIB)
