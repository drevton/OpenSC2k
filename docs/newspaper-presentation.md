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
scalar City preview with the same per-story seeds. Drawing's shared-choice scratch
is restored. The UI window owns these texts; reading and scrolling do not change
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

Build the font asset with `python3 tools/build_newspaper_font.py`; source fonts,
versions and licenses are in assets/fonts/newspaper. SC2KNPF1 stores a 24-byte
little-endian header (magic, face count, size count, raw length, compressed length),
the byte-sized font sizes, then a zlib stream. Its payload is face/size/CP1252-major
12-byte glyph records (u32 coverage offset, u8 width/height, i16 bearings, u16 26.6
advance), followed by coverage pixels. The 32-pixel master provides scale-independent
advances. Malformed font assets fail startup instead of falling back silently.

Validation is limited to Release and comparison builds; no interactive run or
simulation replay was requested for this presentation change.
