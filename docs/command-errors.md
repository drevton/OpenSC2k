# User command errors

OpenTTD's `CommandHelperBase::InternalPostResult` receives an action summary separately
from `CommandCost`'s detailed reason, and `ShowErrorMessage` combines them in one window:
https://docs.openttd.org/source/d1/d81/command_8cpp_source
https://docs.openttd.org/source/d8/d22/error__gui_8cpp_source

The native frontend follows that separation without changing legacy command results or
adding presentation bits to `CITY_COMMAND_TEST`:

- `DispatchCityCanvasEvent` scopes feedback to actual input. Nested input gets its own
  command depth; idle, previews and diagnostic scripts do not open failure windows.
- `CityCommandFailureSummary` provides action context centrally. Construction uses the
  existing read-only TEST query before execution as a fallback, including unavailable
  actions. `ReportCityCommandResult` records actual tool outcomes in a stack-scoped,
  UI-owned `CityCommandFeedback`. This takes precedence over a preview quote, reports
  money running out after partial work, and preserves zero-cost success. Tool failures
  are never inferred from legacy admission results or a zero price.
- `CityCommandFailureReason` translates cost errors or existing resource IDs. Tree
  quotes distinguish incompatible climate, a full tree tile, and no trees to remove.
  Bulldozing distinguishes no eligible tiles from eligible grass that costs nothing.
- Existing command/resource and file notices use the same renderer. Their caller and
  text ownership lifetimes remain intact. Each command records whether its own explicit
  notice explained the failure; an error from nested input cannot hide its parent's
  failure. Allocation failures on interactive commands also receive an error window
  while preserving the diagnostic error and unwind behavior.
- `OpenCityCommandErrorWindow` replaces ordinary failure windows. It never replaces
  a notice retaining a simulation caller. Ordinary errors do not pause City and close
  through the desktop's close button or Escape. Both text blocks use interface-font
  metrics, word wrapping and native UI scaling.

Terrain editor brushes retain their zero-cost TEST contract: random offsets and
propagated changes are checked during execution without advancing RNG in previews.
Raise/lower and forest brushes report when every actual attempt is blocked. A stretch
without a height change remains a normal no-op. All-blocked legacy tree/water and
demolition quotes return an error, including when eligible demolition would be free.
Duplicate mayor's houses have a specific reason.

Only a tile away from the action's target that prevents the operation gets a marker.
`ReportCityCommandBlockedByTile` supplies that explicit blocker; ordinary reports and
fallback quotes never guess a location. Recursive landscaping reports the military or
water tile blocking its affected terrain; the terrain editor identifies the lower
neighbour preventing a raise. Target refusals, unsuitable ground and insufficient funds
show only the dialog. `Game.error_tile` retains the blocker, with its
error-window identity in UI. Drawing reuses the red OpenGFX2
selection sprite at the current terrain slope, elevation and zoom, clipped below
windows. Replacing or closing that window damages only the old/new marker bounds.
Replacing or rotating the city clears it. This presentation state is never saved or
included in simulation equality.

`CancelCityCommandFeedback` keeps Shift-cancelled drags, declined tunnel/bond questions,
and cancelled span choices quiet. Feedback scopes are restored across nested input and
suspended during pending simulation work. Save/load/replacement cancellation still uses
the existing explicit file notices instead of interpreting a false return as failure.

To add a user command, give it a summary in the central function and report actual tool
outcomes or retain an existing explicit failure notice. Inspect, newspaper, information
panels, chart controls, manual budget and advisor requests have central summaries.
Do not treat arbitrary command return values as gameplay success: some are admission
or setting values. Diagnostic scripts and background simulation remain quiet.

Normal bond issue/redemption now queries TEST with `CITY_BOND_ASK`. Eligible actions
return `CITY_COMMAND_NEEDS_CONFIRMATION`; the UI opens a nonmodal confirmation that
owns its prompt and pending command. Yes rechecks TEST with `CITY_BOND_CONFIRMED`,
then executes the complete command. No/close discards it. Ineligible actions show an
ordinary error without entering a bond routine or its retained rejection notice.
Diagnostic UI recording retains the audited ASK caller through the optional
presentation override in `src/city/comparison/observation.c`.

The bond handlers themselves require the explicit `CITY_BOND_CONFIRMED` option.
Executing `CITY_BOND_ASK` without TEST rejects the request without opening a window
or changing City. The UI, a script or a future network caller can submit the same
confirmed command; neither handler reads active UI commands, dialog answers or
presentation state. Validation and the debt/cash effects use current City state.
The shared command wrapper permits absent UI/Drawing owners for these commands
when no simulation phase is pending. This does not yet make the simulation loop or
all other command families usable without presentation owners.

Original bond rejection notices and suspended ASK callers live in
`src/city/comparison/bonds.c`. That adapter shares the City mutation functions, including
the retained oldest rate and signed queue shift, while preserving diagnostic
admission results. It is selected only by an optional diagnostic override; the
normal build has no dependency on comparison sources.

Validation: Release and comparison libraries compile; interactive error/cancellation
flows have not been exercised as part of this change.

Native Stretch Terrain is available in terrain edit mode. Grab a tile and drag vertically;
each changed target elevation submits a command immediately, so terrain follows the drag.
The gesture keeps its original tile, elevation and pixels per height step. It does not
repick the tile beneath the moved pointer. Returning toward the initial height updates the
terrain again. Release applies only a final, previously unapplied target. Shift or Ctrl
suppresses further height changes; preceding live edits remain applied. Ctrl held at
mouse-down retains the ordinary demolition gesture. A blocked stretch stops the gesture,
so later motion or release cannot repeat the rejected command. An unchanged height is a no-op.

Native Level Terrain is available in terrain edit and city modes. Mouse-down flattens the
starting tile immediately; dragging levels newly included strips of the inclusive rectangle
as the pointer moves. The UI captures the starting ground elevation before the first edit
and keeps it for the entire gesture. It retains only the previous selection rectangle and
pointer position, so stationary motion and release do not repeat completed work. Shrinking
the selection leaves earlier live edits applied. Shift or Ctrl suppresses further changes.
Each new strip lowers high tiles first, then raises low tiles. Each changed tile submits a
resolved `CITY_COMMAND_LEVEL_TERRAIN_TILE`; its four corners
reach the target in one command. Every command quotes and executes the same bounded plan,
including surrounding slope changes. Editing is free; city mode charges terrain costs.
The selection preview shows the area and the endpoint command's current admission, not a
combined price or a guarantee that the entire area is clear. A blocked or unaffordable
command stops the gesture; preceding successful commands remain applied and charged.
Neither tool allocates map-sized planning storage. `CITY_COMMAND_STRETCH_TERRAIN_TILE`
requires terrain edit mode, while the level command is also admitted during city play.

Terrain edit mode uses a dedicated toolbar: Start City, file/settings and map controls,
centering, Inspect Tile, raise/lower/stretch/level, trees, water and clearing. RCI demand
bars and city management controls are omitted. Inspect Tile remains available from its
button and the existing Help/keyboard route. Disabled controls retain their own artwork
with a gray treatment, including wide bridge icons; no duplicate disabled bitmap cache is
allocated. Ordinary pointer focus raises windows without the explicit attention highlight,
so dragging any window or clicking map filters does not flash the window border. Explicit
menu actions that locate an existing window retain their brief attention highlight.

The editor's rightmost status field is informational: it does not register button feedback
or accept the finances action. Native manual budget opening also rejects terrain edit mode,
so alternate callers cannot open finances before the city starts. City-mode treasury clicks
retain their Budget action.
