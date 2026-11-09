"""Turns raw 128x64 frames from preview.cpp into GIFs that look like the blue OLED."""
import sys
from pathlib import Path

from PIL import Image

W, H, SCALE, FPS = 128, 64, 4, 20
ON = (110, 200, 255)   # blue OLED pixel
OFF = (6, 8, 12)


def to_image(buf: bytes) -> Image.Image:
    img = Image.new("RGB", (W, H), OFF)
    px = img.load()
    for i, v in enumerate(buf):
        if v:
            px[i % W, i // W] = ON
    return img.resize((W * SCALE, H * SCALE), Image.NEAREST)


def main(folder: str) -> None:
    out = Path(folder)
    stills = []
    for raw in sorted(out.glob("*.raw")):
        data = raw.read_bytes()
        n = len(data) // (W * H)
        frames = [to_image(data[i * W * H:(i + 1) * W * H]) for i in range(n)]
        if raw.stem.startswith("still_"):
            stills.append((raw.stem, frames[0]))
        elif n == 1:
            frames[0].save(raw.with_suffix(".png"))
        else:
            frames[0].save(raw.with_suffix(".gif"), save_all=True, append_images=frames[1:],
                           duration=1000 // FPS, loop=0)
        raw.unlink()
    stills.sort()
    # Contact sheet: one still per mood, 3 across.
    cols = 3
    rows = (len(stills) + cols - 1) // cols
    pad = 12
    sheet = Image.new("RGB", (cols * (W * SCALE + pad) + pad, rows * (H * SCALE + pad) + pad), (30, 30, 34))
    for i, (_, im) in enumerate(stills):
        sheet.paste(im, (pad + (i % cols) * (W * SCALE + pad), pad + (i // cols) * (H * SCALE + pad)))
    sheet.save(out / "all-moods.png")


if __name__ == "__main__":
    main(sys.argv[1])
