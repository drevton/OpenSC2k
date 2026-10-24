#ifndef SC2K_CITY_COMMANDS_H
#define SC2K_CITY_COMMANDS_H

#include "city/types.h"
#include "city/engine/command_types.h"

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
// EDIT_MAP_LABEL: tile is the location; parameter1 is dialog result 1/2 (accept/cancel).
// Text is immutable command payload, at most 23 Windows-1252 bytes; empty acceptance clears the label.
// EDIT_FACILITY_LABEL: same parameters and payload; facility text is limited to 24 bytes plus NUL.
// ASSIGN_STADIUM_TEAM: tile is the location; parameter1 is team 0..4 or -1 for no selection.
// Immutable Windows-1252 team name is limited to 24 bytes; no selection preserves game state.
// Execution stages the text on City for this call and restores the enclosing command's input afterward.
i64 DoCityCommandWithText(City *city, CityCommandId id, u32 flags, TileIndex tile, int parameter1, int parameter2,
    int parameter3, const u8 *text, u32 length);

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
