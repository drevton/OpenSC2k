// TileInfo disaster-scan input (zero-based bits):
// Native overlay[1..4] carries stationary effect codes 11 pollution, 12 flood, 13/14 riots and 15 fire. The scan
// uses resolved overlay IDs, not the raw byte; overlay[0] can independently retain a sign. Moving effects and
// event lifetimes are City records. Effects may replace kind, which runs the shared family-metadata conversion.
// TF_WATERED/TF_POWERED are service state; permanent water is a kind class and flooding here is a transient effect.
// This module adds no private tile bits. Heights, slopes, ground and occupant payloads follow tile_storage.h and
// their owning family modules rather than being repurposed as disaster counters.

#include "city/maps/labels.h"
#include "city/engine/random.h"
#include "city/maps/buildings.h"
#include "audio/types.h"
#include "city/maps/map_types.h"
#include "city/maps/tile_storage.h"
#include "game/audio.h"
#include "city/presentation.h"
#include "city/state.h"
#include "city/disasters/disasters.h"
#include "city/events/events.h"
#include "city/growth/growth.h"
#include "city/maps/layers.h"
#include "city/tools/tools.h"
#include "game/state.h"
#include "assets/state.h"

enum {
  CITY_TILE_SCAN_CHANGED = 1 << 0,
  CITY_TILE_SCAN_SAW_RIOTS = 1 << 1,
  CITY_TILE_SCAN_SAW_FLOOD = 1 << 2,
  CITY_TILE_SCAN_SAW_FIRE = 1 << 3,
  CITY_TILE_SCAN_SAW_POLLUTION = 1 << 4,
  CITY_TILE_SCAN_CHEMICAL_BUILDING = 1 << 5,
};

// Scan the city map and advance transient disaster and growth markers. Requires initialized map and landscape/disaster
// state; advances transient overlay and tile effects in simulation order.
i64 UpdateCityMapTileStates(City *city) {
  static const i8 neighbor_offset[4][2] = {{0, -1}, {-1, 0}, {0, 1}, {1, 0}};
  u32 flags = 0;
  int x, y, overlay, direction;
  TileIndex neighbor_tile, cell_index;
  CityTileArea chemical_area = {.start = INVALID_TILE};
  if (city->disasters.event_decay_ticks > 0)
    --city->disasters.event_decay_ticks;

  for (y = 0; y < city->map_height; ++y) {
    for (x = 0; x < city->map_width; ++x) {
      cell_index = y * city->map_width + x;
      // Empty spans have no RNG or mutation effects. Inspect live bytes in scan
      // order so a disaster spreading into a later span is still handled now.
      while (x + 8 <= city->map_width) {
        int overlays = 0;
        for (int lane = 0; lane < 8; ++lane) overlays |= ReadOverlay(city, cell_index + lane);
        if (overlays) break;
        x += 8;
        cell_index += 8;
      }
      if (x == city->map_width) break;
      TileInfo *tp = &city->tile[cell_index];
      overlay = ReadOverlay(city, cell_index);
      switch (overlay) {
      case CITY_OVERLAY_FIRE: {
        flags |= CITY_TILE_SCAN_SAW_FIRE;
        if ((Sc2kCrtRand(city) & 3) != 0)
          break;
        if (HasTileWater(tp)) {
          WriteOverlay(city, cell_index, CITY_OVERLAY_NONE);
          InvalidateTile(city, cell_index);
          break;
        }

        direction = Sc2kCrtRand(city) & 7;
        switch (direction) {
        case CITY_NEIGHBOR_NORTH:
        case CITY_NEIGHBOR_WEST:
        case CITY_NEIGHBOR_SOUTH:
        case CITY_NEIGHBOR_EAST:
          neighbor_tile = GetRelativeTileOrInvalid(city, cell_index,
              neighbor_offset[direction][0], neighbor_offset[direction][1]);
          if (neighbor_tile != INVALID_TILE) {
            if (direction == CITY_NEIGHBOR_NORTH &&
                ReadOverlay(city, neighbor_tile) != CITY_OVERLAY_FIRE)
              flags |= CITY_TILE_SCAN_CHANGED;
            ApplyDisasterCellEffect(city, neighbor_tile, 0);
            if (direction != CITY_NEIGHBOR_NORTH &&
                ReadOverlay(city, neighbor_tile) != CITY_OVERLAY_FIRE)
              flags |= CITY_TILE_SCAN_CHANGED;
          }
          break;
        case 5: {
          int tile = tp->kind;
          if (IsTileKindBuilding(tile)) {
            flags &= ~CITY_TILE_SCAN_CHEMICAL_BUILDING;
            if (tile == TK_CHEMICAL_STORAGE || tile == TK_CHEMICAL_PROCESSING_2X2 ||
                tile == TK_CHEMICAL_PROCESSING_3X3)
              flags |= CITY_TILE_SCAN_CHEMICAL_BUILDING;
            chemical_area = flags & CITY_TILE_SCAN_CHEMICAL_BUILDING ?
                ResolveBulldozeFootprint(city, cell_index) : (CityTileArea){.start = INVALID_TILE};
            flags |= CITY_TILE_SCAN_CHANGED;
            DemolishCityFootprintForDisaster(city, cell_index, 1, 1);
            ResolveDestroyedCityMapCell(city, cell_index);
            if (NextCityMapRandomModulo(city, 16) == 0)
              CreateCityMapEffectEvent(city, cell_index, 0, 0, 1);

            if (flags & CITY_TILE_SCAN_CHEMICAL_BUILDING)
              for (int dy = 0; dy < chemical_area.height; ++dy)
                for (int dx = chemical_area.width - 1; dx >= 0; --dx)
                  SetCityMapOverlayIfReplaceable(city,
                      GetRelativeTileOrInvalid(city, chemical_area.start, dx, dy), CITY_OVERLAY_POLLUTION);
          }
          break;
        }
        default:
          if (ReadFireCoverage(city, MapTileXY(city, (x / 4) * 4, (y / 4) * 4)) + 8 > (u8)Sc2kCrtRand(city)) {
            flags |= CITY_TILE_SCAN_CHANGED;
            DemolishCityFootprintForDisaster(city, cell_index, 1, 1);
            ResolveDestroyedCityMapCell(city, cell_index);
          }
          break;
        }
        break;
      }

      case CITY_OVERLAY_RIOTS_NORTH_WEST:
      case CITY_OVERLAY_RIOTS_SOUTH_EAST: {
        flags |= CITY_TILE_SCAN_SAW_RIOTS;
        if ((Sc2kCrtRand(city) & 3) != 0)
          break;
        if ((u8)Sc2kCrtRand(city) == 0 || HasTileWater(tp)) {
          WriteOverlay(city, cell_index, CITY_OVERLAY_NONE);
          InvalidateTile(city, cell_index);
          break;
        }

        WriteTrafficDensity(city, MapTileXY(city, (x / 2) * 2, (y / 2) * 2), 0);
        direction = Sc2kCrtRand(city) & 0x7F;
        if (direction < 4) {
          neighbor_tile = GetRelativeTileOrInvalid(city, cell_index,
              neighbor_offset[direction][0], neighbor_offset[direction][1]);
          ApplyDisasterCellEffect(city, neighbor_tile, 0);
        }

        int neighbors = 0;
        int step = overlay == CITY_OVERLAY_RIOTS_NORTH_WEST ? -1 : 1;
        for (int axis = 0; axis < 2; ++axis) {
          TileIndex neighbor = GetRelativeTileOrInvalid(city, cell_index, axis == 1 ? step : 0, axis == 0 ? step : 0);
          if (neighbor == INVALID_TILE) continue;
          int tile = ReadTile(city, neighbor);
          bool passable = tile == TK_RUBBLE ||
              IsTileKindRoadRoute(tile);
          if (passable)
            neighbors |= 1 << axis;
        }

        if (neighbors == 0) {
          WriteOverlay(city, cell_index, overlay == CITY_OVERLAY_RIOTS_NORTH_WEST ? CITY_OVERLAY_RIOTS_SOUTH_EAST :
              CITY_OVERLAY_RIOTS_NORTH_WEST);
          InvalidateTile(city, cell_index);
        } else {
          bool reverse_direction = (Sc2kCrtRand(city) & 7) == 0;
          WriteOverlay(city, cell_index, reverse_direction ? (overlay == CITY_OVERLAY_RIOTS_NORTH_WEST ?
              CITY_OVERLAY_RIOTS_SOUTH_EAST
                                                                            : CITY_OVERLAY_RIOTS_NORTH_WEST)
                                : CITY_OVERLAY_NONE);
          InvalidateTile(city, cell_index);
          if (neighbors == 3)
            neighbors = (Sc2kCrtRand(city) & 1) + 1;
          if (neighbors == 1) {
            TileIndex neighbor = GetRelativeTileOrInvalid(city, cell_index, 0,
                overlay == CITY_OVERLAY_RIOTS_SOUTH_EAST ? 1 : -1);
            SetCityMapOverlayIfReplaceable(city, neighbor, overlay);
          } else {
            TileIndex neighbor = GetRelativeTileOrInvalid(city, cell_index,
                overlay == CITY_OVERLAY_RIOTS_SOUTH_EAST ? 1 : -1, 0);
            SetCityMapOverlayIfReplaceable(city, neighbor, overlay);
          }
        }
        break;
      }

      case CITY_OVERLAY_POLLUTION: {
        flags |= CITY_TILE_SCAN_SAW_POLLUTION;
        if ((Sc2kCrtRand(city) & 1) != 0)
          continue;
        if (NextCityMapRandomModulo(city, 64) == 0 ||
            (HasTileWater(tp) && (Sc2kCrtRand(city) & 0xF) == 0)) {
          WriteOverlay(city, cell_index, CITY_OVERLAY_NONE);
          InvalidateTile(city, cell_index);
          continue;
        }

        int tile = CityBuildingConstructionKind(tp);
        if (!tile) tile = tp->kind;
        if (IsTileKindZoneBuilding(tile) &&
            kTileZoneCategory[tile - TK_RESIDENTIAL_FIRST] == CITY_BUILDING_DEVELOPED) {
          // A projected construction kind is not a completed building footprint.
          if (tp->kind != tile) continue;
          CityTileArea footprint = ResolveBulldozeFootprint(city, cell_index);
          int size = footprint.width;
          if (!size) continue;
          if (size == 3)
            size = 4;
          GrowActiveZoneTiles(city, footprint.start + footprint.width - 1, size, 0);
        }

        int lowest_power = ReadCityLandHeight(tp);
        int direction = -1;
        for (int neighbor = 0; neighbor < 4; ++neighbor) {
          TileIndex neighbor_tile = GetRelativeTileOrInvalid(city, cell_index,
              kCardinalNeighborX[neighbor], kCardinalNeighborY[neighbor]);
          if (neighbor_tile != INVALID_TILE) {
            int power = ReadCityLandHeight(&city->tile[neighbor_tile]);
            if (power < lowest_power) {
              direction = neighbor;
              lowest_power = power;
            }
          }
        }
        if (direction == -1)
          direction = Sc2kCrtRand(city) & 3;
        WriteOverlay(city, cell_index, CITY_OVERLAY_NONE);
        InvalidateTile(city, cell_index);
        TileIndex neighbor = GetRelativeTileOrInvalid(city, cell_index,
            kCardinalNeighborX[direction], kCardinalNeighborY[direction]);
        SetCityMapOverlayIfReplaceable(city, neighbor, CITY_OVERLAY_POLLUTION);
        flags |= CITY_TILE_SCAN_CHANGED;
        continue;
      }

      case CITY_OVERLAY_FLOOD: {
        flags |= CITY_TILE_SCAN_SAW_FLOOD;
        if (city->disasters.event_decay_ticks != 0 || NextCityMapRandomModulo(city, 2) == 0) {
          if (city->disasters.event_decay_ticks >= 52 || (Sc2kCrtRand(city) & 3) == 0) {
            if (city->disasters.event_decay_ticks < 30 && (Sc2kCrtRand(city) & 3) == 0) {
              if (IsTileKindBuilding(tp->kind)) {
                DemolishCityFootprintForDisaster(city, cell_index, 0, 0);
              }
              WriteOverlay(city, cell_index, CITY_OVERLAY_NONE);
              InvalidateTile(city, cell_index);
            }
            if (city->disasters.event_decay_ticks > 0) {
              direction = Sc2kCrtRand(city) & 3;
              neighbor_tile = GetRelativeTileOrInvalid(city, cell_index,
                  neighbor_offset[direction][0], neighbor_offset[direction][1]);
              if (neighbor_tile != INVALID_TILE) {
                SpreadCityMapFireToCell(city, neighbor_tile,
                    ReadCityLandHeight(tp));
                if (ReadOverlay(city, neighbor_tile) != CITY_OVERLAY_FLOOD)
                  flags |= CITY_TILE_SCAN_CHANGED;
              }
            }
          }
        } else {
          WriteOverlay(city, cell_index, CITY_OVERLAY_NONE);
          InvalidateTile(city, cell_index);
        }
        break;
      }

      default:
        if (IsCityEventOverlay(overlay)) {
          int event_type = city->events.event[CityEventOverlaySlot(overlay)].type;
          if (event_type == CITY_MAP_EVENT_FIRE || event_type == CITY_MAP_EVENT_MILITARY ||
              (event_type == CITY_MAP_EVENT_POLICE && NextCityMapRandomModulo(city, 16) == 0)) {
            int direction = Sc2kCrtRand(city) & 3;
            TileIndex neighbor_tile = GetRelativeTileOrInvalid(city, cell_index,
                neighbor_offset[direction][0], neighbor_offset[direction][1]);
            if (neighbor_tile != INVALID_TILE) {
              if (ReadOverlay(city, neighbor_tile) == CITY_OVERLAY_FIRE)
                flags |= CITY_TILE_SCAN_CHANGED;
              ResolveDestroyedCityMapCell(city, neighbor_tile);
            }
          }
          if (event_type == CITY_MAP_EVENT_POLICE || event_type == CITY_MAP_EVENT_MILITARY) {
            int direction = Sc2kCrtRand(city) & 3;
            TileIndex neighbor = GetRelativeTileOrInvalid(city, cell_index,
                neighbor_offset[direction][0], neighbor_offset[direction][1]);
            ClearTransientCityMapOverlay(city, neighbor);
            flags |= CITY_TILE_SCAN_CHANGED;
            continue;
          }
        }
        break;
      }
    }
  }

  if ((flags & CITY_TILE_SCAN_SAW_RIOTS) && (Sc2kCrtRand(city) & 7) == 0)
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_DEMOLITION);
  if ((flags & CITY_TILE_SCAN_SAW_FLOOD) && (Sc2kCrtRand(city) & 7) == 0)
    PlayCitySound(city, INVALID_TILE, CITY_SOUND_WATER);
  if (flags & CITY_TILE_SCAN_SAW_FIRE)
    PlayCitySoundWithDelay(city->game, CITY_SOUND_FIRE, -1);

  if (city->disasters.event_decay_ticks != 0 && city->disasters.event_cleanup_ticks != 0) {
    if ((Sc2kCrtRand(city) & 7) == 0)
      PlayCitySound(city, INVALID_TILE, CITY_SOUND_EARTHQUAKE);
    if (NextCityMapRandomModulo(city, 2) == 0) {
      y = NextCityMapRandomModulo(city, city->map_height);
      x = NextCityMapRandomModulo(city, city->map_width);
      cell_index = MapTileXY(city, x, y);
      int kind = ReadTile(city, cell_index);
      // Stock excludes the first house kind from this cleanup draw.
      if (IsTileKindBuilding(kind) && kind != TK_LOWER_CLASS_HOMES_FIRST) {
        if (city->settings.auto_goto != 0)
          CenterCityMapOnEvent(city, x, y);
        DemolishCityFootprintForDisaster(city, cell_index, 1, 0);
      }
    }
    --city->disasters.event_cleanup_ticks;
  }

  if (flags & (CITY_TILE_SCAN_CHANGED | CITY_TILE_SCAN_SAW_RIOTS)) RefreshCityMapTiles(city);

  return (flags & (CITY_TILE_SCAN_SAW_FIRE | CITY_TILE_SCAN_SAW_RIOTS |
      CITY_TILE_SCAN_SAW_POLLUTION | CITY_TILE_SCAN_SAW_FLOOD)) != 0;
}
