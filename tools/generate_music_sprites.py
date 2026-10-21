"""Pack bundled OpenGFX2 transport artwork as native run sprites."""
from pathlib import Path
from pack_landscape_assets import picture_pack
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets/ui/opengfx2/music-transport-2x.png"
OUTPUT = ROOT / "assets/ui/opengfx2/music/transport.dat"


def generate() -> None:
  image = Image.open(SOURCE).convert("RGBA")
  assert image.size == (160, 40)
  images = []
  for index in range(4):
    sprite = image.crop((index * 40, 0, index * 40 + 40, 40))
    sprite.putdata([(0, 0, 0, 0) if (r, g, b) == (0, 0, 255) else (r, g, b, a)
        for r, g, b, a in sprite.get_flattened_data()])
    images.append((index, sprite))
  OUTPUT.parent.mkdir(parents=True, exist_ok=True)
  OUTPUT.write_bytes(picture_pack(images))



if __name__ == "__main__":
  generate()
