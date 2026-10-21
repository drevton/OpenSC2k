# Independent map view rotation

World tile indices, dimensions, directional metadata, coarse layers and event
positions retain their stored coordinate system. `City.geography.rotation` is
that stored basis, including stock file metadata; changing a camera never writes
it. Each native map camera stores a relative quarter turn (0–3).

## Implementation plan

1. Centralize forward/inverse coordinate transforms, camera dimensions, local
   neighbor offsets, slope rotation and directional artwork selection.
2. Traverse clip-derived diagonals in camera space and resolve each cell to its
   world TileIndex. Use the inverse transform for picking and world commands.
3. Select multi-tile anchors, ground unions, foundations, utilities and building
   artwork in camera space without changing stored footprint/orientation bits.
4. Project changed world tiles, previous/current effects and labels separately
   into each visible camera. Retain the existing screen damage and scroll-copy
   paths; rotating one camera discards only its cache.
5. Add independent viewport rotation controls and make center exchange operate
   through world coordinates. Keep stock save codecs in   the stored basis.
6. Admit SC2 comparisons only in the stored orientation. Physical rotation changes
   pathfinder traversal and RNG consumption, so rotated replay is unsupported.

## Pixel work requirement

For an equivalent scene, zoom and damage clip, preserve the existing sprite
composition and number of passes. Coordinate transforms must not widen clips,
add a second ground/object pass, repaint obscured pixels, scan the whole map, or
refresh other cameras. Reuse prepared directional sprites; never rotate a full
framebuffer or create a rotated map copy. Turning a camera needs one repaint of
its visible area, as before. Different angles naturally expose different faces.

The existing map cache pixel counters and indexed tile/paint counters are the
appropriate measurements when runtime verification is requested. A successful
build alone does not establish pixel-count equality.

## Implemented behavior

Native cameras have independent rotation and focus. F8 creates another viewport
(up to the existing eight slots); viewport buttons rotate that window. Main
rotation controls target the last map view clicked. Center exchange retains each
camera's direction. Projection, picking, sprite composition and damage use the
same camera transforms. Vehicle interpolation stays in world space.

The retired map permutation and event-rotation routines are removed. Water-source
sorting retains its existing limit in a dedicated workspace of at most 32 KiB,
instead of borrowing a map-sized rotation buffer. No new rendering pass or pixel
buffer is introduced. Camera rotation is presentation state for the current run;
stock files retain their stored map basis and world-space saved view position.

The native app builds. The changed journal code and metadata diagnostic compile.
The older native-map-command diagnostic still has unrelated compilation errors
(retired Drawing fields and outdated command arguments). Runtime visual checks,
gameplay tests and pixel-count comparisons have not been run.

## Rendering preparation

Viewport bounds reuse per-zoom maximum sprite extents in Drawing. All three zoom levels are
prepared together; asset revision, sprite catalog, environment and native-artwork changes invalidate
the limits. This adds 48 bytes of scalar cache state, independent of map size. Existing startup
preparation of scaled sprite pixels remains in place.

Bounds queries only calculate bounds. Event storage/scanning/sorting happens when an indexed paint
actually consumes the event list, using that view's rotation. Tile composition borrows the current
tile's decoded geometry through its drawing helpers; neighbouring tiles retain their own geometry.
Bridge sprite and palette selection share the resolved style, and axis/facing/lane rotation is
calculated directly for the four camera directions. These changes retain the existing drawing passes.

## Object depth drawing

The later scalar object-ordering change is independent of camera rotation. Native
map paints prepare clip-visible sprite commands, draw terrain/ordinary roads,
and blit whole buildings and vehicles in a shared depth order. Drawing retains
one fixed 224 KiB workspace shared by subdivided clips and all map windows.
Depth ties preserve command insertion order.
Existing rotation, damage projection and scroll-copy storage are unchanged.
See [event rendering](event-storage.md) for the scalar approximation and limits.
