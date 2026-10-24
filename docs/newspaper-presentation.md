# Newspaper presentation

The native newspaper opens centered at 530×425 logical pixels, keeping its earlier
window proportions. Smaller canvases constrain it to the available area.
It draws a masthead, banner headline, seven story panels, justified excerpts and
a grayscale photograph. Assets owns baked Noto Serif, Anton and Chomsky coverage
glyphs at 22 sizes, selected from final framebuffer scale. Ordinary page painting
uses no font engine, file I/O, whole-page bitmap or glyph allocation.

Opening the paper runs a 0.75-second software zoom/spin with a small settling
overshoot. UI tracks 45 presentation frames using the host clock; City and its RNG
are untouched. Drawing captures one temporary image at the final window resolution
and inverse-maps its pixels with fixed-point scanline increments. The compositor
repaints old/new transformed bounds, including underlying map windows, and respects
windows above the paper. The image is released on completion, dismissal or shutdown;
normal text rendering resumes at full sharpness. Clicking the paper, Escape, Enter
or Space skips the entrance. If image allocation fails, the ordinary page appears
on the next frame.

The original paint routine still runs and retains its City/RNG effects and
comparison capture. Afterwards, full headlines and bodies are composed using a
scalar City preview with the same per-story seeds and its own shared token choices.
The UI window owns these texts; reading and scrolling do not change
City, regenerate articles or spend simulation random numbers.

Click an excerpt, headline or lead photo to open the article in the same canvas
window. The wheel, arrows, Page Up/Down and Home/End scroll the text. The scrollbar
can be clicked. Back, Escape or Backspace returns to the front page. Opening the
reader cancels the native automatic-close deadline. Closing the newspaper releases
its text/picture storage and the existing temporary dialog pause.

Native capture geometry uses a project-defined grid, with independent font sizes
and alignment. The historical page rectangles, typography tables and token map
live only in `src/city/comparison/resource_storage.c`; comparison initialization
installs them explicitly. Stock template installation also restores its token map.

## Code ownership

`city/events/news.c` owns news records, ranking, article selection and template expansion. Its existing
250-byte shared token-choice table is transient City state; headline and body composition reuse it.
The composition API takes City and immutable newspaper resources, without reaching into Drawing.
After any opening notice is dismissed, `FinishCityNewspaperInitialization` fills the weather/editorial
records and consumes the existing event-RNG draw for the music request. It runs once before the first paint.

`ui/news/newspaper_capture.c` owns capture layout, retained first-paint effects, text/filler capture and
full article preview composition. Its declarations live in `ui/news/newspaper_capture.h`. Capture writes
the supplied City's calendar, CRT RNG and article caches; a manual preview supplies a scalar City copy.
The result owns copied text and geometry plus a retained picture reference. Recapture replaces that
reference; release it before discarding the capture storage. Drawing owns only prepared capture
layout/font/alignment tables.

`ui/news/newspaper_drawing.c` owns software page painting, publication layout, the opening animation
and window input. It consumes captured text and pictures; painting does not compose articles again.
`ui/news/newspaper_window.c` owns canvas window creation, page replacement, deadlines and callback handoff.
Its completion is attached before pause reconciliation can dispatch nested UI work.

`ui/news/newspaper_presentation.c` implements initialization/destruction and the text, rectangle and
measurement presentation hooks. Comparison can observe those hooks without retaining a native capture.
Window/dialog lifecycle declarations stay in `ui/news/newspaper.h`; capture internals do not leak through it.

The shared window-stack, focus, pixel-coordinate and visibility interface is `ui/windows/windows.h`.
Window pointers are borrowed until the stack changes; retain window IDs across callbacks and open/close/focus operations.
Fixed-font decoding and measurement live in `drawing/fonts/fonts.c`, declared in `drawing/fonts/fonts.h`.
Newspaper presentation uses those interfaces without importing Drawing's private implementation header.

Build the font asset with `python3 tools/build_newspaper_font.py`; source fonts,
versions and licenses are in assets/fonts/newspaper. SC2KNPF1 stores a 24-byte
little-endian header (magic, face count, size count, raw length, compressed length),
the byte-sized font sizes, then a zlib stream. Its payload is face/size/CP1252-major
12-byte glyph records (u32 coverage offset, u8 width/height, i16 bearings, u16 26.6
advance), followed by coverage pixels. The 32-pixel master provides scale-independent
advances. Malformed font assets fail startup instead of falling back silently.

Validation is limited to Release and comparison builds; no interactive run or
simulation replay was requested for this presentation change.
