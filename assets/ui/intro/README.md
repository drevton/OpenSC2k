# Intro artwork

The starting menu and loading screen share one baked background containing the full-color city
illustration, website emblem, large OpenSC2k title and caption "OPEN SOURCE CITY SIMULATOR".
The main menu offers New City, Load City, Settings, Music Jukebox, OpenSC2k Website and Exit.
Settings and Music Jukebox open the existing canvas windows above the menu; closing them
returns to the menu without choosing a city action. The website button opens
`https://www.opensc2k.com` in the default browser while leaving the menu open. Terrain editing
remains available from the New City window.
The logo is `assets/app/sources/city-mark.png`; font sources and their SIL Open Font License
are in the same directory. `city-background.png` is the original panorama; its prompt, generation
method and source hash are recorded in [prompts.md](prompts.md). The baker composites the existing
emblem and large, heavier typography into `background.png`. A near-opaque dark green heading
field fades into the illustration below the logo, keeping both the white title and larger cream
caption legible against the detailed city. The image fills
the screen from its top edge; the menu and loading panel sit beneath the title.
Artwork, labels and progress digits use smooth sampling,
independent of game pixel scale. Normal startup opens the menu without loading a city save or
advancing simulation. An explicit city-file argument still opens that city directly.

Regenerate with `python3 tools/bake_intro_assets.py`, then build normally. `intro.dat` is the runtime
asset pack. `brand.png`, `background.png` and `preview.png` are design references and are not
copied into the app bundle.

The pack shares `SC2KPIC1` with advisors, illustrations, neighbors and music controls: a little-endian
image count, then records with u16 ID, width, height and palette count, and u32 run-payload length.
Intro images have no palette; payloads contain ordinary RunSprite row offsets and straight-alpha
RGB/RGBA runs. Images 0–3 are loading-stage labels, images 4–14 are digits 0–9 and percent,
and image 15 is the city illustration with its baked logo, title and caption.
The sprite payloads occupy approximately 2.04 MiB. Drawing streams the shared resampler's rows
through the normal sprite drawer into the existing framebuffer. Temporary row scratch avoids a
full-screen scaled copy; redraws perform no file I/O.
