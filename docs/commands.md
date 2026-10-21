# City commands and local functions

`RunCityCommand(...)` runs a resolved player action that changes shared deterministic City state.
Construction, demolition, finances, names, gameplay rules, pause and speed use this API. Its `Cmd...`
implementation receives `City`, `u32 flags`, `TileIndex` and actual options. It must give the same
result regardless of local tools, windows, camera, audio or focus.

UI, camera, audio, host files, startup choices and engine scheduling call their domain functions
directly. There is no native local-command registry, local ID namespace or generic local executor.
A menu action opens its window or calls the relevant function; after collecting a bridge style,
for example, it submits the resolved construction through `RunCityCommand`.

```c
SelectCityToolGroupAndMode(game, group, variant);
RunCityCommand(game, CITY_COMMAND_BUILD_BRIDGE, start, end, options, 0, CcBuildBridge, context);
```

`RunCityCommandWithPayload` accepts immutable text. Raw pointer input and creation options stay local
to their actual functions and cannot be supplied to the shared API. Terrain commands carry
corner-editing and seawater-fill choices in `parameter2`, independent of each player's preferences.
Their recorded form stores `(TileIndex, operation, terrain_options)` so replay preserves both choices.

Execution currently begins immediately in single-player and returns whether the action succeeded.
An optional completion callback has this shape:

```c
void CcBuildBridge(Game *game, u32 id, const CityCommandResult *result,
    TileIndex tile, int parameter1, int parameter2, int parameter3, void *context);
```

The result separates the actual handler return (`value`), net treasury debit (`cost`), gameplay error,
host error, cancellation and success. The callback runs once after completion, including retained
animation/notice continuations. Its result pointer is borrowed only for that call. Callback/context
are local data and must never be serialized. A future transport may defer shared execution; callback
lifetime must extend to synchronized completion. No network backend is introduced by these APIs.

`DoCityCommand` executes or queries internally and preserves the handler's `i64` return contract.
`CITY_COMMAND_TEST` queries admission/cost without City writes, RNG, presentation, I/O or observations.
Internal cadence and tick advancement use the scheduler directly; they are not player network commands.
Simulation notices and decisions remain pending events. `DeliverCitySimulationEvents` presents them to UI
and retains the continuation token until dismissal. It does not convert the events into callback records.

Temporary native dialog pauses stop scheduling without changing the City's selected speed. Native
budget/advisor windows keep their session state in UI; opening them does not change financial state.
Manual newspaper reading uses a snapshot. Naming windows collect text locally and submit the committed
shared edit. Explicit simulation decisions, such as a military offer, remain City commands.

Optional stock adapters translate original caller IDs and stock coordinate records at the comparison
boundary. Legacy numeric local inputs and their decoder exist only in `src/city/comparison/local_commands.*`;
the normal game does not compile or reference them. The observer's command hook keeps diagnostic replay
outside the player-command API. Compilation alone does not establish replay coverage.

The existing optional `check_command_domains` source uses direct UI/file functions and shared City
commands. It retains focused state-isolation, construction, callback and save/load checks. Task validation
follows AGENTS.md; compiling this refactor does not claim those checks have been run.

## Construction prices and payment

Domain quote functions calculate raw work prices and admission errors without reading treasury
or editor mode. `DoCityCommand(..., CITY_COMMAND_TEST, ...)` applies the shared monetary policy:
editor construction is free, positive charges must fit the live treasury, and zero-cost work
remains affordable even when cash is negative. Quotes do not modify City or consume RNG.
`ApplyCityCommandCostPolicy` gives previews that same policy while retaining the charge when
funds are insufficient. Financial actions such as bond repayment keep their own debt rules.

`DoCityCommand` checks and pays for ordinary resolved commands: buildings, explicit bridges,
compact highways, onramps, zoning, tree rectangles and native terrain edits. It obtains a raw
TEST price, checks live affordability before execution and debits only after the real action
succeeds. Execution costs remain independent of meaningful handler returns: a bridge's return
is still its construction value even when editor payment is zero. No payment scope or cost
accumulator is stored in City, and nested commands cannot reuse an outer command's price.

Building footprint placement and its finishing phase are separate. The shared handler pays
between them, before stadium presentation can retain the caller. Utility updates, orientation,
rewards and completion observation keep their original order around that continuation.
Rejected geometry still reaches the actual handler for its original blocker/notice and return;
it is never retried after a notice. Building affordability retains its gate before protest RNG.

Retained line/span selectors, tunnel confirmations and neighbor decisions keep payment at their
original continuation points. The optional original toolbar/drag adapters also pay when their
retained caller resumes; native ordinary commands do not enter those adapters. A tunnel reads
its live construction cost after dismissal rather than restoring its opening quote. Legacy
terrain propagation and demolition retain intermediate charges and partial failure, including
tree-protest notices. Canal rectangles charge each actually completed cell because regrading
can change eligibility while traversing the rectangle. An initial quote cannot replace those
actual partial charges. `PayCityCommandCost` checks and pays these stages;
`DebitCityCommandCost` commits an already accepted decision without repeating it after a notice.
Legacy lowering retains its final negative-cash clamp. Nonfinancial editor restrictions, such
as admitting Stretch Terrain only in edit mode, stay with the operation.

## Information updates

Simulation and command code call `InvalidateCityInformation(game, categories)` with a
32-bit mask of semantic changes, such as `CITY_UPDATE_FINANCES | CITY_UPDATE_POLICIES`.
The implementation belongs to UI and only ORs those bits into the existing desktop's
pending mask. A Game without UI discards the notification. Command TEST queries do
not notify; executing commands can notify even after failure because partial changes
may remain.

Each `Window` has an `update_categories` subscription mask. Window traits provide
defaults, and opening a window adds its subscriptions to the pending mask so its first
paint uses current data. Before collecting damage, `PrepareCityInformation` consumes
the pending bits once, marks matching windows dirty, and refreshes their retained UI
data. Other windows read City directly when painted. Status information follows the
same boundary. Notifications never invoke window procedures, paint, advance City or
consume RNG. Existing map tile damage and pending simulation notices remain separate.

## Map presentation

Game commands, tools, disasters and idle simulation do not call
`RedrawDirtyCityMapView` or `PresentCityMapView`. They retain explicit invalidations;
the frame renderer publishes those changes to all map windows. Overlay changes in
the transient-disaster scan invalidate their locations rather than relying on its
former immediate map paint.

Optional map-update observations preserve the stock replay checkpoints, including
its suppressed-paint counters. Only the comparison adapter translates those events
into the original redraw/present calls. With no comparison observer installed, those
observations do no drawing or presentation and do not consume pending invalidations.

Native gameplay never waits for animation or pumps cadence inside a command.
Demolition effects and earthquake screen shake advance from the main loop using
Drawing state. City mutations complete before returning; presentation timing does
not expose partially completed operations to another frame. The original timer
pumps and earthquake wait loop remain only in the optional stock comparison path.

Native timing uses three distinct sources. Deterministic City work keeps its
existing simulation ticks and cadence counters; host presentation clocks never
advance City or consume its RNG. `GAME_TIME_REALTIME` drives UI timeouts, budget
helper delivery and cost labels, including during pause. `GAME_TIME_UNPAUSED`
drives world palette animation, smoke, vehicle interpolation, demolition and
screen shake. It freezes across pause and resumes without a wall-time jump.
The host samples these clocks at frame/input boundaries and rendering entry.
Owner-held deadlines need no registration, heap allocation or callback scheduler.
`PollGameInterval` returns elapsed intervals arithmetically, preserves the next
boundary, and never dispatches commands or runs a catch-up loop. Budget UI chooses
one delivery after a stall; water palette animation consumes the interval count.
