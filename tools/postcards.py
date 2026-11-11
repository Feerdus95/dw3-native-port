"""The journal's pictures of the fields (platform/postcards.c), from the
player's own disc: each stage's map as the field editor draws it
(dw3fe --shot), cut to the journal's frame and scaled, into
build/data/postcards/XXX.png for each of the stage's modes (the decomp's
stage tables). None ship: they are the game's art.
python native/tools/postcards.py <dw3fe.exe> [--disc <disc>]"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

NATIVE = Path(__file__).resolve().parents[1]
BUILD = NATIVE / "build"
OUT = BUILD / "data/postcards"
TABLE = NATIVE.parent / "dw3_decomp/src/field/field_mode/data/fieldstg.c"
SIZE = (624, 200)  # the frame's inside (156 x 50), four times over


def cover(im: Image.Image) -> Image.Image:
    """The picture's middle at the frame's shape: as wide a view as fills it"""
    w, h = im.size
    cw = min(w, h * SIZE[0] // SIZE[1])
    ch = min(h, w * SIZE[1] // SIZE[0])
    left, top = (w - cw) // 2, (h - ch) // 2
    return im.crop((left, top, left + cw, top + ch)).resize(SIZE, Image.LANCZOS)


def main() -> None:
    dw3fe, extra = sys.argv[1], sys.argv[2:]
    stages: dict[str, list[int]] = {}
    for mode, stage in sorted(set(re.findall(r"\{(\d+), \d+, WSTAG(\d+)_startStage\}", TABLE.read_text(encoding="utf-8")))):
        stages.setdefault(f"wstag{stage}", []).append(int(mode))
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        shot = Path(tmp) / "shot.png"
        done = 0
        for n, (stage, modes) in enumerate(stages.items(), 1):
            shot.unlink(missing_ok=True)
            if subprocess.run([dw3fe, *extra, "--shot", str(shot), stage], capture_output=True).returncode == 0 and shot.exists():
                picture = cover(Image.open(shot).convert("RGB"))
                for mode in modes:
                    picture.save(OUT / f"{mode:03X}.png")
                done += 1
            if n % 10 == 0 or n == len(stages):
                print(f"{n * 100 // len(stages)}% ({n}/{len(stages)})", flush=True)
    print(f"{done} stages' pictures in {OUT}")


if __name__ == "__main__":
    main()
