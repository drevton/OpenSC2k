# Newspaper and notice illustrations

The thirteen original ImageGen paintings and their prompts are in `rendered-v1/`.
The newspaper and notice windows load `illustrations.dat`. The twelve newspaper pictures keep the
original 155 x 100 logical slots (411 is 154 x 100); the victory key is 106 x 53.
Notice pictures 403, 406 and 407 reuse the newspaper artwork. The intro also reuses
the full-resolution city-hall source (404), baked separately into `../intro/intro.dat`.

The offline baker requires Pillow. Rebuild the shipped pack and indexed PNG previews with:

```
python3 tools/bake_illustration_assets.py
```

The bake fits the source paintings to their display sizes, keeps 90% saturation
and 94% contrast, and applies a light 45-degree ink screen and subtle paper grain
**after** the resize. Newspaper issues before 1990 retain a grayscale palette for the
shared illustration sprite; **1990 is the first year with color**. The choice uses the issue's
year at capture and stays fixed while that issue is open. Notice images and the
shared baked source artwork retain their color.
This keeps a softer printed finish at game resolution. The result retains
256-color palettes. Windows use the ordinary sprite resampler when their display
size changes and reuse the prepared sprite for redraws. No files are read during
painting. `baked/manifest.json` records the dimensions and filter settings;
`baked/before-after.png` compares the resized and printed traffic illustration.
`--strength` adjusts the ink effect; the shipped setting is 0.14 (previously 0.24).
`--grain` independently adjusts paper grain; the shipped setting is 1.2 (previously 3.0).

The pack shares the `SC2KPIC1` format with advisor portraits: an eight-byte
signature and little-endian u32 record count, then thirteen ordered records
(128, followed by 400–411). Each stores ID, width, height and palette count as
little-endian u16, payload length as u32, 256 RGB palette entries, and native
RunSprite row offsets and opaque palette runs. The baker uses the same run encoder
as landscape sprites. Assets owns the native sprites; notice and newspaper windows
borrow them. Original comparison rendering retains its stock bitmap inputs.
