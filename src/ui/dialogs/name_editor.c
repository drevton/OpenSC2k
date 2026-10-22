#include "city/maps/labels.h"
#include "ui/windows/window_internal.h"
#include "ui/dialogs/name_editor.h"
#include "ui/dialogs/information.h"

#include <string.h>

#include "assets/resource_types.h"
#include "assets/resources.h"
#include "assets/state.h"
#include "base/text.h"
#include "city/economy/facilities.h"
#include "city/engine/questions.h"
#include "city/maps/layers.h"
#include "city/maps/map.h"
#include "city/presentation.h"
#include "city/state.h"
#include "drawing/fonts/fonts.h"
#include "drawing/raster/canvas_primitives.h"
#include "game/commands.h"
#include "game/errors.h"
#include "game/observation.h"
#include "game/state.h"
#include "ui/dialogs/alerts.h"
#include "ui/dialogs/dialogs.h"
#include "ui/input.h"
#include "ui/map.h"
#include "ui/sdl.h"
#include "ui/signs.h"
#include "ui/state.h"
#include "ui/windows/controls.h"
#include "ui/windows/windows.h"

enum CityNameEditorControl { CITY_NAME_EDITOR_INPUT, CITY_NAME_EDITOR_OK, CITY_NAME_EDITOR_CANCEL };

static CityRect CityNameEditorBounds(const Window *window, int control) {
  CityRect b = window->bounds;
  if (window->kind == CITY_CANVAS_STADIUM_TEAM) {
    if (control == CITY_NAME_EDITOR_INPUT) return (CityRect){b.left + 9, b.top + 28, b.right - 9, b.top + 51};
    if (control == CITY_NAME_EDITOR_OK) return (CityRect){b.left + 33, b.bottom - 29, b.left + 95, b.bottom - 10};
    return (CityRect){b.right - 95, b.bottom - 29, b.right - 33, b.bottom - 10};
  }
  if (control == CITY_NAME_EDITOR_INPUT) return (CityRect){b.left + 2, b.top + 16, b.right - 2, b.top + 30};
  int middle = (b.left + b.right) / 2;
  if (control == CITY_NAME_EDITOR_OK) return (CityRect){b.left, b.bottom - 14, middle, b.bottom};
  return (CityRect){middle, b.bottom - 14, b.right, b.bottom};
}

static void CanvasDrawLabelEditor(Canvas *canvas, const Window *window) {
  CityRect b = window->bounds;
  DrawCanvasFrame(canvas, (CityRect){b.left, b.top + 14, b.right, b.bottom},
      canvas_face, canvas_light, canvas_dark, true);
  DrawCityWindowCaption(canvas, b, window->title, canvas_face, canvas_light, canvas_dark, 0);
  DrawCityTextBox(canvas, CityNameEditorBounds(window, CITY_NAME_EDITOR_INPUT), window->message,
      window->text_caret, window->text_scroll, true, window->text_select_all, true);
  for (int control = CITY_NAME_EDITOR_OK; control <= CITY_NAME_EDITOR_CANCEL; ++control) {
    CityRect button = CityNameEditorBounds(window, control);
    DrawCanvasButtonFrame(canvas, button, canvas_face, canvas_light, canvas_dark, true);
    DrawCityInterfaceTextCentered(canvas->interface_font, canvas, button,
        control == CITY_NAME_EDITOR_OK ? "OK" : "Cancel", canvas_text, false, 10);
  }
}

static i32 Sc2kCanvasStadiumTeamAt(const Window *window, i32 x, i32 y) {
  if (window->kind != CITY_CANVAS_STADIUM_TEAM || x < window->bounds.left + 9 ||
      x >= window->bounds.right - 9)
    return -1;
  i32 row = 0;
  for (i32 team = 0; team < CITY_TEAM_NAME_COUNT; ++team) {
    if (!(window->stadium_team_choices & (1u << team)))
      continue;
    i32 top = window->bounds.top + 62 + row++ * 18;
    if (y >= top && y < top + 17)
      return team;
  }
  return -1;
}

static void CanvasDrawStadiumTeamEditor(Canvas *canvas, const Window *window) {
  CanvasDrawLabelEditor(canvas, window);
  i32 row = 0;
  for (i32 team = 0; team < CITY_TEAM_NAME_COUNT; ++team) {
    if (!(window->stadium_team_choices & (1u << team)))
      continue;
    i32 top = window->bounds.top + 62 + row++ * 18;
    i32 selected = team == window->stadium_team_selection;
    DrawCanvasButtonBox(canvas, window->bounds.left + 9, top, window->bounds.right - 9, top + 17,
        selected ? canvas_title : canvas_light, !selected);
    CanvasTextAt(canvas, window->bounds.left + 14, top + 3, window->stadium_team_labels[team],
        selected ? canvas_light : canvas_text);
  }
}

static void OpenCityLabelEditor(City *city, NativeLabelDialog *pending, const CityOverlayName *name, bool facility) {
  CopyText(pending->name, sizeof(pending->name), (const char *)name->character);
  Window window = {0};
  window.kind = CITY_CANVAS_LABEL_EDIT;
  int left = IntMax(0, (city->game->ui->desktop.canvas_width - CITY_SIGN_EDITOR_WIDTH) / 2);
  int top = IntMax(18, (city->game->ui->desktop.canvas_height - 50) / 2);
  window.bounds = (CityRect){left, top, left + CITY_SIGN_EDITOR_WIDTH, top + 50};
  window.title = facility ? "Facility name" : "Map label";
  window.message = pending->name;
  window.text_caret = strlen(pending->name);
  int opened = Sc2kCanvasDesktopOpen(&city->game->ui->desktop, window);
  if (opened >= 0)
    pending->window_id = city->game->ui->desktop.windows[opened].window_id;
  else if (facility) {
    CityCommand cancel = pending->command;
    cancel.parameter2 = CITY_DIALOG_CANCEL;
    ExecuteRecordedCityCommandWithPayload(city->game, cancel, NULL, 0, (const u8 *)"", 0);
  }
  pending->opening = false;
  ApplicationUpdateDialogPause(city);
}

static void FinishCityFacilityEditor(City *city, NativeLabelDialog *pending, TileIndex tile, int token, i64 result) {
  if (result == CITY_FACILITY_OPENING_READY) {
    const CityQuestion *question = FindCityQuestion(city, token);
    pending->command = (CityCommand){CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_OK, 0};
    OpenCityLabelEditor(city, pending, &question->context.facility_name.original_name, true);
  } else {
    pending->opening = false;
    if (result == CITY_FACILITY_OPENING_UNAVAILABLE && IsMapTileValid(city, tile))
      PresentCityTileInformationDialog(city, tile);
  }
}

static void CompleteCityFacilityNotice(City *city, void *context) {
  NativeLabelDialog *pending = context;
  TileIndex tile = pending->facility_opening.tile;
  int token = pending->facility_opening.token;
  bool resume = !city->game->close_requested && !city->game->execution_failed;
  i64 result = ResumeCityFacilityNameQuestion(city, token, resume);
  if (resume) FinishCityFacilityEditor(city, pending, tile, token, result);
  else pending->opening = false;
  ApplicationUpdateDialogPause(city);
}

void PresentCityLabelEditor(City *city, TileIndex tile, bool facility) {
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  ApplicationUpdateDialogPause(city);
  if (!IsMapTileValid(city, tile))
    return;
  int slot = facility ? ReadCityFacilityOverlay(city, tile) : ReadCitySignOverlay(city, tile);
  if (facility && !CityTileHasFacilityLabel(city, tile)) {
    PresentCityTileInformationDialog(city, tile);
    return;
  }
  if (!facility &&
      (!CanEditCityMapSign(city, tile) ||
          (slot == CITY_OVERLAY_NONE && FindFreeOverlayNameSlot(city) == 0))) {
    ExecuteRecordedCityCommandWithPayload(city->game, (CityCommand){CITY_COMMAND_EDIT_MAP_LABEL, x, y, 1}, NULL, 0,
        (const u8 *)"", 0);
    return;
  }
  for (i32 index = 0; index < CITY_NAME_EDITOR_CAPACITY; ++index) {
    NativeLabelDialog *pending = &city->game->ui->label_dialogs[index];
    if (pending->window_id || pending->continuation || pending->opening)
      continue;
    pending->opening = true;
    pending->command = (CityCommand){facility ? CITY_COMMAND_EDIT_FACILITY_LABEL : CITY_COMMAND_EDIT_MAP_LABEL,
        x, y, 1};
    if (facility && !city->game->observation) {
      OpenCityLabelEditor(city, pending, CityOverlayNameAt(city, slot), true);
      return;
    }
    if (facility) {
      if (city->game->ui->next_label_transaction == 0x7FFFFFFF)
        city->game->ui->next_label_transaction = 0;
      i32 token = ++city->game->ui->next_label_transaction;
      const GameObservation *observer = city->game->observation;
      i64 result = observer && observer->begin_facility_name ? observer->begin_facility_name(city, token, tile) :
          BeginCityFacilityNameQuestion(city, token, tile);
      if (result == CITY_FACILITY_OPENING_NOTICE) {
        pending->facility_opening.tile = tile;
        pending->facility_opening.token = token;
        int opened = CreateCityResourceAlert(city, CITY_BITMAP_NOTICE_407, CITY_NOTICE_MAYOR_PARADE);
        u32 window = opened >= 0 ? city->game->ui->desktop.windows[opened].window_id : 0;
        if (window) opened = FocusCityCanvasWindow(&city->game->ui->desktop, window);
        if (opened < 0 || !AttachCityWindowCallback(&city->game->ui->desktop.windows[opened],
            CompleteCityFacilityNotice, pending)) {
          if (window) Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window);
          ResumeCityFacilityNameQuestion(city, token, false);
          pending->opening = false;
          SetGameError(city->game, SC2K_ERROR_HOST_ALLOCATION_FAILURE);
          city->game->execution_failed = true;
        } else ApplicationUpdateDialogPause(city);
      } else {
        FinishCityFacilityEditor(city, pending, tile, token, result);
      }
      return;
    }
    int name_slot = slot ? slot : FindFreeOverlayNameSlot(city);
    CityOverlayName empty = {0};
    OpenCityLabelEditor(city, pending, slot ? CityOverlayNameAt(city, name_slot) : &empty, false);
    return;
  }
}

void AnswerCityLabelEditor(City *city, NativeLabelDialog *pending, i32 accept) {
  CityCommand packet = pending->command;
  u32 window_id = pending->window_id;
  char name[CITY_OVERLAY_NAME_BYTES];
  memcpy(name, pending->name, sizeof(name));
  if (pending->continuation || packet.id == CITY_COMMAND_REPLY_TO_QUESTION)
    packet.parameter2 = accept ? 1 : 2;
  else
    packet.parameter3 = accept ? 1 : 2;
  if (!pending->continuation) {
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window_id);
    if (pending->window_id == window_id) pending->window_id = 0;
    ApplicationUpdateDialogPause(city);
  }
  ExecuteRecordedCityCommandWithPayload(city->game, packet, NULL, 0,
      (const u8 *)name, (u32)strlen(name));
}

void PresentCityStadiumTeamDialog(City *city, TileIndex tile) {
  int x = MapTileX(city, tile), y = MapTileY(city, tile);
  ApplicationUpdateDialogPause(city);
  for (i32 index = 0; index < CITY_NAME_EDITOR_CAPACITY; ++index) {
    NativeStadiumDialog *pending = &city->game->ui->stadium_dialogs[index];
    if (pending->window_id || pending->retained)
      continue;
    if (city->game->ui->next_label_transaction == 0x7FFFFFFF)
      city->game->ui->next_label_transaction = 0;
    int token = ++city->game->ui->next_label_transaction;
    int available = city->infrastructure.stadium_team_mask == CITY_TEAM_MASK_ALL ? CITY_TEAM_MASK_ALL :
        ~city->infrastructure.stadium_team_mask & CITY_TEAM_MASK_ALL;
    if (city->game->observation) {
      if (!QueueCityStadiumTeamQuestion(city, token, tile)) return;
      const CityQuestion *question = FindCityQuestion(city, token);
      available = question->context.stadium_team.available_teams;
      pending->command = (CityCommand){CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_OK, -1};
    } else {
      pending->command = EncodeCityCommand(city, CITY_COMMAND_ASSIGN_STADIUM_TEAM, tile, -1, 0, 0);
    }
    pending->name[0] = '\0';
    pending->answer = CITY_DIALOG_UNANSWERED;
    Window window = {0};
    window.kind = CITY_CANVAS_STADIUM_TEAM;
    window.bounds = (CityRect){175, 100, 425, 310};
    window.title = "Stadium team";
    window.message = pending->name;
    window.text_caret = strlen(pending->name);
    window.stadium_team_choices = available;
    window.stadium_team_selection = -1;
    for (i32 team = 0; team < 5; ++team)
      window.stadium_team_labels[team] = kCitySports[team];
    i32 opened = Sc2kCanvasDesktopOpen(&city->game->ui->desktop, window);
    if (opened >= 0)
      pending->window_id = city->game->ui->desktop.windows[opened].window_id;
    else if (city->game->observation)
      ExecuteRecordedCityCommand(city->game, CITY_COMMAND_REPLY_TO_QUESTION, token, CITY_DIALOG_CANCEL, -1);
    ApplicationUpdateDialogPause(city);
    return;
  }
}

// Keep the complete game placement caller on its stack while canvas events run.
void PresentCityStadiumPlacementDialog(City *city, TileIndex tile) {
  CityCanvasStadiumRequest request = {CITY_CANVAS_STADIUM_DIALOG_REQUEST, tile};
  RunPausedCityModalDialog(city, &request);
}

void SelectCityStadiumTeam(City *city, NativeStadiumDialog *pending, Window *window, int team) {
  window->stadium_team_selection = team;
  if (pending->command.id == CITY_COMMAND_ASSIGN_STADIUM_TEAM) pending->command.parameter1 = team;
  else pending->command.parameter3 = team;
  CopyText(pending->name, sizeof(pending->name),
      (const char *)CityOverlayNameAt(city, CITY_TEAM_NAME_FIRST + team)->character);
  Sc2kDirtyMarkRect(&city->game->ui->desktop.dirty, window->bounds);
}

void AnswerCityStadiumEditor(City *city, NativeStadiumDialog *pending, bool accept) {
  CityCommand packet = pending->command;
  u32 window_id = pending->window_id;
  char name[CITY_OVERLAY_NAME_BYTES];
  memcpy(name, pending->name, sizeof(name));
  int answer = accept ? CITY_DIALOG_OK : CITY_DIALOG_CANCEL;
  if (packet.id == CITY_COMMAND_ASSIGN_STADIUM_TEAM) {
    if (accept && ExecuteRecordedCityCommandWithPayload(city->game, packet, NULL, 0,
        (const u8 *)name, strlen(name)) < 0) return;
  } else {
    packet.parameter2 = answer;
    if (ExecuteRecordedCityCommandWithPayload(city->game, packet, NULL, 0, (const u8 *)name,
        (u32)strlen(name)) < 0)
      return;
    if (FindCityQuestion(city, packet.parameter1) != NULL)
      return;
  }
  // A command may close the editor and reuse its fixed slot for a different window.
  if (pending->window_id != window_id) return;
  pending->answer = answer;
  Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, window_id);
  if (pending->window_id == window_id) pending->window_id = 0;
  ApplicationUpdateDialogPause(city);
}


bool AnswerCityOverlayNameDialog(City *city, i32 token, i32 answer, const u8 *text, u32 length) {
  for (i32 index = 0; index < CITY_NAME_EDITOR_CAPACITY; ++index) {
    NativeLabelDialog *pending = &city->game->ui->label_dialogs[index];
    if (!pending->continuation || pending->answer || pending->command.parameter1 != token)
      continue;
    if (length > CITY_OVERLAY_NAME_MAX_LENGTH)
      return false;
    if (length)
      memmove(pending->name, text, length);
    pending->name[length] = '\0';
    pending->answer = answer;
    Sc2kCanvasDesktopDismissWindow(&city->game->ui->desktop, pending->window_id);
    pending->window_id = 0;
    ApplicationUpdateDialogPause(city);
    return true;
  }
  return false;
}


// Edit bounded labels and stadium names. Answers may close the window and resume
// a retained caller, so no window pointer is used after submitting an answer.
bool NameEditorWindowProcedure(Game *game, const Window *window, const WindowEvent *event) {
  if (event->kind == WINDOW_PAINT) {
    if (window->kind == CITY_CANVAS_LABEL_EDIT) CanvasDrawLabelEditor(event->canvas, window);
    else CanvasDrawStadiumTeamEditor(event->canvas, window);
    return true;
  }
  if (event->kind != WINDOW_INPUT) return false;
  City *city = game->city;
  CityCanvasDesktop *desktop = &game->ui->desktop;
  const CanvasSDLEvent *input = event->input;
  NativeLabelDialog *label = NULL;
  NativeStadiumDialog *stadium = NULL;
  for (int i = 0; i < CITY_NAME_EDITOR_CAPACITY; ++i) {
    if (window->kind == CITY_CANVAS_LABEL_EDIT && game->ui->label_dialogs[i].window_id == window->window_id)
      label = &game->ui->label_dialogs[i];
    if (window->kind == CITY_CANVAS_STADIUM_TEAM && game->ui->stadium_dialogs[i].window_id == window->window_id)
      stadium = &game->ui->stadium_dialogs[i];
  }
  if (!label && !stadium) return false;
  Window *target = FindCityCanvasWindow(desktop, window->window_id);
  char *name = label ? label->name : stadium->name;
  CityRect field = CityNameEditorBounds(window, CITY_NAME_EDITOR_INPUT);
  int answer = -1;
  if (input->kind == CANVAS_SDL_KEY_DOWN) {
    CityHotkey key = input->key;
    if (key == CITY_KEY_RETURN || key == CITY_KEY_ESCAPE) answer = key == CITY_KEY_RETURN;
    else if (HandleCityTextEditKey(name, &target->text_caret, &target->text_select_all, key))
      Sc2kDirtyMarkRect(&desktop->dirty, field);
    else if (stadium && (key == CITY_KEY_UP || key == CITY_KEY_DOWN)) {
      int direction = key == CITY_KEY_DOWN ? 1 : -1;
      int start = target->stadium_team_selection;
      if (start == -1 && direction < 0) start = 5;
      for (int team = start + direction; team >= 0 && team < 5; team += direction)
        if (target->stadium_team_choices & (1 << team)) {
          SelectCityStadiumTeam(city, stadium, target, team);
          break;
        }
    }
  } else if (input->kind == CANVAS_SDL_POINTER_DOWN && input->button == CANVAS_SDL_BUTTON_LEFT) {
    CityRect b = window->bounds;
    if (CanvasCloseAt(b, event->x, event->y)) answer = 0;
    else if (CanvasPointInRect(CityNameEditorBounds(window, CITY_NAME_EDITOR_OK), event->x, event->y)) answer = 1;
    else if (CanvasPointInRect(CityNameEditorBounds(window, CITY_NAME_EDITOR_CANCEL), event->x, event->y)) answer = 0;
    if (answer < 0) {
      int team = stadium ? Sc2kCanvasStadiumTeamAt(target, event->x, event->y) : -1;
      if (team >= 0) SelectCityStadiumTeam(city, stadium, target, team);
      else if (CanvasPointInRect(field, event->x, event->y)) {
        target->text_caret = CityTextBoxCaretAt(&game->resources->interface_font, field,
            name, target->text_caret, &target->text_scroll, event->x);
        target->text_select_all = 0;
        Sc2kDirtyMarkRect(&desktop->dirty, field);
      } else return false;
    }
  } else if (input->kind == CANVAS_SDL_TEXT_INPUT) {
    InsertCityTextInput(name, CITY_OVERLAY_NAME_MAX_LENGTH + 1,
        &target->text_caret, &target->text_select_all, input->text, false);
    Sc2kDirtyMarkRect(&desktop->dirty, field);
  } else return false;
  if (answer >= 0) {
    if (label) AnswerCityLabelEditor(city, label, answer);
    else AnswerCityStadiumEditor(city, stadium, answer);
    ApplicationUpdateDialogPause(city);
  }
  return true;
}

i32 QueryCityOverlayNameDialog(City *city, int slot, CityNameCommandInput *state) {
  CityCanvasNameRequest request = {CITY_CANVAS_NAME_DIALOG_REQUEST, slot, state};
  i32 answer = RunPausedCityModalDialog(city, &request);
  return answer;
}

// Detect closures without callbacks or City commands. The caller reconciles the pause before submitting replies,
// so nested commands see all closed editor slots cleared and cannot make this pass collect them again.
bool CollectClosedCityNameEditors(CityInterfaceState *ui, CityEditorCancellations *cancelled) {
  cancelled->stadium_count = cancelled->label_count = 0;
  bool any_open = false;
  for (i32 index = 0; index < CITY_NAME_EDITOR_CAPACITY; ++index) {
    NativeStadiumDialog *pending = &ui->stadium_dialogs[index];
    if (!pending->window_id)
      continue;
    bool open = FindCityCanvasWindow(&ui->desktop, pending->window_id) != NULL;
    if (!open) {
      CityCommand cancel = pending->command;
      cancel.parameter2 = CITY_DIALOG_CANCEL;
      if (cancel.id == CITY_COMMAND_REPLY_TO_QUESTION)
        cancelled->stadiums[cancelled->stadium_count++] = cancel;
      pending->window_id = 0;
      pending->answer = CITY_DIALOG_CANCEL;
    }
    any_open |= open;
  }
  for (i32 index = 0; index < CITY_NAME_EDITOR_CAPACITY; ++index) {
    NativeLabelDialog *pending = &ui->label_dialogs[index];
    if (!pending->window_id)
      continue;
    bool open = FindCityCanvasWindow(&ui->desktop, pending->window_id) != NULL;
    if (!open && !pending->opening && !pending->continuation && pending->command.id == CITY_COMMAND_REPLY_TO_QUESTION) {
      CityCommand cancel = pending->command;
      cancel.parameter2 = CITY_DIALOG_CANCEL;
      cancelled->labels[cancelled->label_count++] = cancel;
    }
    if (!open)
      pending->window_id = 0;
    any_open |= open;
  }
  return any_open;
}

// Use the captured commands in the original stadium-then-label order after pause reconciliation.
void CancelClosedCityNameEditors(Game *game, const CityEditorCancellations *cancelled) {
  for (int index = 0; index < cancelled->stadium_count; ++index)
    RunCityCommand(game, CITY_COMMAND_REPLY_TO_QUESTION,
        INVALID_TILE, cancelled->stadiums[index].parameter1, CITY_DIALOG_CANCEL,
        cancelled->stadiums[index].parameter3, NULL, NULL);
  for (i32 index = 0; index < cancelled->label_count; ++index)
    ExecuteRecordedCityCommandWithPayload(game, cancelled->labels[index], NULL, 0, (const u8 *)"", 0);
}
