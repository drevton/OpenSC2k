#ifndef SC2K_CITY_COMMANDS_H
#define SC2K_CITY_COMMANDS_H

#include "city/engine/input.h"
#include "city/types.h"
#include "city/engine/command_types.h"

// Shared deterministic player actions. UI and scheduling call their domain functions directly.
typedef enum CityCommandId {
  CITY_COMMAND_BUDGET_RATE = 2,
  CITY_COMMAND_PLACE_ZONE_RECT = 5,
  CITY_COMMAND_PLACE_WATER_PIPE = 6,
  CITY_COMMAND_PLACE_SUBWAY = 7,
  CITY_COMMAND_PLACE_ROAD = 8,
  CITY_COMMAND_PLACE_RAIL = 9,
  CITY_COMMAND_PLACE_POWER_LINE = 10,
  CITY_COMMAND_BULLDOZE = 11,
  CITY_COMMAND_APPLY_BULLDOZER_TOOL = 12,
  CITY_COMMAND_ADJUST_TERRAIN_HEIGHT = 13,
  CITY_COMMAND_PLACE_ROAD_TERRAIN_SPAN = 14,
  CITY_COMMAND_TOGGLE_CITY_POLICY = 15,
  CITY_COMMAND_DISASTER_UNLOCK_ANSWER = 16,
  CITY_COMMAND_ISSUE_BOND = 17,
  CITY_COMMAND_REDEEM_BOND = 18,
  CITY_COMMAND_PLACE_TRANSPORT_LINE = 21,
  CITY_COMMAND_PLACE_HIGHWAY_ONRAMP = 25,
  CITY_COMMAND_DISPATCH_CITY_SERVICE = 29,
  CITY_COMMAND_SET_INDUSTRY_TAX_BASELINE = 30,
  CITY_COMMAND_PLACE_SUBWAY_RAIL_CONNECTION = 31,
  CITY_COMMAND_SET_SIMULATION_SPEED = 37,
  CITY_COMMAND_TOGGLE_AUTO_BUDGET = 42,
  CITY_COMMAND_GRANT_SPECIAL_FACILITIES_AND_CASH = 46,
  CITY_COMMAND_CARVE_WATER_CHANNEL = 48,
  CITY_COMMAND_APPLY_WATER_TILE = 49,
  CITY_COMMAND_LOWER_TERRAIN_TILE = 50,
  CITY_COMMAND_RAISE_TERRAIN_TILE = 55,
  CITY_COMMAND_STEP_TERRAIN_HEIGHT = 56,
  CITY_COMMAND_SET_EVENT_STAGE_AT_TILE = 63,
  CITY_COMMAND_EDIT_MAP_LABEL = 71,
  CITY_COMMAND_EDIT_FACILITY_LABEL = 72,
  CITY_COMMAND_ASSIGN_STADIUM_TEAM = 78,
  CITY_COMMAND_TOGGLE_NO_DISASTERS = 111,
  CITY_COMMAND_ADD_CITY_CASH_BONUS = 114,
  CITY_COMMAND_REPLY_TO_QUESTION = 117,
  CITY_COMMAND_RENAME_SIGN = 131,
  CITY_COMMAND_MOVE_SIGN = 132,
  CITY_COMMAND_SET_FIX = 133,
  CITY_COMMAND_CHEAT = 134,
  CITY_COMMAND_ALL_INVENTIONS = 135,
  CITY_COMMAND_BULLDOZE_RECT = 137,
  CITY_COMMAND_SET_TREE_LIFECYCLE = 138,
  CITY_COMMAND_PLANT_TREES_RECT = 139,
  CITY_COMMAND_SET_TERRAIN_PROTECTION = 140,
  CITY_COMMAND_SET_INDIVIDUAL_FACILITIES = 160,
  CITY_COMMAND_BUILD_CANALS_RECT = 148,
  CITY_COMMAND_BUILD_BRIDGE = 149,
  CITY_COMMAND_ENABLE_DEBUG_CITY = 150,
  CITY_COMMAND_RECALL_DISPATCH = 151,
  CITY_COMMAND_START_DISASTER = 152,
  CITY_COMMAND_ANSWER_MILITARY_OFFER = 153,
  // Parameters: CityBudgetRateKind, exact percent (tax 0..20, funding 0..100), zero.
  CITY_COMMAND_SET_BUDGET_RATE = 154,
  // Tile, building TileKind, orientation (-1 automatic or 0..3), zero.
  CITY_COMMAND_PLACE_BUILDING = 155,
  // Finish terrain editing with the accepted city setup; all parameters zero.
  CITY_COMMAND_START_CITY = 156,
  // Tile, absolute land elevation (0..30), CITY_TERRAIN_NO_SEAWATER option, zero.
  CITY_COMMAND_LEVEL_TERRAIN_TILE = 157, CITY_COMMAND_STRETCH_TERRAIN_TILE = 158,
  CITY_COMMAND_PLACE_STREAM = 159,
} CityCommandId;

bool IsCityCommandId(u32 id);

CityCommandArguments DecodeCityCommand(const City *city, CityCommand command);
CityCommand EncodeCityCommand(const City *city, CityCommandId id, TileIndex tile, int parameter1, int parameter2,
    int parameter3);

enum { CITY_TUNNEL_CONFIRMED = 1 << 8 };
enum { CITY_HIGHWAY_FACING_SHIFT = 9, CITY_HIGHWAY_COMMAND_MASK = 0xFFF };

// Line options describe resolved crossing/neighbor choices; no selected tool or price class is transmitted.
enum { CITY_LINE_ACCEPT_BORDER = 1 << 16, CITY_LINE_CANCEL_SPAN = 1 << 17 };

// Apply commands in received order to the supplied city.
// Single-tile commands: tile is the location; parameter1 is the option/tool/answer.
// Lines and landscape rectangles: tile is the start, parameter1 the ending TileIndex,
// parameter2 the options. Zone rectangles: tile is the start, parameter1 is a
// ZoneKind and parameter2 is the ending TileIndex.
// Commands without a map location use INVALID_TILE and their original scalar parameters.
// Example: DoCityCommand(city, CITY_COMMAND_PLACE_BUILDING, 0, tile, TK_SCHOOL, -1, 0).
enum { CITY_COMMAND_TEST = COMMAND_TEST };
// ASK is a TEST query. Execution requires an explicit confirmed option.
enum { CITY_BOND_CONFIRMED = 0, CITY_BOND_ASK = 1 };
// TEST-only control result, outside both prices and negated city message IDs.
enum { CITY_COMMAND_NEEDS_CONFIRMATION = INT32_MIN };

// All command entry points accept u32 flags. TEST returns an i64 nonnegative immediate
// price, a negative error or CITY_COMMAND_NEEDS_CONFIRMATION, without writes, RNG advances,
// dialogs, audio, drawing, file operations or observer notifications. The UI collects a
// required choice before execution and supplies it as a command option; execution validates
// the live state again. Execution returns a negative gameplay error on failure and
// a nonnegative value on success. Free settings/actions query as zero; a question's
// eventual outcome still depends on its reply and live state when executed.
// GetCityConstructionCost retains the price when it reports insufficient funds. Before
// a bridge style is chosen it quotes the cheapest compatible style.
enum { CITY_TERRAIN_POINT_EDIT = 1, CITY_TERRAIN_NO_SEAWATER = 2, CITY_TERRAIN_OPTIONS_MASK = 3 };
// Domain quote functions below return raw work prices and gameplay admission errors.
// DoCityCommand(TEST) and preview pricing apply the shared monetary policy.
i64 DoCityCommand(City *city, CityCommandId id, u32 flags, TileIndex tile, int parameter1, int parameter2,
    int parameter3);
// EDIT_MAP_LABEL: tile is the location; parameter1 is dialog result1/2 (accept/cancel).
// Text is immutable command payload, at most23 Windows-1252 bytes; empty acceptance clears the label.
i64 DoCityCommandWithText(City *city, CityCommandId id, u32 flags, TileIndex tile, int parameter1, int parameter2,
    int parameter3,
    const u8 *text, u32 length);
// EDIT_FACILITY_LABEL: same parameters and payload; facility text is limited to 24 bytes plus NUL.
// ASSIGN_STADIUM_TEAM: tile is the location; parameter1 is team0..4 or -1 for no selection.
// Immutable Windows-1252 team name is limited to24 bytes; no selection preserves game state.
// A command's immutable text payload takes priority over interactive controls.
bool ReadCityNameCommandAnswer(City *city, CityNameCommandInput *state, int *answer);

i64 CityCommandPriceResult(CityCommandCost quote);
// Raw construction costs never inspect editor mode or treasury. These shared entry
// points apply the exemption and live affordability without changing command results.
i64 AffordableCityCommandCost(const City *city, i64 cost);
CityCommandCost ApplyCityCommandCostPolicy(const City *city, CityCommandCost quote);
// Retained selectors and partial-work stages only; ordinary commands pay in DoCityCommand.
i64 PayCityCommandCost(City *city, i64 cost);
// For staged work already admitted by its caller; does not repeat the funds decision.
void DebitCityCommandCost(City *city, i64 cost);

#endif
