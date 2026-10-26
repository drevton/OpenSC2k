#include "game/timing.h"
#include "city/maps/environment.h"
#include "city/engine/city_name.h"
#include "city/engine/clock.h"
#include "city/economy/budget.h"
#include "city/engine/simulation_state.h"
#include "city/disasters/types.h"
#include "city/infrastructure/utilities.h"
#include "game/update.h"
#include "city/events/emergency.h"
#include "city/infrastructure/transport.h"
#include "city/growth/growth.h"
#include "city/economy/facilities.h"
#include "city/military.h"
#include "audio/types.h"
#include "city/economy/types.h"
#include "city/engine/command_types.h"
#include "city/events/news.h"
#include "city/maps/tile_storage.h"
#include "city/maps/map.h"
#include "city/tools/types.h"
#include "city/presentation.h"
#include "city/engine/simulation.h"
#include "city/engine/commands.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "game/audio.h"
#include "city/disasters/disasters.h"
#include "city/economy/economy.h"
#include "city/economy/history.h"
#include "city/economy/tables.h"
#include "city/engine/city_types.h"
#include "city/events/events.h"
#include "city/state.h"
#include "city/tools/tools.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "base/text.h"

// Direct commands apply page effects. Edition must be supported;
// applies page effects or retains the diagnostic newspaper caller through its observation boundary.
void SelectCityNewspaperEdition(City *city, int edition) {
  assert(IsCityNewspaperEditionValid(edition));
  city->newspaper.edition_index = edition;
  const GameObservation *observer = city->game->observation;
  if (observer && observer->open_manual_newspaper) observer->open_manual_newspaper(city, edition);
  else ApplyCityNewspaperPageEffects(city, edition);
}

enum { kTickContinuationFirst = 0x40000001, kTickContinuationLast = 0x5FFFFFFF };

static int SuspendCityTick(City *city, CityTickContinuation work);
static i64 CheckCityBankruptcy(City *city, u32 options);
static i64 FinishCityPopulationReward(City *city, u32 options);
static i64 CompleteCityPopulationReward(City *city, u32 options);
static i64 BeginCityMilitaryReward(City *city, u32 options, bool accept);

// Finish the captured milestone, secondary economy and history updates in phase order. Milestone comes from the
// suspended caller; the remaining calculations read live City after any newspaper dismissal.
static void FinishCityEconomyPhase(City *city, int milestone) {
  FinishCityEconomyPrimary(city, milestone);
  UpdateCityEconomySecondary(city);
  RecordCityMonthlyHistory(city);
}

// Recompute month, quarter and year from the live tick, then clear population accumulators. Run only at the monthly
// tail; a dialog may have changed the clock since the phase began.
static void FinishCityMonthlyPhase(City *city) {
  // Dialog continuations can change the clock; reload it before clearing population.
  UpdateCityCalendar(city, (int)city->clock.tick);
  for (int index = 0; index < CITY_POPULATION_BUCKET_COUNT; ++index)
    city->population.value[index] = 0;
}

// Open the scheduled monthly newspaper when enabled, then finish monthly counters. Options choose immediate effects or
// a retained dialog continuation; returns completion, failure or the pending token without repeating economy work.
static i64 ContinueCityMonthlyAfterEconomy(City *city, u32 options) {
  if (city->newspaper.enabled != 0 &&
      (city->clock.month == CITY_MONTH_APRIL || city->clock.month == CITY_MONTH_AUGUST)) {
    const GameObservation *observer = city->game->observation;
    if (observer && observer->open_manual_newspaper) {
      observer->open_manual_newspaper(city, city->newspaper.edition_index);
    } else if (options & CITY_TICK_DEFER_DIALOGS) {
      return GetGameError(city->game) ? 0 : SuspendCityTick(city,
          (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_MONTHLY_NEWSPAPER,
              .edition = city->newspaper.edition_index});
    } else ApplyCityNewspaperPageEffects(city, city->newspaper.edition_index);
  }
  FinishCityMonthlyPhase(city);
  return GetGameError(city->game) == 0;
}

// Continue the captured facility scan through aging newspapers and fiscal notices. Preserves work across each
// suspension and reads live City on resume; returns completion, failure or a continuation token.
static i64 ContinueCityAnnualFacilities(City *city, u32 options, CityAnnualFacilityWork work) {
  while (!AdvanceCityAnnualFacilities(city, &work)) {
    if (options & CITY_TICK_DEFER_DIALOGS)
      return GetGameError(city->game) ? 0 : SuspendCityTick(city,
          (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_AGING_NEWSPAPER,
              .edition = city->newspaper.edition_index, .annual = work});
    ApplyCityNewspaperPageEffects(city, city->newspaper.edition_index);
  }
  int arcology_count = FinishCityAnnualFacilities(city, &work);
  int notice = arcology_count >= 0 ? CITY_NOTICE_ARCOLOGY_EXODUS_BEGUN :
      PrepareCityFiscalWarning(city) ? CITY_NOTICE_FISCAL_CRISIS : 0;
  if (notice && (options & CITY_TICK_DEFER_DIALOGS))
    return GetGameError(city->game) ? 0 : SuspendCityTick(city,
        (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_ECONOMY_NOTICE,
            .notice = notice, .arcology_count = arcology_count});
  if (arcology_count >= 0) {
    CompleteCityArcologyExodus(city, arcology_count);
    PrepareCityFiscalWarning(city);
  }
  FinishCityMonthlyEconomy(city);
  return ContinueCityMonthlyAfterEconomy(city, options);
}

// Run monthly bookkeeping once, then continue annual facilities and the monthly newspaper tail. Observer callbacks may
// replace the stock bookkeeping path; options and signed continuation results must be propagated by the caller.
static i64 CompleteCityMonthlyPhase(City *city, u32 options) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->monthly_bookkeeping)
    observer->monthly_bookkeeping(city);
  else {
    if (BeginCityAnnualSettlement(city))
      return ContinueCityAnnualFacilities(city, options, BeginCityAnnualFacilities(city));
    FinishCityMonthlyEconomy(city);
  }
  return ContinueCityMonthlyAfterEconomy(city, options);
}

// Find a suspended simulation caller by token. Returns a borrowed continuation or NULL; resuming or cancelling that
// token may release the returned object.
const CityTickContinuation *FindCityTickContinuation(const City *city, int token) {
  for (const CityTickContinuation *pending = city->tick_continuations; pending; pending = pending->next)
    if (pending->token == token) return pending;
  return NULL;
}

// Initialize the selected newspaper and retain any opening notice's continuation. Requires a valid edition; returns a
// pending token or failure, with no restart of initialization after dismissal.
i64 BeginCityNewspaperOpening(City *city, int edition) {
  assert(IsCityNewspaperEditionValid(edition));
  city->newspaper.edition_index = edition;
  bool ended = !AreCityNewsResourcesAvailable(city);
  CityNewspaperInitialization state = BeginCityNewspaperOpeningPresentation(city, edition);
  return GetGameError(city->game) ? 0 : SuspendCityTick(city,
      (CityTickContinuation){.dialog = CITY_TICK_DIALOG_NEWSPAPER_INITIALIZATION, .edition = edition,
          .notice = state == CITY_NEWSPAPER_INITIALIZATION_NOTICE ? CITY_NOTICE_MAYOR_PARADE : 0,
          .outcome = (ended ? CITY_NEWSPAPER_INITIALIZATION_ENDED : 0) |
              (state == CITY_NEWSPAPER_INITIALIZATION_SKIPPED ? CITY_NEWSPAPER_INITIALIZATION_SKIP_TAIL : 0)});
}

static i64 ContinueCityDisaster(City *city, u32 options);

// Resume or cancel exactly one suspended simulation caller identified by token. Missing tokens return zero; removes
// its slot before callbacks, uses live City on resume and may return a new pending token instead of final completion.
i64 ResumeCityTick(City *city, int token, bool resume, int answer) {
  CityTickContinuation **link = &city->tick_continuations;
  while (*link && (*link)->token != token) link = &(*link)->next;
  if (!*link) return 0;
  CityTickContinuation *pending = *link;
  CityTickContinuation work = *pending;
  u32 options = work.options;
  CityTickDialog dialog = work.dialog;
  *link = pending->next;
  // Remove the slot before callbacks can pump nested commands. A timed step
  // retains this same caller and token until it reaches its real return.
  i64 result = 1;
  if (resume && dialog == CITY_TICK_ANIMATION) {
    result = ContinueCityDisaster(city, options);
    if (result == CITY_OPERATION_PENDING) {
      pending->next = city->tick_continuations;
      city->tick_continuations = pending;
      return token;
    }
  }
  free(pending);
  if (!resume) {
    if (dialog == CITY_TICK_ANIMATION) {
      CancelGameDelay(city->game, &city->game->animation_delay);
      city->game->animation_token = 0;
      CancelCityDisasterWork(city);
    }
    return 1;
  }
  if (dialog == CITY_TICK_DIALOG_NEWSPAPER_INITIALIZATION) {
    if (!(work.outcome & CITY_NEWSPAPER_INITIALIZATION_SKIP_TAIL))
      FinishCityNewspaperInitialization(city, work.edition);
    return 1;
  } else if (dialog == CITY_TICK_DIALOG_BUDGET) {
    SetCityCursor(city, CITY_CURSOR_ARROW, CITY_CURSOR_SELECT);
    result = CompleteCityMonthlyPhase(city, options);
  } else if (dialog == CITY_TICK_DIALOG_ECONOMY_NOTICE) {
    if (work.notice == CITY_NOTICE_ARCOLOGY_EXODUS_BEGUN) {
      CompleteCityArcologyExodus(city, work.arcology_count);
      return GetGameError(city->game) ? 0 : SuspendCityTick(city,
          (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_ECONOMY_NOTICE,
              .notice = CITY_NOTICE_ARCOLOGY_EXODUS_COMPENSATION});
    }
    if (work.notice == CITY_NOTICE_ARCOLOGY_EXODUS_COMPENSATION && PrepareCityFiscalWarning(city))
      return GetGameError(city->game) ? 0 : SuspendCityTick(city,
          (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_ECONOMY_NOTICE,
              .notice = CITY_NOTICE_FISCAL_CRISIS});
    FinishCityMonthlyEconomy(city);
    result = ContinueCityMonthlyAfterEconomy(city, options);
  } else if (dialog == CITY_TICK_DIALOG_MISSION) {
    FinishCityMissionOutcome(city, work.outcome);
    if (work.outcome != CITY_OUTCOME_BANKRUPT) result = CheckCityBankruptcy(city, options);
  } else if (dialog == CITY_TICK_DIALOG_REWARD_NEWSPAPER) {
    result = FinishCityPopulationReward(city, options);
  } else if (dialog == CITY_TICK_DIALOG_MILITARY_OFFER) {
    result = BeginCityMilitaryReward(city, options, answer == CITY_DIALOG_YES);
  } else if (dialog == CITY_TICK_DIALOG_MILITARY_RESULT) {
    FinishCityMilitaryPlacement(city, work.military);
    result = CompleteCityPopulationReward(city, options);
  } else if (dialog == CITY_TICK_DIALOG_MILESTONE_NEWSPAPER) {
    FinishCityEconomyPhase(city, work.milestone);
  } else if (dialog == CITY_TICK_DIALOG_AGING_NEWSPAPER) {
    result = ContinueCityAnnualFacilities(city, options, work.annual);
  } else if (dialog == CITY_TICK_DIALOG_MONTHLY_NEWSPAPER) FinishCityMonthlyPhase(city);
  if (result != 1) return result;
  if (dialog == CITY_TICK_DIALOG_DISASTER_NEWSPAPER || dialog == CITY_TICK_ANIMATION) {
    if (options & (CITY_TICK_FROM_IDLE | CITY_TICK_FROM_EVENT_IDLE)) CompleteCityIdleSimulation(city->game, options);
  } else if (options & CITY_TICK_FROM_IDLE)
    return FinishCityIdleSimulation(city->game, (options & CITY_TICK_ALLOW_TERRAIN_WATER) != 0);
  return GetGameError(city->game) == 0;
}

// Apply an explicit answer to the retained military offer and continue its result path. Validates answer/payload
// before effects; TEST neither consumes the answer nor searches or zones a military site.
i64 CmdAnswerMilitaryOffer(City *city, u32 flags, TileIndex tile, int token, int answer, int reserved) {
  const CityTickContinuation *work = FindCityTickContinuation(city, token);
  if (tile != INVALID_TILE || !work || work->dialog != CITY_TICK_DIALOG_MILITARY_OFFER || reserved ||
      (answer != CITY_DIALOG_YES && answer != CITY_DIALOG_NO))
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  return ResumeCityTick(city, token, true, answer);
}

// Cancel and release every suspended tick caller owned by City. Cancellation performs each caller's real cleanup,
// including animation cancellation; no continuation pointer survives this operation.
void ReleaseCityTickContinuations(City *city) {
  while (city->tick_continuations)
    ResumeCityTick(city, city->tick_continuations->token, false, 0);
}

// Allocate a continuation for the supplied captured work and assign a fresh token. Returns zero and sets Game's error
// on exhaustion or allocation failure; City owns the pending record until resume or cancellation.
static int SuspendCityTick(City *city, CityTickContinuation work) {
  if (city->next_tick_continuation == kTickContinuationLast) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return 0;
  }
  CityTickContinuation *pending = malloc(sizeof(*pending));
  if (!pending) {
    SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
    return 0;
  }
  int token = IntMax(city->next_tick_continuation + 1, kTickContinuationFirst);
  city->next_tick_continuation = token;
  work.next = city->tick_continuations;
  work.token = token;
  *pending = work;
  city->tick_continuations = pending;
  return token;
}

// Continue disaster effects using explicit terrain-water permission and the active observation boundary. Returns
// pending while animation retains the caller; completion may suspend for its newspaper before idle scheduling resumes.
static i64 ContinueCityDisaster(City *city, u32 options) {
  const GameObservation *observer = city->game->observation;
  bool water = (options & CITY_TICK_ALLOW_TERRAIN_WATER) != 0;
  i64 result = observer && observer->process_disaster ? observer->process_disaster(city, water) :
      AdvanceCityDisasterEffects(city, water);
  if (result == CITY_OPERATION_PENDING) return result;
  if (!(observer && observer->process_disaster) && result) {
    if (options & CITY_TICK_DEFER_DIALOGS)
      return GetGameError(city->game) ? 0 : SuspendCityTick(city,
          (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_DISASTER_NEWSPAPER});
    ApplyCityNewspaperPageEffects(city, CITY_NEWSPAPER_EDITION_FIRST);
  }
  return GetGameError(city->game) == 0;
}

// Advance pending disaster effects using explicit scheduler and terrain-water options. Returns completion, failure or
// a retained pending result; callers must resume a pending caller instead of restarting the disaster.
i64 AdvanceCityDisasterWithOptions(City *city, u32 options) {
  i64 result = ContinueCityDisaster(city, options);
  if (result != CITY_OPERATION_PENDING) return result;
  int token = SuspendCityTick(city, (CityTickContinuation){.options = options, .dialog = CITY_TICK_ANIMATION});
  if (!token) {
    CancelGameDelay(city->game, &city->game->animation_delay);
    CancelCityDisasterWork(city);
  }
  return token;
}

// Present or suspend for the captured mission/bankruptcy outcome. Options select deferred presentation; returns
// completion, failure or a token and never fabricates a dismissed notice.
static i64 BeginCityTickOutcome(City *city, u32 options, int outcome) {
  const GameObservation *observer = city->game->observation;
  if (observer && observer->mission_outcome) observer->mission_outcome(city, outcome);
  else {
    PlayCitySound(city, INVALID_TILE,
        outcome == CITY_OUTCOME_MISSION_WON ? CITY_SOUND_CIVIC_PLACEMENT : CITY_SOUND_DEMOLITION);
    if (options & CITY_TICK_DEFER_DIALOGS)
      return GetGameError(city->game) ? 0 : SuspendCityTick(city,
          (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_MISSION, .outcome = outcome});
    FinishCityMissionOutcome(city, outcome);
  }
  return GetGameError(city->game) == 0;
}

// Test current cash against the bankruptcy threshold and begin its outcome when due. Uses live state after preceding
// notices; returns the outcome continuation result or ordinary completion.
static i64 CheckCityBankruptcy(City *city, u32 options) {
  return city->indicators.cash < -100000 ? BeginCityTickOutcome(city, options, CITY_OUTCOME_BANKRUPT) : 1;
}

// Evaluate active mission criteria, decrement its deadline when unmet and continue to bankruptcy checks. This advances
// shared mission state; any resulting notice retains the caller and must not cause criteria to run twice.
static i64 CheckCityMissionCriteria(City *city, u32 options) {
  if (city->mission.active != 0) {
    MissionCriteria *criteria = &city->mission.metadata.criteria;
    CityEconomyMonths *economy = &city->economy;
    bool achieved = criteria->minimum_population <= city->indicators.population;
    for (int index = 0; index < CITY_ECONOMY_TAX_SECTOR_COUNT; ++index)
      if (economy->sector[index].mission_metric < criteria->minimum_sector_metric[index])
        achieved = false;
    if (city->indicators.cash < (i64)criteria->minimum_net_funds + city->indicators.bond_count)
      achieved = false;
    if (city->indicators.city_land_value < criteria->minimum_city_metric)
      achieved = false;
    if ((u32)criteria->minimum_service_metric[0] > city->indicators.workforce_life_expectancy)
      achieved = false;
    if ((u32)criteria->minimum_service_metric[1] > city->indicators.workforce_education)
      achieved = false;
    u32 environment[3] = {
        city->indicators.city_pollution, city->indicators.city_crime, city->indicators.city_traffic};
    for (int index = 0; index < 3; ++index) {
      int maximum = criteria->maximum_environment_metric[index];
      if (maximum > 0 && environment[index] > (u32)maximum)
        achieved = false;
    }
    for (int index = 0; index < 2; ++index) {
      int statistic_index = criteria->statistic_index[index];
      if (statistic_index != 0 && city->statistics.value[statistic_index] < criteria->minimum_statistic[index])
        achieved = false;
    }
    int outcome = achieved ? CITY_OUTCOME_MISSION_WON :
        --criteria->remaining_ticks == 0 ? CITY_OUTCOME_MISSION_FAILED : -1;
    if (outcome >= 0) {
      i64 result = BeginCityTickOutcome(city, options, outcome);
      if (result != 1) return result;
    }
  }
  return CheckCityBankruptcy(city, options);
}

// Grant the reward selected by the live unlock level after its newspaper closes. A military reward requires an
// explicit answer and may suspend; nested edits are retained rather than overwritten by captured City state.
static i64 FinishCityPopulationReward(City *city, u32 options) {
  // Newspaper dismissal can change the unlock level; grant from the live value.
  int unlock_index = (i16)city->unlocks.disaster_level;
  if (unlock_index == CITY_REWARD_UNLOCK_MILITARY_BASE + 1) {
    const GameObservation *observer = city->game->observation;
    if (observer && observer->military_offer)
      observer->military_offer(city);
    else {
      // Even headless callers must supply a decision; no UI answer is invented.
      int answer = city->unlock_bonus_answer;
      if (answer == CITY_DIALOG_UNANSWERED)
        return GetGameError(city->game) ? 0 : SuspendCityTick(city,
            (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_MILITARY_OFFER});
      city->unlock_bonus_answer = CITY_DIALOG_UNANSWERED;
      return BeginCityMilitaryReward(city, options, answer == CITY_DIALOG_YES);
    }
  } else if (unlock_index == CITY_REWARD_UNLOCK_LLAMA_DOME + 1) {
    SetSpecialFacilityAvailability(city, CITY_REWARD_LLAMA_DOME, 1);
  } else if (unlock_index < CITY_REWARD_UNLOCK_MILITARY_BASE + 1) {
    SetSpecialFacilityAvailability(city, unlock_index - 1, 1);
  }
  return CompleteCityPopulationReward(city, options);
}

// Search/apply the pre-notice military effects once using the explicit acceptance decision. Captures deferred
// placement cleanup for its result notice; returns the continuation result without repeating the search on resume.
static i64 BeginCityMilitaryReward(City *city, u32 options, bool accept) {
  CityMilitaryPlacement placement = BeginCityMilitaryPlacement(city, accept);
  if (placement.notice && (options & CITY_TICK_DEFER_DIALOGS))
    return GetGameError(city->game) ? 0 : SuspendCityTick(city,
        (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_MILITARY_RESULT,
            .military = placement});
  FinishCityMilitaryPlacement(city, placement);
  return CompleteCityPopulationReward(city, options);
}

// Rebuild unlock availability after the reward and continue mission/bankruptcy evaluation. Run once at the reward
// tail; returns any following pending outcome rather than claiming the full tick finished.
static i64 CompleteCityPopulationReward(City *city, u32 options) {
  RebuildCityUnlockState(city);
  SetCityCursor(city, CITY_CURSOR_ARROW, CITY_CURSOR_SELECT);
  return CheckCityMissionCriteria(city, options);
}

// Run one bounded batch of the currently scheduled simulation pass. Returns true when no work remains and false when
// suspended or already running; the cursor and scratch state remain owned by City between calls.
bool StepCitySimulationWork(City *city) {
  CitySimulationWork *work = &city->simulation_work;
  if (!work->phase) return true;
  if (work->running) return false;
  work->running = true;
  bool complete = false;
  enum { kSectionsPerBatch = 16, kMapBatch = kSectionsPerBatch * CITY_MAP_SECTION_CELL_COUNT,
      kGrowthBatch = kMapBatch / 16 };
  if (work->phase == CITY_PHASE_POWER) {
    complete = StepCityPowerGrid(city, &work->utility, kMapBatch);
  } else if (work->phase == CITY_PHASE_WATER_NETWORK) {
    complete = StepCityWaterNetwork(city, &work->utility, kMapBatch);
  } else if (work->phase == CITY_PHASE_ENVIRONMENT) {
    complete = StepCityEnvironment(city, &work->environment, 4 * kMapBatch / city->map_width);
  } else if (work->phase == CITY_PHASE_TRAFFIC_DECAY) {
    if (!work->cursor) {
      InvalidateMapLayer(city, CITY_MAP_CHANGE_TRAFFIC_DENSITY);
      city->indicators.city_traffic = 0;
    }
    const int sections = city->map_cell_count / CITY_MAP_SECTION_CELL_COUNT;
    int end = IntMin(sections, work->cursor + kSectionsPerBatch);
    u32 total = city->indicators.city_traffic;
    for (int section = work->cursor; section < end; ++section)
      total += DecayTrafficDensitySection(city, section);
    city->indicators.city_traffic = total;
    work->cursor = end;
    complete = end == sections;
  } else if (work->phase >= CITY_PHASE_TILE_SCAN_FIRST && work->phase <= CITY_PHASE_TILE_SCAN_LAST) {
    int quarter = work->phase - CITY_PHASE_TILE_SCAN_FIRST;
    int count = IntMin(kGrowthBatch, city->map_cell_count / 16 - work->cursor);
    ScanCityTileBatch(city, quarter % 4, quarter / 4,
        work->cursor, count, work->large_city_effect);
    work->cursor += count;
    complete = work->cursor == city->map_cell_count / 16;
  }
  work->running = false;
  if (complete) *work = (CitySimulationWork){0};
  return complete;
}

// Drain the current simulation pass using its existing working state. Requires a caller outside the pass's running
// callback; completion clears the pass without scheduling a new one.
void FinishCitySimulationWork(City *city) {
  if (city->simulation_work.running) return;
  while (city->simulation_work.phase) StepCitySimulationWork(city);
}

// Advance the city clock, run the periodic-year gate, and dispatch the 25-phase step. Uses the immediate-presentation
// option path; scheduled callers needing deferred dialogs must preserve the returned continuation from the options
// form.
void AdvanceCitySimulationPhase(City *city) {
  AdvanceCitySimulationPhaseWithOptions(city, 0);
}

// Advance the agreed simulation phase using explicit scheduling and presentation options. A pending result retains
// this phase's continuation; callers must resume it rather than advancing or replaying the same phase again.
i64 AdvanceCitySimulationPhaseWithOptions(City *city, u32 options) {
  if (city->simulation_work.phase) {
    StepCitySimulationWork(city);
    return GetGameError(city->game) == 0;
  }
  u32 tick_bits = city->clock.tick + 1;
  int tick = (int)tick_bits;
  city->clock.tick = tick_bits;
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
  AdvanceCityLandscape(city);
  UpdateCityCalendar(city, tick);
  if (city->fixes & (1ull << CITY_FIX_DAILY_VALUATION))
    RecalculateCityAssetValue(city);
  int phase = tick % CITY_PHASE_COUNT;

  if (city->game->spread_simulation && city->map_cell_count > CITY_MAP_CELL_COUNT &&
      (phase == CITY_PHASE_POWER || phase == CITY_PHASE_ENVIRONMENT || phase == CITY_PHASE_WATER_NETWORK ||
          phase == CITY_PHASE_TRAFFIC_DECAY ||
          (phase >= CITY_PHASE_TILE_SCAN_FIRST && phase <= CITY_PHASE_TILE_SCAN_LAST))) {
    city->simulation_work = (CitySimulationWork){.phase = phase,
        .large_city_effect = ((city->fixes & (1ull << CITY_FIX_RESTORED_CHEATS)) && city->cheats.church == 2) ||
            (u32)(2500 * city->statistics.count[TK_CHURCH]) < city->indicators.population};
    StepCitySimulationWork(city);
    return GetGameError(city->game) == 0;
  }

  // Dispatch one phase; scan cases retain their existing x/y residue order.
  switch (phase) {
  case CITY_PHASE_MONTHLY_BOOKKEEPING:
    InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
    if (city->indicators.year_flag != 0) {
      const GameObservation *observer = city->game->observation;
      if (observer && observer->show_annual_budget) observer->show_annual_budget(city);
      else if (PrepareCityAnnualBudget(city)) {
        if (!city->game->ranked_events_suppressed || IsCityOutcomeNoticeActive(city)) {
          RefreshCityBudgetEstimates(city);
          if (options & CITY_TICK_DEFER_DIALOGS)
            return GetGameError(city->game) ? 0 : SuspendCityTick(city,
                (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_BUDGET});
        }
        SetCityCursor(city, CITY_CURSOR_ARROW, CITY_CURSOR_SELECT);
      }
    }
    return CompleteCityMonthlyPhase(city, options);
  case CITY_PHASE_POWER:
    ScanPowerGridPhase(city);
    break;
  case CITY_PHASE_ENVIRONMENT:
    UpdateCityEnvironmentMaps(city);
    break;
  case CITY_PHASE_TILE_SCAN_Y0_X0:
  case CITY_PHASE_TILE_SCAN_Y0_X1:
  case CITY_PHASE_TILE_SCAN_Y0_X2:
  case CITY_PHASE_TILE_SCAN_Y0_X3:
  case CITY_PHASE_TILE_SCAN_Y1_X0:
  case CITY_PHASE_TILE_SCAN_Y1_X1:
  case CITY_PHASE_TILE_SCAN_Y1_X2:
  case CITY_PHASE_TILE_SCAN_Y1_X3:
  case CITY_PHASE_TILE_SCAN_Y2_X0:
  case CITY_PHASE_TILE_SCAN_Y2_X1:
  case CITY_PHASE_TILE_SCAN_Y2_X2:
  case CITY_PHASE_TILE_SCAN_Y2_X3:
  case CITY_PHASE_TILE_SCAN_Y3_X0:
  case CITY_PHASE_TILE_SCAN_Y3_X1:
  case CITY_PHASE_TILE_SCAN_Y3_X2:
  case CITY_PHASE_TILE_SCAN_Y3_X3: {
    int scan = phase - CITY_PHASE_TILE_SCAN_FIRST;
    ScanQuarterCityTiles(city, scan % 4, scan / 4);
    break;
  }
  case CITY_PHASE_TRAFFIC_DECAY:
    DecayTrafficDensity(city);
    break;
  case CITY_PHASE_WATER_NETWORK:
    ScanCityWaterNetwork(city);
    break;
  case CITY_PHASE_ECONOMY: {
    const GameObservation *observer = city->game->observation;
    if (observer && observer->primary_economy) {
      observer->primary_economy(city);
      UpdateCityEconomySecondary(city);
      RecordCityMonthlyHistory(city);
    } else {
      CityMilestoneNotice notice = BeginCityEconomyPrimary(city);
      if (notice.show) {
        if (options & CITY_TICK_DEFER_DIALOGS)
          return GetGameError(city->game) ? 0 : SuspendCityTick(city,
              (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_MILESTONE_NEWSPAPER,
                  .milestone = notice.milestone, .edition = city->newspaper.edition_index});
        ApplyCityNewspaperPageEffects(city, city->newspaper.edition_index);
      }
      FinishCityEconomyPhase(city, notice.milestone);
    }
    break;
  }
  case CITY_PHASE_MISSION_UNLOCK: {
    int unlock_index = city->unlocks.disaster_level;
    if (unlock_index >= 0 && unlock_index < CITY_REWARD_UNLOCK_COUNT) {
      u32 unlock_threshold = kDisasterUnlockPopulationThresholds[unlock_index];
      if (unlock_threshold != 0 && unlock_threshold < city->indicators.population) {
        int prior_index = unlock_index;
        SetCityCursor(city, CITY_CURSOR_REWARDS, CITY_CURSOR_SELECT);
        city->unlocks.disaster_level = prior_index + 1;
        if (PrepareRankedCityEvent(city, CITY_NEWS_POPULATION_REWARD, prior_index)) {
          const GameObservation *observer = city->game->observation;
          if (observer && observer->show_ranked_event)
            observer->show_ranked_event(city, CITY_NEWS_POPULATION_REWARD, prior_index);
          else if (options & CITY_TICK_DEFER_DIALOGS)
            return GetGameError(city->game) ? 0 : SuspendCityTick(city,
                (CityTickContinuation){.options = options, .dialog = CITY_TICK_DIALOG_REWARD_NEWSPAPER,
                    .edition = city->newspaper.edition_index});
          else ApplyCityNewspaperPageEffects(city, city->newspaper.edition_index);
        }
        return FinishCityPopulationReward(city, options);
      }
    }

    return CheckCityMissionCriteria(city, options);
  }
  case CITY_PHASE_VIEW_REFRESH:
    // Periodic presentation consumes changed tiles without discarding retained pixels.
    RefreshCityMapTiles(city);
    InvalidateCityInformation(city->game, CITY_UPDATE_POPULATION | CITY_UPDATE_INDUSTRY | CITY_UPDATE_HISTORY |
        CITY_UPDATE_FINANCES | CITY_UPDATE_POLICIES);
    break;
  case CITY_PHASE_EVENTS_DISASTERS:
    InvalidateCityInformation(city->game, CITY_UPDATE_MAP | CITY_UPDATE_UTILITIES | CITY_UPDATE_NEIGHBORS);
    EvaluateCityProblemsAndRandomDisaster(city);
    break;
  default:
    break;
  }
  return GetGameError(city->game) == 0;
}

// Preserve City transition ordering; UI owns tools, panels and map controls. Mode must select a supported simulation
// mode; transition effects can change shared City state and presentation and must run once.
void SetCityTimerMode(City *city, int mode) {

  if (mode == CITY_TIMER_STOPPED) {
    HideCitySimulationPanels(city);
    SetCityToolsVisible(city->game, 0);
    RebuildCityToolbar(city);
    ShowCityMapControls(city, false);
    MaybePlayCityMusic(city->game, true);
    city->indicators.cash = 0;
    EmptyCityName(city);
    InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
    city->settings.timer_mode = CITY_TIMER_STOPPED;
    RebuildCityToolbar(city);
    SetCityToolsVisible(city->game, 1);
  } else if (mode == CITY_TIMER_RUNNING) {
    SetCityToolsVisible(city->game, 0);
    EvaluateCityProblemsAndRandomDisaster(city);
    RestoreCityMapControls(city);
    InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
    city->settings.timer_mode = CITY_TIMER_RUNNING;
    RebuildCityToolbar(city);
    SetCityToolsVisible(city->game, 1);
    MaybePlayCityMusic(city->game, true);
  } else if (mode == CITY_TIMER_EVENT) {
    InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
    if (city->settings.timer_mode == CITY_TIMER_STOPPED)
      SetCityToolsVisible(city->game, 0);
    InitializeCityEmergencyDispatchState(city);
    MaybePlayCityEventMusic(city->game, CITY_MUSIC_TRACK_04);
    city->settings.timer_mode = CITY_TIMER_EVENT;
    if (city->settings.speed == CITY_SPEED_UNLIMITED)
      city->settings.speed = CITY_SPEED_FAST;
    RebuildCityToolbar(city);
    SetCityToolsVisible(city->game, 1);
  }
  InvalidateCityInformation(city->game, CITY_UPDATE_STATUS);
}

// Enable or disable one gameplay-affecting runtime fix. Rejects presentation-only fixes, invalid indices and reserved
// arguments; TEST has no effects and execution updates City deterministically.
i64 CmdSetCityFix(City *city, u32 flags, int fix, int enabled, int reserved) {
  if ((u32)fix >= CITY_FIX_COUNT ||
      !(CITY_FIX_SIMULATION_MASK & (1ull << fix)) || (u32)enabled > 1 || reserved)
    return CITY_COMMAND_COST_INVALID;
  if (flags & CITY_COMMAND_TEST) return 0;
  if (enabled) city->fixes |= 1ull << fix;
  else city->fixes &= ~(1ull << fix);
  if (fix == CITY_FIX_ASSET_VALUES || fix == CITY_FIX_TREATMENT_VALUE ||
      fix == CITY_FIX_DAILY_VALUATION)
    RecalculateCityAssetValue(city);
  if (fix == CITY_FIX_LOADED_CONNECTIONS && enabled)
    RecountCityIndustryConnections(city);
  return 1;
}
