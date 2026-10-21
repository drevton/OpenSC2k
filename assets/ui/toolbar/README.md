# Native toolbar artwork

Hand-painted raster artwork supplies the primary and construction toolbar icons, including
zoom, disabled variants, zoning and building choices. Transparent source sheets and the full
ImageGen prompts live in `painted/`; `painted/manifest.json` maps cropped source subjects to
stable atlas IDs. Legacy category IDs alias the matching painted construction icon. Level terrain uses a large yellow equals
symbol above a flat earth slab, keeping the action readable at small toolbar sizes.
Stretch terrain uses a matching gold vertical double-headed arrow above a grassy earth slab from
`painted/stretch-terrain.png`, appended at atlas ID 105. Both the terrain toolbar and landscaping
chooser use the normal cached artwork renderer.
The start screen Website button uses the globe in `painted/website.png`, appended at atlas ID 106.
`painted/website-generation.json` records its built-in ImageGen prompt and source hash.
The start screen Music Jukebox button uses the red-and-gold painted jukebox in
`painted/jukebox.png`, appended at atlas ID 107. Its prompt and source hash are
recorded in `painted/jukebox-generation.json`. The standard icon cache prepares it
at the same size as the other startup button icons.
The main Landscaping button uses a large bright steel spade planted halfway into
a wide turf tile from `painted/landscaping.png`. A short shaft, broad blade and
large handle keep the tool recognizable within the square icon at small sizes.
The highway icon uses two asphalt carriageways with a plain concrete central divider
from `painted/highway.png`. The previous planted-median icon is preserved unchanged
in `painted/alternates/highway-avenue.png`; its original source also remains in
`painted/construction.png`. To restore it, point the highway's single-icon manifest
entry at `alternates/highway-avenue.png` and regenerate the atlas.

Power-plant and water-building chooser buttons use dedicated, tightly framed artwork in
`painted/power-plants.png` and `painted/water-buildings.png`. Large cooling towers with a gold
lightning bolt and a broad red-banded water tank with a blue droplet remain distinct at small sizes.
They omit ground diamonds and distant scenery. `painted/readable-utilities-prompts.json` records
the built-in ImageGen prompts.

Building design started with temporary game-sprite references for recognizable functions and colors.
Further ImageGen passes changed architecture, rooflines, footprints, proportions and facade details,
then moved from cartoon contours to realistic architectural painting on straight-edged isometric square
lots. The expensive endgame arcologies retain tall, distinct silhouettes. Final designs include a
staggered cyan police tower, a stepped yellow subway entrance, a red-banded water tower and a blue-rotor
windmill. Original sprite reference sheets and discarded intermediate artwork are not redistributed.
The toolbar uses the painted assets consistently. Building placement cursors use the loaded native
map artwork; other tool cursors use their matching painted toolbar icons.

Road and rail bridge tools use the original generated side-view suspension bridge
from `assets/ui/bridges/generated/toolbar-bridge.png`. This toolbar-specific original
uses dark brown steel, thick cables and closely spaced hangers, with a compact 2:1 aspect.
Both bridge buttons occupy two columns in the transport toolbar, including hit testing and wrapping.
Their atlas cells remain 128×128; packing compresses the horizontal dimension and the shared
sprite resampler restores their 2:1 aspect at the current interface scale. Source revision and license are
documented in [OpenGFX2 artwork](../opengfx2/README.md).

Full-resolution source sheets, stable icon IDs and prompts form the design reference for future map-sprite
work. They are not installed as in-game building graphics. That separate work must follow the game's
footprints, rotations, ground anchors, palette/animation needs and current sprite-format requirements.

`icons.dat` contains 32-, 64- and 128-pixel native RunSprite variants for 1x, 2x and 4x UI detail.
The pack uses the same sparse resolution-reference records as map sprites. UI scale selects detail
independently of map zoom; Drawing prepares the final button dimensions once and uses the normal sprite drawer.
Optional `variants/<icon>-1x.png`, `-2x.png` and `-4x.png` sources override individual resolutions.
Without these, the baker downsamples the painted source offline. Main-toolbar and extra-viewport zoom buttons share the painted plus/minus
medallions and their muted disabled variants. The old SVG sheet is retained as historical source;
the packer requires painted artwork for every toolbar slot, so vector fallbacks cannot reappear.
The generator preserves alpha and aspect ratio and fits subjects to the usable area with Lanczos
resampling. Packing is offline; there is no render-time image generation or file loading.

Regenerate from the repository root:

```
node tools/generate_map_icons.cjs
```

Pass `--toolbar-only` to regenerate just the toolbar pack and preview.

The Node script requires `sharp`. `icons.txt` records atlas order: `CityTopbarButton`,
then `CityCanvasTool`, then the disaster menu icon, three muted disabled variants and the
narrow-toolbar switch icon. The top bar uses OpenTTD-style icon buttons with fixed
widths and group separators. Narrow canvases switch button sets instead of wrapping or
stretching the buttons. One Zoning button opens a movable toolbar containing residential,
commercial, industrial, airport and seaport zones, de-zoning and demolition. One Transport button opens a movable
window containing road, highway, rail, subway, station, connection and demolition tools.
For a road or rail bridge, drag its line from a dry bank toward the water and release
to open the bridge chooser. Existing line ends can be extended the same way. Flat
canal crossings keep their approaches on dry banks and place bridge decks over every
water tile, including one-tile-wide canals.
The default-on advanced Infrastructure option "Bridge ramps on dry ground" keeps
new road and rail approaches above the bank's existing ground, without creating
an embankment. It affects newly built bridges; existing bridge terrain is preserved.
Utilities opens a toolbar combining power and water tools and choosers. Civic & Special
Buildings opens a toolbar containing Education, Health & Safety, Parks & Recreation, and
Rewards & Arcologies. The bulldozer is last in all three toolbars. Child building choosers
open beneath their parent toolbar and keep it on screen. Category toolbars remember their
last placement tool independently from building and zone-density choices.
There is no separate top-level demolition button. Atlas IDs remain stable independently
of which category buttons are visible.
Clockwise and counterclockwise rotation have adjacent direct-action buttons. Counterclockwise
borrows the mirrored clockwise artwork in both the main toolbar and extra viewports, preserving atlas IDs.
PageDown rotates clockwise;
PageUp rotates counterclockwise.
The native buttons map to the opposite grid transform because the isometric projection reverses
its visible direction. Their arrows, labels and shortcuts describe rotation of the displayed map;
the original grid commands retain their historical meaning for comparison and recorded actions.
Beside Layers, a direct Underworld / Overworld toggle uses its own painted earth
cutaway of bare soil and rock with buried copper and blue utility pipes,
appended at atlas ID 102.
It stays pressed in Underworld and reflects mode changes made by selecting
pipes and subways. Its tooltip names the mode
that clicking will reveal. Switching preserves the selected placement tool and
uses the existing surface/underground visibility banks.
`preview.png` is a contact sheet of all atlas icons, including disabled variants and the page switch.
Power plants, water buildings, and rewards/arcologies use a preview/list/details chooser: selecting a row
changes the preview; Build or Enter commits the placement tool. Lines and pipes remain
direct tools in their category pop-ups. Zone densities and airport/seaport zoning use small
checked text pop-ups; medium density is absent
because the simulation supports only light and dense zones. Recreation shows
names and prices, while transport, landscaping, education, services
and emergency dispatch retain compact tool buttons with explanatory tooltips.

The toolbar retains the OpenTTD layout and beveled controls. Text pop-ups are used for
choices such as zone density and utility tools, rather than replacing the icon toolbar.
Build shortcuts follow SimCity 3000 where supported; menus and Help > Keyboard Shortcuts
show the current assignments. F10 opens menus; arrows and Enter navigate them.
Dropdown text starts at the row's top: the interface font's capitals begin on glyph row zero,
so shifting text up clips their top strokes. Ten-pixel rows retain complete capitals and descenders.

Construction toolbars and their building choosers use the OpenTTD green palette, including
the title bar and both raised and pressed bevels. The main toolbar and status bar remain gray.
Status fields use raised gray bevels and centered text with black shadows; paused text is yellow.
The status bar is 12 UI pixels high; the independent RCI panel rises 4 pixels above it.
The status bar defaults to 693 UI pixels, using
[OpenTTD's full-toolbar formula](https://github.com/OpenTTD/OpenTTD/blob/15.3/src/toolbar_gui.cpp):
30 buttons plus six quarter-button spacers, at our 22-pixel button size. Consolidating categories
does not shrink the status bar. The top toolbar fits its fixed-size buttons and group separators,
without empty padding at either end. Both bars are centered independently. Narrow canvases clamp
the status bar to the available width and page toolbar buttons when needed.
Map pixels remain visible beside each bar when the canvas is wider than that bar.
Zoning density pop-outs fit their labels and shortcuts. Opening one immediately activates its
last-used density (light on first use), without highlighting a row until the pointer enters it.
The pop-out closes after the pointer leaves an 8 UI pixel margin around the toolbar/menu,
or when clicking outside, so building can begin with the next map click.
De-zone is a distinct eraser tool with a visible selected state. Repeated clicks select the
eraser again, never turn it off. Demolish uses a shaded steel bomb with a lit fuse.
The game-speed dropdown uses the same green beveled pop-out style as zoning and fits its labels.
