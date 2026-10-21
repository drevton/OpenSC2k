#ifndef SC2K_COMMAND_TYPES_H
#define SC2K_COMMAND_TYPES_H

#include "city/maps/map_types.h"
#include "city/types.h"

// Resolved question answers carried by shared commands. Values match stock dialog replies.
enum {
  CITY_DIALOG_UNANSWERED = -1, CITY_DIALOG_NONE = 0, CITY_DIALOG_OK = 1, CITY_DIALOG_CANCEL = 2,
  CITY_DIALOG_YES = 6, CITY_DIALOG_NO = 7,
};

typedef struct Game Game;

// Resolved deterministic City actions.
enum { COMMAND_TEST = 1 << 0 };

typedef struct CommandRecord {
  u32 id;
  i32 parameter1;
  i32 parameter2;
  i32 parameter3;
} CommandRecord;

typedef struct {
  TileIndex tile;
  int parameter1;
  int parameter2;
  int parameter3;
} CommandArguments;

typedef struct {
  i64 value; // Negative gameplay error on failure; nonnegative command value on success.
  i64 cost, error; // Net treasury debit over the call, and gameplay failure, separately from admission.
  i32 host_error;
  bool succeeded, cancelled;
} CommandResult;

typedef void CommandCallback(Game *game, u32 id, const CommandResult *result,
    TileIndex tile, int parameter1, int parameter2, int parameter3, void *context);

typedef enum CityCommandCostError {
  CITY_COMMAND_COST_OK = 0,
  CITY_COMMAND_COST_INVALID = -1,
  CITY_COMMAND_COST_BLOCKED = -2,
  CITY_COMMAND_COST_TOO_EXPENSIVE = -3,
  CITY_COMMAND_COST_REJECTED = -4,
  CITY_COMMAND_COST_WRONG_TREE_CLIMATE = -5,
  CITY_COMMAND_COST_TREE_LIMIT = -6,
  CITY_COMMAND_COST_NOTHING_TO_REMOVE = -7,
  CITY_COMMAND_COST_ALREADY_BUILT = -8,
  CITY_COMMAND_COST_REMOVE_RAIL = -9,
  CITY_COMMAND_COST_REMOVE_ROAD = -10,
  CITY_COMMAND_COST_REMOVE_POWER_LINE = -11,
  CITY_COMMAND_COST_REMOVE_BUILDING = -12,
  CITY_COMMAND_COST_REMOVE_UNDERGROUND = -13,
  CITY_COMMAND_COST_ALREADY_AT_SEA_LEVEL = -14,
  CITY_COMMAND_COST_BRIDGE_SAME_SPOT = -15,
  CITY_COMMAND_COST_CLEAR_DEEP_WATER = -16,
} CityCommandCostError;

typedef struct {
  i64 cost;
  int error; // Cost error above, or a specific city message ID.
  TileIndex error_tile; // Separate from the reason; INVALID_TILE when there is no specific blocker.
} CityCommandCost;

#endif
