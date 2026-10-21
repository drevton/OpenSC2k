# Marina (248)

Four original Imagegen paintings from fixed quarter-turn layout guides. Master tile width is 128 pixels.
The baked 3×3 foundation uses the exact native terrain mask, with the ground omitted from the final sprite so native animated water remains visible. The original painting only calibrates the foreground extraction.
Bake with `python3 tools/bake_civic_sprites.py assets/landscape/civic/248-marina`.
See `bake-result.json` for edge checks and `final-rotations-preview.png` for the baked views.
Fine painted details can vary between angles.

Full square water painting is calibration reference only; extract docks/boats/office separately for native animated water. Bows v-max visible0/3, sterns1/2. Two tall masts, oneoffice rearleft, fixed pier plan.
