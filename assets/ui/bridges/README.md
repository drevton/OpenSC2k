# Original bridge interface artwork

The twelve PNGs in `generated/` were made with the built-in image_gen tool for
this project. Only original generated shape studies were supplied as editing inputs; no
external artwork was used. `generation.json` records the shape and finishing
prompts for each source image. These replace every bridge chooser icon
and both wide road/rail bridge toolbar buttons; no OpenGFX2 bridge icon is used.
`toolbar-bridge.png` is a dedicated dark brown, compact suspension bridge with
thick cables and eight closely spaced hangers. `toolbar-generation.json` records
its generation and refinement prompts.

The prompt set requests original horizontal side elevations, oversized columns,
cables, braces and supports, smooth high-resolution shading and lighting,
transparent backgrounds and legibility at 46×24 logical pixels. The subjects are wood, concrete, steel arch, suspension,
cantilever, girder, tubular, raised power lines, drawbridge, highway and
reinforced highway. Causeway shares the concrete design; the legacy rail symbol
shares the girder design. Road and rail chooser lists share the thirteen styles.

`tools/generate_map_icons.cjs` trims the transparent margins, applies baked
structure color variants, normalizes chooser silhouettes to the row frame and
writes the native RunSprite `icons.dat` atlas. The source PNGs retain their original
alpha. The generated palette variants need no map-art or OpenTTD palette inputs.
Window sprite caches use the shared resampler and sprite drawer for display scaling.

Repack with Node.js and sharp from the repository root:

    node tools/generate_map_icons.cjs

The landscape bridge sprites remain separate assets with their existing attribution.
