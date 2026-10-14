"""The options menu's button icons (platform/controls.c) from Xelu's free
prompts pack (CC0: https://github.com/haaldor/Xelu_prompts_SVG, its
exported_images_100.zip unpacked), scaled to 48 px into native/assets/prompts/:
<ps3|ps4|ps5|xbox360|xboxone|xboxseries|switch>_<SDL's name of the button>.png and key_<SDL scancode>.png.
python native/tools/prompts.py <unpacked folder>"""
import sys
from pathlib import Path

from PIL import Image

NATIVE = Path(__file__).resolve().parents[1]
OUT = NATIVE / "assets/prompts"
SIZE = 48

# SDL's gamepad buttons go by place (a the bottom one): each pad's own label there
PS = {"a": "Cross", "b": "Circle", "x": "Square", "y": "Triangle", "leftshoulder": "L1", "rightshoulder": "R1",
      "lefttrigger": "L2", "righttrigger": "R2"}
XBOX = {"a": "A", "b": "B", "x": "X", "y": "Y", "leftshoulder": "LB", "rightshoulder": "RB", "lefttrigger": "LT",
        "righttrigger": "RT"}
PADS = {
    "ps3": ("Others/PS3", "PS3_", {**PS, "back": "Select", "start": "Start"}),
    "ps4": ("Others/PS4", "PS4_", {**PS, "back": "Share", "start": "Options"}),
    "ps5": ("PS5", "PS5_", {**PS, "back": "Share", "start": "Options"}),
    "xbox360": ("Others/Xbox 360", "360_", {**XBOX, "back": "Back", "start": "Start"}),
    "xboxone": ("Others/Xbox One", "XboxOne_", {**XBOX, "back": "Windows", "start": "Menu"}),
    "xboxseries": ("Xbox Series", "XboxSeriesX_", {**XBOX, "back": "View", "start": "Menu"}),
    "switch": ("Switch", "Switch_", {"a": "B", "b": "A", "x": "Y", "y": "X", "back": "Minus", "start": "Plus",
                                     "leftshoulder": "LB", "rightshoulder": "RB", "lefttrigger": "LT",
                                     "righttrigger": "RT"}),
}
COMMON = {"leftstick": "Left_Stick_Click", "rightstick": "Right_Stick_Click", "dpup": "Dpad_Up",
          "dpdown": "Dpad_Down", "dpleft": "Dpad_Left", "dpright": "Dpad_Right", "dpad": "Dpad"}

# Xelu's key names to SDL scancodes (the USB ones), both sides' for Ctrl, Shift, Alt, Win
KEYS = {"Arrow_Up": [82], "Arrow_Down": [81], "Arrow_Left": [80], "Arrow_Right": [79], "Space": [44],
        "Enter": [40, 88], "Esc": [41], "Backspace": [42], "Tab": [43], "Minus": [45, 86], "Plus": [87],
        "Bracket_Left": [47], "Bracket_Right": [48], "Semicolon": [51], "Quote": [52], "Tilda": [53],
        "Mark_Left": [54], "Mark_Right": [55], "Slash": [56], "Asterisk": [85], "Caps_Lock": [57],
        "Print_Screen": [70], "Insert": [73], "Home": [74], "Page_Up": [75], "Del": [76], "End": [77],
        "Page_Down": [78], "Num_Lock": [83], "Ctrl": [224, 228], "Shift": [225, 229], "Alt": [226, 230],
        "Win": [227, 231]}
KEYS.update({chr(c): [4 + c - ord("A")] for c in range(ord("A"), ord("Z") + 1)})
KEYS.update({str(d): [30 + (d - 1) % 10] for d in range(10)})
KEYS.update({f"F{n}": [57 + n] for n in range(1, 13)})


def save(src, name):
    Image.open(src).convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS).save(OUT / name, optimize=True)


def main(pack):
    OUT.mkdir(parents=True, exist_ok=True)
    for fam, (folder, prefix, own) in PADS.items():
        for sdl, xelu in {**own, **COMMON}.items():
            save(pack / folder / f"{prefix}{xelu}.png", f"{fam}_{sdl}.png")
    for xelu, codes in KEYS.items():
        for code in codes:
            save(pack / "Keyboard & Mouse/Dark" / f"{xelu}_Key_Dark.png", f"key_{code}.png")
    print(f"OK {len(list(OUT.glob('*.png')))} icons in {OUT}")


if __name__ == "__main__":
    assert KEYS["A"] == [4] and KEYS["Z"] == [29] and KEYS["1"] == [30] and KEYS["0"] == [39] and KEYS["F12"] == [69]
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(Path(sys.argv[1]))
