"""Packs the port for players into native/dist/dw2003-native/ and a zip next to
it: the exe, SDL3.dll, the extractor, the README, credits and license, an
example native.ini. Nothing from the game: it refuses to finish if anything of
the disc or a save got in. Run from anywhere: python native/tools/package.py"""
import shutil
import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
DIST = NATIVE / "dist"
OUT = DIST / "dw2003-native"
FILES = {
    "dw2003.exe": NATIVE / "build/dw2003.exe",
    "SDL3.dll": NATIVE / "build/SDL3.dll",
    "tools/extract_iso.py": NATIVE / "tools/extract_iso.py",
    "README.md": NATIVE / "README.md",
    "CREDITS.md": NATIVE / "CREDITS.md",
    "native.ini.example": NATIVE / "native.ini.example",
    "LICENSE": NATIVE / "LICENSE",
}
# what the disc or a player brings, never ours to ship
FORBIDDEN_SUFFIXES = {".bin", ".str", ".cue", ".iso", ".mcd", ".36"}
FORBIDDEN_DIRS = {"data", "saves"}


def check(root):
    """Paths in root that come from the game or a player"""
    bad = []
    for p in root.rglob("*"):
        rel = p.relative_to(root)
        if rel.parts[0] in FORBIDDEN_DIRS or p.suffix.lower() in FORBIDDEN_SUFFIXES:
            bad.append(rel.as_posix())
    return bad


def main():
    missing = [name for name, src in FILES.items() if not src.exists()]
    if missing:
        sys.exit(f"missing: {', '.join(missing)} (build first; LICENSE is still to be chosen)")
    if OUT.exists():
        shutil.rmtree(OUT)
    for name, src in FILES.items():
        dst = OUT / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    bad = check(OUT)
    if bad:
        shutil.rmtree(OUT)
        sys.exit(f"refused, game or player files: {bad}")
    zip_path = shutil.make_archive(str(DIST / "dw2003-native"), "zip", OUT)
    print(f"OK {OUT} and {zip_path}")


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        import tempfile
        t = Path(tempfile.mkdtemp())
        (t / "data").mkdir()
        (t / "data/SLES_039.36").write_bytes(b"x")
        (t / "dw2003.exe").write_bytes(b"x")
        assert check(t) == ["data", "data/SLES_039.36"], check(t)
        print("selftest OK")
    else:
        main()
