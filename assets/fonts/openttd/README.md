OpenTTD Sans is the official OpenTTD UI typeface from OpenTTD/OpenTTD-TTF.

Source: https://github.com/OpenTTD/OpenTTD-TTF
Revision: e96a69b1acd7c1b672a39f2c1539fa92d0910af7
File: ttf/OpenTTD-Sans.ttf
License: GPL version 2; the upstream license is included as LICENSE.

The canvas uses a monochrome rasterization at its intended 10-pixel height.
Run `python3 tools/generate_canvas_font.py` to regenerate the embedded C glyphs.
Pillow is only needed for regeneration; the runtime renderer is C.

The embedded jukebox table, generated with `python3 tools/generate_music_font.py`,
remains a diagnostic fallback. Native jukebox text uses the interface font asset
and scales glyph coordinates directly into the framebuffer.

Native canvas windows now use `../interface-font.dat`, generated with
`tools/build_interface_font.py` at 1x, 2x and 4x, following OpenGFX2.
The embedded normal table remains a bootstrap/diagnostic fallback and shares
the same advances. Higher-resolution glyphs are rasterized independently,
not obtained by enlarging the 10-pixel image. See `../interface-font.md`.
