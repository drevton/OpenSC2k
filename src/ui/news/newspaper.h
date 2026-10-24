#ifndef SC2K_UI_NEWSPAPER_H
#define SC2K_UI_NEWSPAPER_H

#include "city/presentation.h"
#include "city/types.h"

typedef struct City City;
typedef struct Game Game;
typedef struct CityInterfaceState CityInterfaceState;
typedef struct CityNewspaperDialog CityNewspaperDialog;

// UI owns the dialog lifetime; callers retain the opening notice until the City initialization tail can run once.
CityNewspaperInitialization BeginCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
void InitializeCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
void DestroyCityNewspaperWindow(City *city, CityNewspaperDialog *dialog);

i32 RunPausedCityNewspaperDialog(City *city, CityNewspaperDialog *dialog);
bool ChooseCityNewspaperModal(City *city, CityNewspaperDialog *dialog);
// Retained disaster presentation advances the pending effects, then waits for its newspaper.
// CITY_OPERATION_PENDING keeps the same effect work; it must be continued rather than restarted.
i64 ProcessPendingCityDisaster(City *city, bool terrain_water_allowed);
// Copies the requested edition and arms delivery after the first rendered map frame.
void ScheduleCityStartupNewspaper(City *city);
// Limit a nonnegative host wait to an armed deadline. Reads the clock only while armed; does not open or mutate UI.
int LimitCityStartupNewspaperWait(const CityInterfaceState *ui, int wait_ms);
// After successful map presentation, start a queued edition's half-second delay once. No clock read when not queued.
void ArmCityStartupNewspaper(CityInterfaceState *ui);
// Attempt due delivery only when host/dialog gates allow it. Clear the deadline before opening so nested work may
// schedule another edition. True requests repaint even if opening fails; blocked work keeps its deadline.
bool PollCityStartupNewspaper(City *city, double now);
// True transfers callback delivery through opening/window completion, which may occur before return.
// False retains no callback/context and leaves cleanup to the caller.
bool OpenCityNewspaperWithCallback(City *city, int edition, bool automatic,
    void (*callback)(City *city, void *context), void *context);
// Resume/cancel one newspaper-initialization token. City finishes its tail before UI opens the page.
// True transfers callback delivery, possibly before return; false retains no callback or context.
// Page-creation failure consumes the City token; it does not restart or roll back initialization.
bool ResumeCityNewspaperOpening(City *city, int token, bool resume,
    void (*callback)(City *city, void *context), void *context);
bool InitializeCityNewspaperWindow(City *city, CityNewspaperDialog *paint);
// Returns the captured window ID or zero; the window owns its page, and paint is borrowed only for this call.
// Pause reconciliation may close or reorder windows before return; reacquire the ID before accessing the window.
u32 OpenCityNewspaperPage(City *city, CityNewspaperDialog *paint, int edition, double close_at);
void OpenCityManualNewspaper(City *city, int edition);

void PresentCityNewspaperEdition(City *city, i32 edition_index);
typedef struct Window Window;
bool CityNewspaperOpening(const Window *window);
CityRect CityNewspaperPaintBounds(const Window *window, CityRect bounds);
bool UpdateCityNewspaperOpening(Game *game, double now);

#endif
