#!/usr/bin/env python3
"""Explicitly import licensed Freesound excerpts as development WAV sources.

Normal builds never run this tool. Public HQ preview URLs, licenses, input hashes
and excerpt recipes are pinned in assets/audio/freesound/sources.json. Full-quality
original downloads require Freesound authentication; no credentials are used here.
"""
import argparse
import array
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import wave


ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "assets/audio/freesound"
RATE = 44100


def Fetch(url):
  with urllib.request.urlopen(url, timeout=60) as response:
    return response.read()


def Convert(source, recipe, ffmpeg):
  command = [ffmpeg, "-v", "error", "-i", str(source), "-ss", str(recipe["start_seconds"]),
      "-t", str(recipe["duration_seconds"]), "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"]
  samples = array.array("h", subprocess.run(command, capture_output=True, check=True).stdout)
  if sys.byteorder != "little":
    samples.byteswap()
  if not samples or len(samples) != round(recipe["duration_seconds"] * RATE):
    raise ValueError(f"Incomplete excerpt for {recipe['id']}")
  peak = max(abs(value) for value in samples)
  if not peak:
    raise ValueError(f"Silent excerpt for {recipe['id']}")
  gain = recipe["peak_gain"] * 32767 / peak
  for index, value in enumerate(samples):
    fade = min(1, index / (RATE * .010), (len(samples) - index - 1) / (RATE * .025))
    samples[index] = round(value * gain * fade)
  if sys.byteorder != "little":
    samples.byteswap()
  return samples.tobytes()


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--cache", type=Path, default=ROOT / "build/asset-imports/freesound")
  args = parser.parse_args()
  ffmpeg = shutil.which("ffmpeg")
  if not ffmpeg:
    parser.error("ffmpeg is required only for this explicit offline import")
  args.cache.mkdir(parents=True, exist_ok=True)
  recipes = json.loads((DESTINATION / "sources.json").read_text())["samples"]
  credits = ["# Freesound sound credits", "",
      "These excerpts use the publicly available HQ MP3 previews, not the authenticated original downloads.",
      "All edits happen offline; only the resulting native PCM pack and credits ship.", ""]
  for recipe in recipes:
    if recipe["license"] not in ("CC0-1.0", "CC-BY-4.0"):
      raise ValueError(f"Unapproved source license: {recipe['license']}")
    source = args.cache / f"{recipe['id']}.mp3"
    if not source.exists():
      source.write_bytes(Fetch(recipe["download_url"]))
    if hashlib.sha256(source.read_bytes()).hexdigest() != recipe["download_sha256"]:
      raise ValueError(f"Source changed: verify provenance before updating {source}")
    pcm = Convert(source, recipe, ffmpeg)
    with wave.open(str(DESTINATION / recipe["file"]), "wb") as output:
      output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
      output.writeframes(pcm)
    credits.extend([f"## {recipe['file']}", "",
        f"[{recipe['title']}]({recipe['url']}) by {recipe['author']} on Freesound.org.",
        f"License: [{recipe['license']}]({recipe['license_url']}).",
        f"Excerpt: {recipe['start_seconds']} seconds, duration {recipe['duration_seconds']} seconds.",
        f"Changes: {recipe['changes']}", ""])
  (DESTINATION / "CREDITS.md").write_text("\n".join(credits))
  license_file = DESTINATION / "LICENSE-CC-BY-4.0.txt"
  if not license_file.exists():
    license_file.write_bytes(Fetch("https://raw.githubusercontent.com/spdx/license-list-data/main/text/CC-BY-4.0.txt"))
  print(f"Imported {len(recipes)} attributed Freesound excerpts as 44.1 kHz mono PCM WAVs")


if __name__ == "__main__":
  main()
