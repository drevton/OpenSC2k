#!/usr/bin/env python3
"""Import the curated, unmodified OpenSFX sources and their individual credits."""
import hashlib
import json
from pathlib import Path
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "assets/audio/opensfx"
REVISION = "d0bf8d4deda22d5a7f8d01953433cb2644663e9f"
BASE = f"https://raw.githubusercontent.com/OpenTTD/OpenSFX/{REVISION}"
SAMPLES = (1, 2, 6, 9, 10, 12, 18, 19, 23, 24, 27, 31, 33, 39, 66)


def Fetch(url):
  with urllib.request.urlopen(url, timeout=60) as response:
    return response.read()


def main():
  (DESTINATION / "effects").mkdir(parents=True, exist_ok=True)
  credits = Fetch(f"{BASE}/src/opensfx.psfo").decode()
  imported = []
  for sample in SAMPLES:
    name = f"osfx_{sample:02}.wav"
    source = f"{BASE}/src/wav/{name}"
    data = Fetch(source)
    (DESTINATION / "effects" / name).write_bytes(data)
    credit = next(line for line in credits.splitlines() if line.startswith(f'"src/wav/{name}"'))
    imported.append(dict(file=f"effects/{name}", url=source,
        sha256=hashlib.sha256(data).hexdigest(), attribution=credit))
  # selection.wav was previously imported from the same pinned upstream revision.
  imported.append(dict(file="selection.wav", url=f"{BASE}/src/wav/osfx_21.wav",
      sha256=hashlib.sha256((DESTINATION / "selection.wav").read_bytes()).hexdigest(),
      attribution=next(line for line in credits.splitlines() if line.startswith('"src/wav/osfx_21.wav"'))))
  (DESTINATION / "credits.json").write_text(json.dumps(dict(revision=REVISION, samples=imported), indent=2) + "\n")
  for filename, url in (
      ("LICENSE-CC-BY-SA-3.0.txt",
          "https://raw.githubusercontent.com/spdx/license-list-data/main/text/CC-BY-SA-3.0.txt"),
      ("LICENSE-CC0-1.0.txt", "https://raw.githubusercontent.com/spdx/license-list-data/main/text/CC0-1.0.txt")):
    (DESTINATION / filename).write_bytes(Fetch(url))
  print(f"Imported {len(SAMPLES)} OpenSFX source WAVs and credits")


if __name__ == "__main__":
  main()
