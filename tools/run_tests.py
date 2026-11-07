"""Builds and runs the unit tests that need neither the game's data nor its code:

    python native/tools/run_tests.py [name ...]      (no names: all)

Each test is compiled with llvm-mingw (build.toolchain) and SDL (third_party), into a scratch folder with SDL3.dll beside it, and
run there, so nothing of the tree or of the player's is touched. The exit code is the number of tests that failed (CI runs this).
The tests that read the disc or the exe (test_movies, test_spu, test_extract) and the ones that need the game's code
are run by hand, as their own headers say."""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE))
sys.argv, argv = sys.argv[:1], sys.argv[1:]  # build.py reads sys.argv for its own flags
import build  # noqa: E402

# name: (the sources, with -lSDL3 when it needs it). Each is a tools/test_<name>.c
TESTS = {
    "json": ["platform/json.c"],
    "mods": ["platform/digimon_mods.c", "platform/json.c"],
    "effect_stages": ["platform/effects.c", "platform/clips.c", "platform/soundlib.c", "platform/scriptmod.c", "platform/json.c"],
    "scriptmod": ["platform/scriptmod.c", "platform/json.c"],
    "forms": ["platform/forms.c"],
    "memcard_forms": ["platform/memcard.c", "platform/forms.c"],
    "gte": [],
    "softvram": ["platform/gpu_draw.c", "platform/gpu.c"],
}


def run_one(name, work):
    exe = work / f"test_{name}.exe"
    cmd = [build.CC, "-std=gnu11", "-Wno-everything", "-I", NATIVE / "platform", "-I", NATIVE / "include", "-I", build.SDL / "include",
           "-include", "stdint.h", NATIVE / f"tools/test_{name}.c", *(NATIVE / s for s in TESTS[name]),
           f"-L{build.SDL / 'lib'}", "-lSDL3", "-o", exe]
    r = build.run(cmd)
    if r.returncode != 0:
        return False, "does not build:\n" + r.stderr[-1500:]
    r = subprocess.run([str(exe)], cwd=work, capture_output=True, text=True, timeout=300)
    out = (r.stdout + r.stderr).strip().splitlines()
    return r.returncode == 0, "\n".join(out[-6:]) if r.returncode else ""


def main():
    names = argv or list(TESTS)
    for n in names:
        if n not in TESTS:
            sys.exit(f"no test {n}; they are: {', '.join(TESTS)}")
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        shutil.copy2(build.SDL / "bin/SDL3.dll", work)
        for n in names:
            ok, what = run_one(n, work)
            failed += not ok
            print(f"{'ok  ' if ok else 'FAIL'} {n}" + (f": {what}" if what else ""))
    print(f"{len(names) - failed}/{len(names)} passed")
    sys.exit(failed)


if __name__ == "__main__":
    main()
