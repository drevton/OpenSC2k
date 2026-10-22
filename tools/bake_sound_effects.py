#!/usr/bin/env python3
"""Bake original synthesized WAVs and curated third-party WAVs into the native sound pack.

Run import_opensfx.py first when updating upstream sources. Neither script runs in
the game or downloads anything during a normal build. Python standard library only.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import sys
import wave


ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "assets/audio"
RATE = 44100
TAU = 2 * math.pi

# Authoritative mapping: (City ID, role, source WAV, match, explanation).
# APPROXIMATION entries deliberately have no dedicated recording for that role.
EFFECTS = (
    (500, "construction", "opensfx/effects/osfx_31.wav",
        "match", "OpenTTD non-rail construction placement; short road/building confirmation"),
    (501, "placement rejected", "opensfx/effects/osfx_01.wav",
        "approximation", "Negative year cue; no rejection recording"),
    (502, "earthquake", "generated/earthquake.wav",
        "generated", "Original low rumble and debris impacts"),
    (503, "tree or zone placement", "opensfx/effects/osfx_31.wav",
        "approximation", "Generic construction placement"),
    (504, "explosion", "edited/explosion.wav",
        "match", "OpenSFX explosion shortened to 0.4 seconds at 25% amplitude with softened edges"),
    (505, "query", "opensfx/selection.wav",
        "approximation", "Generic interface beep"),
    (506, "police or hospital", "generated/emergency-siren.wav",
        "generated", "Original emergency vehicle wail"),
    (507, "fire", "generated/fire.wav",
        "generated", "Original filtered noise and crackles"),
    (508, "bulldozer work", "freesound/bulldozer.wav",
        "match", "Bulldozer engine recorded by mr101986"),
    (509, "fire dispatch", "generated/fire-engine.wav",
        "generated", "Original alternating fire-engine tones"),
    (510, "helicopter flight", "opensfx/effects/osfx_24.wav",
        "match", "Helicopter"),
    (511, "water", "opensfx/effects/osfx_02.wav",
        "match", "Water placement splat"),
    (512, "demolition", "freesound/demolition.wav",
        "match", "Wooden house demolition recorded by relwin"),
    (513, "civic placement", "generated/civic-chime.wav",
        "generated", "Original three-note completion chime"),
    (514, "power lines", "opensfx/effects/osfx_12.wav",
        "match", "Electric sparking"),
    (515, "aircraft crash", "edited/aircraft-crash.wav",
        "approximation", "OpenSFX big crash shortened to 0.5 seconds at 20% amplitude; not aircraft-specific"),
    (516, "helicopter stage", "freesound/helicopter-start.wav",
        "approximation", "Helicopter ignition recorded by Paul368; exact stage role unproved"),
    (517, "ship", "edited/ship-horn.wav",
        "match", "OpenSFX ship horn shortened to 0.65 seconds at 30% amplitude"),
    (518, "airplane takeoff", "edited/airplane-takeoff.wav",
        "match", "OpenSFX jet takeoff shortened to one second at 30% amplitude"),
    (519, "airplane landing", "edited/airplane-landing.wav",
        "match", "OpenSFX plane wheels touching ground at 25% amplitude with softened edges"),
    (520, "disaster warning", "generated/disaster-warning.wav",
        "generated", "Original slow civil-warning siren"),
    (521, "bus depot", "opensfx/effects/osfx_27.wav",
        "match", "Bus start"),
    (522, "prison", "freesound/prison-door.wav",
        "match", "Prison cell door recorded by kd_jack"),
    (523, "education", "freesound/school-bell.wav",
        "match", "Electric school bell recorded in a school hall by sbyandiji"),
    (524, "train", "opensfx/effects/osfx_10.wav",
        "match", "Train departure horn"),
    (525, "military dispatch", "freesound/dispatch-radio.wav",
        "match", "All units proceed to your post-assignments, recorded by klankbeeld"),
    (526, "unknown 526", "opensfx/selection.wav",
        "approximation", "Unidentified original role; neutral beep placeholder"),
    (527, "sailboat or zoo", "opensfx/effects/osfx_66.wav",
        "approximation", "Outdoor bird cue shared by both roles"),
    (528, "unknown 528", "opensfx/selection.wav",
        "approximation", "Unidentified original role; neutral beep placeholder"),
    (529, "terrain generation", "opensfx/effects/osfx_31.wav",
        "approximation", "Terrain placement; replaces the spoken announcement"),
    (531, "bridge construction", "opensfx/effects/osfx_39.wav",
        "match", "OpenTTD bridge construction; smithy/anvil recording"),
)

# Explicit offline edits of the retained OpenSFX originals. Normal builds only read the resulting WAVs.
# Source, destination, maximum duration in ms, amplitude percent, fade-in ms, fade-out ms.
RECORDING_EDITS = (
    ("osfx_18.wav", "explosion.wav", 400, 25, 10, 80),
    ("osfx_19.wav", "aircraft-crash.wav", 500, 20, 10, 100),
    ("osfx_06.wav", "ship-horn.wav", 650, 30, 40, 150),
    ("osfx_09.wav", "airplane-takeoff.wav", 1000, 30, 40, 200),
    ("osfx_23.wav", "airplane-landing.wav", 200, 25, 10, 40),
)


def EditRecordings():
  destination = AUDIO / "edited"
  destination.mkdir(exist_ok=True)
  for source, filename, duration, gain, fade_in, fade_out in RECORDING_EDITS:
    samples = array.array("h", ReadPCM(AUDIO / "opensfx/effects" / source)[:duration * RATE // 1000 * 2])
    if sys.byteorder != "little": samples.byteswap()
    attack, release = fade_in * RATE // 1000, fade_out * RATE // 1000
    for index, sample in enumerate(samples):
      envelope = min(1, index / attack, (len(samples) - index - 1) / release)
      samples[index] = round(sample * gain / 100 * envelope)
    if sys.byteorder != "little": samples.byteswap()
    with wave.open(str(destination / filename), "wb") as output:
      output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
      output.writeframes(samples.tobytes())


def Synthesise(name, duration):
  """Original sound design from oscillators/noise; no sampled legacy recordings."""
  rng = random.Random(name)
  low = rumble = phase = crackle = 0.0
  samples = []
  for index in range(round(duration * RATE)):
    t = index / RATE
    noise = rng.uniform(-1, 1)
    low += .025 * (noise - low)
    rumble += .006 * (noise - rumble)
    if name == "earthquake":
      if rng.random() < 8 / RATE:
        crackle = rng.uniform(.2, .8)
      crackle *= .998
      value = 5 * rumble + .13 * math.sin(TAU * 39 * t) * (1 + math.sin(TAU * 2 * t))
      value += crackle * (noise + 2 * low)
    elif name == "fire":
      if rng.random() < 24 / RATE:
        crackle = rng.uniform(.2, .8)
      crackle *= .985
      value = 2 * low * (.7 + .3 * math.sin(TAU * 1.25 * t)) + crackle * noise
    elif name in ("emergency-siren", "fire-engine", "disaster-warning"):
      if name == "emergency-siren":
        frequency = 880 + 320 * math.sin(TAU * t / 1.2 - math.pi / 2)
      elif name == "fire-engine":
        frequency = 630 if t % .6 < .3 else 840
      else:
        frequency = 440 + 180 * math.sin(TAU * t / 4 - math.pi / 2)
      phase += TAU * frequency / RATE
      value = math.sin(phase) + .23 * math.sin(3 * phase) + .06 * math.sin(5 * phase)
    else:  # Civic completion: soft major triad with staggered attacks.
      value = 0
      for onset, frequency in ((0, 523.25), (.12, 659.25), (.24, 783.99)):
        age = t - onset
        if age >= 0:
          value += min(1, age / .006) * math.exp(-5 * age) * (
              math.sin(TAU * frequency * age) + .15 * math.sin(TAU * 2 * frequency * age))
    # Remove endpoint clicks, including when the existing scheduler loops a cue.
    fade = min(1, t / .015, (duration - t - 1 / RATE) / .025)
    samples.append(value * max(0, fade))
  mean = sum(samples) / len(samples)
  samples = [value - mean for value in samples]
  peak = max(abs(value) for value in samples)
  gain = .55 if "siren" in name or name in ("fire-engine", "disaster-warning") else .65
  pcm = array.array("h", (round(value / peak * gain * 32767) for value in samples))
  if sys.byteorder != "little":
    pcm.byteswap()
  return pcm.tobytes()


def BakeGenerated():
  directory = AUDIO / "generated"
  directory.mkdir(parents=True, exist_ok=True)
  for name, duration in (("earthquake", 4), ("fire", 4), ("emergency-siren", 2.4),
      ("fire-engine", 2.4), ("disaster-warning", 4), ("civic-chime", 1.2)):
    path = directory / f"{name}.wav"
    with wave.open(str(path), "wb") as output:
      output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
      output.writeframes(Synthesise(name, duration))


def ReadPCM(path):
  with wave.open(str(path), "rb") as source:
    if source.getnchannels() != 1 or source.getsampwidth() != 2 or source.getcomptype() != "NONE":
      raise ValueError(f"Expected mono 16-bit PCM: {path}")
    frames = source.getnframes()
    if not 0 < frames <= 8 * 1024 * 1024:
      raise ValueError(f"Empty/oversize source: {path}")
    pcm = source.readframes(frames)
    if len(pcm) != frames * 2:
      raise ValueError(f"Truncated source: {path}")
    source_rate = source.getframerate()
  # Current curated OpenSFX WAVs are already 44.1 kHz. Fail rather than silently
  # degrading a new source; resample explicitly offline if a future import differs.
  if source_rate != RATE:
    raise ValueError(f"Expected {RATE} Hz: {path} has {source_rate}")
  return pcm


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--regenerate", action="store_true", help="replace the six synthesized source WAVs")
  parser.add_argument("--edit-recordings", action="store_true", help="recreate the quieter edited OpenSFX WAVs")
  parser.add_argument("--ids", nargs="+", type=int, help="bake only these IDs into an incremental override pack")
  parser.add_argument("--output", type=Path, help="destination pack (default: base or override in assets/audio)")
  args = parser.parse_args()
  chosen = set(args.ids) if args.ids else {item[0] for item in EFFECTS}
  if not chosen or chosen - {item[0] for item in EFFECTS}:
    parser.error("sound IDs must be in 500..529 or 531; 530 is an activation request, not a sound")
  effects = [item for item in EFFECTS if item[0] in chosen]
  output = args.output or AUDIO / ("sound-effects.override.dat" if args.ids else "sound-effects.dat")
  if args.regenerate:
    BakeGenerated()
  if args.edit_recordings:
    EditRecordings()
  header_size = 24
  record_size = 80
  offset = header_size + record_size * len(effects)
  records = bytearray()
  payload = bytearray()
  sources = {}
  manifest = []
  for sound_id, role, filename, match, reason in effects:
    if filename not in sources:
      pcm = ReadPCM(AUDIO / filename)
      sources[filename] = (offset, len(pcm) // 2)
      payload.extend(pcm)
      offset += len(pcm)
    if len(filename.encode()) >= 64:
      raise ValueError(f"Source path exceeds native record capacity: {filename}")
    start, frames = sources[filename]
    records.extend(struct.pack("<IIII64s", sound_id, start, frames,
        {"match": 0, "approximation": 1, "generated": 2}[match], filename.encode()))
    manifest.append(dict(id=sound_id, role=role, source=filename, match=match, note=reason,
        frames=frames, sha256=hashlib.sha256((AUDIO / filename).read_bytes()).hexdigest()))
    for original, edited, duration, gain, fade_in, fade_out in RECORDING_EDITS:
      if filename != f"edited/{edited}": continue
      original = f"opensfx/effects/{original}"
      manifest[-1].update(original_source=original,
          original_sha256=hashlib.sha256((AUDIO / original).read_bytes()).hexdigest(),
          edits=dict(maximum_duration_ms=duration, amplitude_percent=gain,
              fade_in_ms=fade_in, fade_out_ms=fade_out))
  pack = struct.pack("<8sIIHHI", b"SC2KSFX\0", len(effects), RATE, 1, 16, offset) + records + payload
  output.parent.mkdir(parents=True, exist_ok=True)
  output.write_bytes(pack)
  metadata = AUDIO / "effects.json" if not args.ids and not args.output else output.with_suffix(".json")
  metadata.write_text(json.dumps(dict(sample_rate=RATE, channels=1,
      bits=16, effects=manifest), indent=2) + "\n")
  print(f"Baked {len(effects)} roles / {len(sources)} recordings: {len(pack):,} bytes")


if __name__ == "__main__":
  main()
