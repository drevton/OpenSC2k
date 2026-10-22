#include "city/saveload/state.h"
#include "game/observation.h"

#include "city/saveload/runtime.h"
#include "game/state.h"

void ObserveCityPresentation(Game *game, CityPresentationEvent event, int token,
    int first, int second, int answer, const char *text) {
  if (game->observation && game->observation->presentation)
    game->observation->presentation(game, event, token, first, second, answer, text);
}

// Reference file I/O supplies its independently configured observer.
void ObserveCityFileIO(CityFileRuntime *file, int operation, u32 requested, u32 result, int error,
    const void *bytes, u32 size) {
  Game *game = file->game;
  if (game->observation && game->observation->file_io) {
    NativeSaveIOOutcome outcome = {operation, requested, result, error, bytes, size};
    game->observation->file_io(game, &outcome);
  }
}
