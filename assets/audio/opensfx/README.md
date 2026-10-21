# OpenSFX source recordings

`selection.wav` is the unmodified OpenSFX "Beep" recording by Janis Lukss,
licensed under Creative Commons Attribution 3.0 Unported (see `LICENSE`).

Source: [OpenTTD/OpenSFX, src/wav/osfx_21.wav](https://github.com/OpenTTD/OpenSFX/blob/d0bf8d4deda22d5a7f8d01953433cb2644663e9f/src/wav/osfx_21.wav).
Attribution: [src/opensfx.psfo](https://github.com/OpenTTD/OpenSFX/blob/d0bf8d4deda22d5a7f8d01953433cb2644663e9f/src/opensfx.psfo).

OpenTTD maps `SND_15_BEEP` to sample 21 with base volume 80. We play the
original 44,100 Hz, mono, 16-bit PCM (3,841 frames) once per selection,
centered in stereo with OpenTTD's maximum-volume channel gain of 7,183 / 65,536.
The effects and master volume controls apply to this independent UI voice.
The recording is loaded once when opening audio and released on shutdown.

The curated city effects in `effects/` are also unmodified imports from the same
pinned revision. `credits.json` gives the upstream URL, hash and full individual
attribution for every imported sample. `LICENSE` is CC BY 3.0;
`LICENSE-CC-BY-SA-3.0.txt` covers the helicopter; `LICENSE-CC0-1.0.txt` covers
public-domain samples. See each entry for the applicable license.

These WAVs are source assets. Native playback uses the project's own
[`sound-effects.dat` pack](../README.md), with loose WAV overrides during development.
The app bundle includes the pack and attribution, not these separate WAV files.
