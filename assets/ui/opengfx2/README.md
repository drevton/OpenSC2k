Music transport icons from OpenGFX2, by Richard Wheeler (Zephyris) and contributors.

`viewport-zoom-2x.png` retains the former zoom artwork as a historical upstream reference.
Its 40×40 sprites came from the same revision's 2× icon sheet, row 1, columns 3–4,
with the white sheet borders excluded. Main-toolbar and extra-viewport zoom controls now
use the original painted medallions in `assets/ui/toolbar/painted/zones-rewards-zoom.png`;
no OpenGFX2 zoom pixel table is compiled into the game.

Source: https://github.com/OpenTTD/OpenGFX2
Revision: 7a17fb370440aed7f2586715d606e9fe190b9dd3
Sheet: graphics/icons/1/icons_20px_32bpp.png
Sprites 709–712: row 3, columns 0–3; 20×20 cells with a one-pixel border.
Blue is transparent. Extracted without resizing. GPL version 2; see LICENSE.
The player uses the 2× variant from graphics/icons/2/icons_20px_32bpp.png,
40×40 sprites extracted without resizing. `tools/generate_music_sprites.py` bakes
`music/transport.dat` as SC2KPIC1 native RGB/RGBA RunSprites. The ordinary sprite
resampler and drawer fit them to 20×20 logical controls at the final UI scale.

Tool cursors use `graphics/cursors/2/classic_yellow_icon.png` for the yellow
badge, `graphics/cursors/2/windows_white_cursor.png` for the white arrow,
and the corresponding 40×40 tool icons from the same 2× icon sheet.
The arrow is layered over the badge to match the supplied reference. The tip
at (0,0) is the hotspot. Cursor sprites have a compact 24×24 logical extent and follow
interface scale independently of map zoom and Retina backing resolution.
Facility badges use the project's already-loaded native building artwork, including
Small Park, hydro, marina and wind previews with their rotors. Other tool badges use
the current painted toolbar atlas, including zones, landscaping and both bridge tools.
Bridge silhouettes retain their wide aspect inside the badge. Native building cursors
use a fixed front view; original/custom building overrides retain their loaded artwork.
The badge and pointer are alpha-composited with the artwork before SDL caches the cursor. No
original game sprites or installation files are read to construct cursors.
`tools/generate_cursor_sprites.py` regenerates `cursors/canvas-cursors.dat`
from the bundled upstream PNGs in `cursors/`. Upstream tool crops remain startup
fallbacks until native graphics are ready. Source revision and GPL-2.0
license are the same as the music icons above.

The default cursor uses `graphics/cursors/2/classic_yellow_cursor.png` from
the same revision, unchanged (SHA-256
`a2acf50a0febff686eafd17c81b35b3570db61160e37d756ba774777d19c76ef`).
The plain gold arrow appears when no tool is selected and over canvas controls.
SDL installs custom native color cursors for both the gold arrow and tool badges.
The platform tracks their position independently of the frame loop, so cursor
motion never waits for game rendering or an extra VSync presentation. It does
not draw game windows, damage the framebuffer or upload game pixels. Cursors
are cached per tool and rebuilt when their displayed size changes. Map and toolbar
artwork and the final cursor size use the shared sprite resampler. Only the final
straight-alpha pixel export remains specific to SDL's color-cursor API. On Retina
displays, backing-pixel dimensions are converted to window points before
creating the native cursor. SDL owns cursor presentation on every platform.

The cursor data file stores the exact ARGB pixels with a small header
(described in `src/assets/cursors.h`). The native app loads it from its
graphics root; Mac bundles include it in `Contents/Resources/graphics/`.
Missing or invalid graphics retain a procedural gold pointer rather than a
platform cursor. Pixel artwork remains covered by the same GPL-2.0 license.

Bridge chooser and toolbar buttons use original generated artwork in
`assets/ui/bridges/generated/`; they no longer use this upstream icon sheet.
