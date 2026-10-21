# Advisor portraits

Eight original gouache and ink illustrations replace advisor bitmap IDs 197–204:
property tax, ordinances, bonds, police, fire, health, education and transit.
Faces are neutral; occupational hats, props and proportions supply the humor.

`sources/` contains the high-resolution paintings. `manifest.json` records the
built-in ImageGen prompts and source hashes, including the original generation
and subsequent expression edit. Keep hashes current when editing art.

Run `python3 tools/bake_advisor_portraits.py` to refresh `baked/`, `preview.png`
and `advisors.dat`. The preview shows the actual baked images at native size.
Normal builds incrementally bake a separate pack in the build directory when
the baker, manifest or source paintings change. The app ships only that pack.

Portraits are **256×328 pixels**, four times the original 64×82 width and height.
The logical UI area remains 64×82, with the source sampled directly at the final
canvas scale. Smaller scales average pixels; 400% UI displays the full raster.
The normal sprite resampler prepares the final pixel size once per window and
reuses it for redraws; changing the UI scale replaces that prepared sprite.

Each portrait is quantized independently to its own 256-color palette. Both advisor
and illustration packs use `SC2KPIC1` (8 bytes), then a little-endian record count
(`u32`). Each record stores ID, width, height and palette count (`u16` each), run
payload length (`u32`), 256 RGB palette entries and the ordinary RunSprite row
offsets/pixel runs. Opaque palette index zero remains an ordinary color.

Assets retains eight normal sprites (IDs 197–204), with no expanded RGBA bitmap
cache. Windows borrow these images and paint through DrawRunSprite. Dimensions,
palette size, sprite streams and EOF are validated; invalid artwork fails loading.
Stock budget-helper images 189–196 are not included.
