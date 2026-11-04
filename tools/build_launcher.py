"""Builds the launcher into build/, next to the game and its SDL3.dll.
Run from anywhere: python tools/build_launcher.py"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
OUT = NATIVE / "build"
SDL = NATIVE / "third_party/SDL3-3.4.16/i686-w64-mingw32"


def compiler():
    if os.environ.get("LLVM_MINGW"):
        return str(Path(os.environ["LLVM_MINGW"]) / "bin/i686-w64-mingw32-clang.exe")
    found = shutil.which("i686-w64-mingw32-clang")
    if not found:
        sys.exit("no llvm-mingw: set LLVM_MINGW to its folder, or put its bin/ on the PATH")
    return found


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "dw3launcher.exe"
    sources = [*sorted((NATIVE / "launcher").glob("*.c")), NATIVE / "platform/extract.c"]
    tag = subprocess.run(["git", "describe", "--tags", "--abbrev=0"], cwd=NATIVE, capture_output=True, text=True).stdout.strip()
    # -static: the C runtime inside the exe, so that only SDL3.dll sits next to it
    subprocess.run([compiler(), "-std=c11", "-O2", "-Wall", "-Wextra", "-Wno-missing-field-initializers",
                    f"-I{SDL / 'include'}", f"-I{NATIVE / 'platform'}", f'-DLAUNCHER_VERSION="{tag or "dev"}"',
                    *map(str, sources), str(SDL / "lib/libSDL3.dll.a"), "-lwinhttp", "-static", "-mwindows", "-o", str(exe)], check=True)
    if not (OUT / "SDL3.dll").exists():
        shutil.copy2(SDL / "bin/SDL3.dll", OUT / "SDL3.dll")
    print(f"OK {exe} ({exe.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
