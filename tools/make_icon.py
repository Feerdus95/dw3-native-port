"""Builds platform/dw3.ico from a 1024 px picture of the icon (a rounded square on a dark background).
Run: python tools/make_icon.py <picture>   (.png, .jpg, .jfif)"""
import io
import struct
import sys
from pathlib import Path
from PIL import Image, ImageDraw

NATIVE = Path(__file__).resolve().parents[1]
SIZES = (16, 32, 48, 64, 128, 256)
# the square's edge in the picture and its corner radius (a few px inside, to leave out the glow around it)
EDGE, RADIUS = (76, 76, 948, 946), 168

src = Image.open(sys.argv[1]).convert("RGB").resize((1024, 1024), Image.LANCZOS)
mask = Image.new("L", (4096, 4096), 0)
ImageDraw.Draw(mask).rounded_rectangle([v * 4 for v in EDGE], radius=RADIUS * 4, fill=255)
icon = src.convert("RGBA")
icon.putalpha(mask.resize((1024, 1024), Image.LANCZOS))
icon = icon.crop((EDGE[0] - 4, EDGE[1] - 4, EDGE[2] + 4, EDGE[3] + 4))
side = max(icon.size)
square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
square.paste(icon, ((side - icon.width) // 2, (side - icon.height) // 2))
entries = []
for n in SIZES:
    buf = io.BytesIO()
    square.resize((n, n), Image.LANCZOS).save(buf, "PNG")
    entries.append((n, buf.getvalue()))
out = struct.pack("<HHH", 0, 1, len(entries))
offset = 6 + 16 * len(entries)
for n, png in entries:
    out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(png), offset)
    offset += len(png)
(NATIVE / "platform/dw3.ico").write_bytes(out + b"".join(png for _, png in entries))
print("platform/dw3.ico", [n for n, _ in entries])
