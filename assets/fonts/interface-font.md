# Native interface font

`interface-font.dat` contains normal UI glyph pixels at 1x, 2x and 4x,
using OpenGFX2's normal-font rasterization and the bundled OpenTTD Sans.

- Font: OpenTTD/OpenTTD-TTF revision e96a69b1acd7c1b672a39f2c1539fa92d0910af7.
- Rasterization: https://github.com/OpenTTD/OpenGFX2/blob/7a17fb370440aed7f2586715d606e9fe190b9dd3/graphics/fonts/charactergrab.py
- License: GPL-2.0; see openttd/LICENSE.

Regenerate with `python3 tools/build_interface_font.py` (Pillow is only needed
for offline regeneration). Match the upstream 10-pixel normal font, baseline
stabilization, monochrome ink, embedded shadow, sprite rectangle and advances.
The C renderer selects the glyph detail from the final canvas scale; it never
loads a font rasterizer or the original executable during gameplay.

Format SC2KUIF3: eight-byte magic, then 1x/2x/4x variants, each with 256
Windows-1252 glyphs and the OpenTTD private-use close icon (U+E2AD).
A glyph stores three bytes (width, height, 1x advance),
then height pairs of little-endian 64-bit ink and shadow masks. The loader
checks dimensions, masks, truncation and trailing bytes. Assets owns the
fixed storage; rendering performs no allocation or file I/O.

`interface-coverage.dat` supplies the alternate font mode using bundled
Noto Sans Regular (SIL Open Font License 1.1; see noto/LICENSE). Run `python3 tools/build_coverage_font.py` to regenerate it.
It retains 8-bit coverage at every integer pixel height from 5 to 55 (including
the smaller jukebox text and 5x captions). Rendering selects the final physical
size and composites glyphs directly into the shared framebuffer. Anti-aliasing
off thresholds coverage; traditional sprite mode continues to use SC2KUIF3 and
disables the anti-alias control, following OpenTTD's settings window behavior.
Logical advances remain identical in both modes so controls retain their layout.
The alternate atlas uses the sprite baseline to keep text aligned in controls.

`overlay-font.dat` contains 12-, 14- and 16-pixel map-label glyphs generated from
the same bundled OpenTTD Sans. Regenerate with `python3 tools/build_overlay_font.py`.
The native pack contains no Wine/GDI font capture or host-font dependency. The
historical capture is retained only under `src/city/comparison/assets/fonts/`
for diagnostic rendering; it is never included by the native asset catalog.

Format SC2KAAF1: eight-byte magic, little-endian u32 decoded/encoded byte counts,
then size-major 256-glyph width/height byte pairs, followed by `(count, alpha)`
byte runs. The loader validates dimensions, total size, runs and EOF. Assets owns
one decoded atlas allocation, loaded at startup and released during shutdown.
This is a font cache, not a window bitmap. Noto Sans copyright, upstream revision
and license are recorded in noto/README.md and noto/LICENSE.
