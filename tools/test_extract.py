"""The disc reader's failures on the real image: python tools/test_extract.py IMAGE.cue WORK_FOLDER
Needs about 2 GB in WORK_FOLDER, and deletes it afterwards. Asserts what the first run promises: a good image
is complete, a bad one leaves the data folder as it was."""
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
SDL = NATIVE / "third_party/SDL3-3.4.16/i686-w64-mingw32"
E_NONE, E_OPEN, E_GAME, E_BAD, E_WRITE, E_READ, E_TRUNCATED, E_CUE, E_SPACE, E_CANCELLED = range(10)


def tree(root):
    """Every file under root by relative path, with its hash for the small ones"""
    return {p.relative_to(root).as_posix(): (p.stat().st_size, hashlib.md5(p.read_bytes()).hexdigest() if p.stat().st_size < 65536 else "")
            for p in Path(root).rglob("*") if p.is_file()}


def main():
    cue, work = Path(sys.argv[1]), Path(sys.argv[2])
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    exe = work / "test_extract.exe"
    subprocess.run(["i686-w64-mingw32-clang", "-std=c11", "-O1", f"-I{SDL / 'include'}", f"-I{NATIVE / 'platform'}", NATIVE / "tools/test_extract.c",
                    NATIVE / "platform/extract.c", SDL / "lib/libSDL3.dll.a", "-static", "-o", exe], check=True)
    shutil.copy2(SDL / "bin/SDL3.dll", work)

    def run(image, data, *extra):
        return subprocess.run([exe, image, data, *map(str, extra)], capture_output=True, text=True).returncode

    bin_name = next(line.split('"')[1] for line in cue.read_text().splitlines() if "FILE" in line)
    image = cue.parent / bin_name
    size = image.stat().st_size

    # an earlier incomplete run, and a mod that must survive everything
    data = work / "data"
    (data / "files").mkdir(parents=True)
    (data / "files/9999.bin").write_bytes(b"stale")
    (data / "mods").mkdir()
    (data / "mods/keep.txt").write_bytes(b"mine")
    before = tree(data)

    def unchanged(what):
        assert tree(data) == before, (what, set(tree(data)) ^ set(before))
        print("OK", what)

    # a .bin cut short after its directory: past the file table's end
    cut = work / "cut.bin"
    with image.open("rb") as src, cut.open("wb") as dst:
        dst.write(src.read(size // 2 // 2352 * 2352))
    assert run(cut, data) == E_TRUNCATED
    unchanged("truncated image")
    cut.unlink()

    junk = work / "junk.bin"
    junk.write_bytes(bytes(2352 * 64))
    assert run(junk, data) == E_GAME
    unchanged("not a game")
    junk.unlink()

    assert run(work / "nothing.cue", data) == E_OPEN
    unchanged("missing .cue")

    multi = work / "multi.cue"
    multi.write_text('FILE "a.bin" BINARY\n  TRACK 01 MODE2/2352\nFILE "b.bin" BINARY\n  TRACK 02 AUDIO\n')
    assert run(multi, data) == E_CUE
    unchanged("multi-file .cue")

    assert run(cue, data, 50) == E_CANCELLED
    unchanged("cancelled halfway")

    readonly = work / "file-not-folder"
    readonly.write_bytes(b"x")
    assert run(cue, readonly) == E_WRITE
    print("OK data folder not writable")

    # the .cue names another file: the .bin of the .cue's own name is used. A hard link, the image's too big to copy.
    twin = work / "renamed"
    twin.mkdir()
    (twin / "game.bin").hardlink_to(image)
    (twin / "game.cue").write_text('FILE "gone.bin" BINARY\n  TRACK 01 MODE2/2352\n')
    accented = work / "dátà ñ"
    assert run(twin / "game.cue", accented) == E_NONE
    assert (accented / "SLES_039.36").exists() and (accented / "files").is_dir() and not (accented / ".extract-tmp").exists()
    print("OK renamed .bin, through the .cue, into an accented path")
    expected = tree(accented)
    shutil.rmtree(accented)

    assert run(cue, data) == E_NONE
    got = tree(data)
    assert "files/9999.bin" not in got and got["mods/keep.txt"] == before["mods/keep.txt"]
    del got["mods/keep.txt"]
    assert got == expected and not (data / ".extract-tmp").exists()
    print(f"OK complete: {len(got)} files; the stale one gone, the mod kept")

    # the names, as the model viewer's own setup_disc.py writes them, in a full extraction and on their own
    names = (data / "names.txt").read_text(encoding="ascii").splitlines()
    assert len(names) == 2382 and "NONE____.BIN" in names[0]
    (data / "names.txt").unlink()
    assert run(cue, data, "names") == E_NONE and (data / "names.txt").read_text(encoding="ascii").splitlines() == names
    assert run(work / "nothing.cue", data, "names") == E_OPEN
    viewer_names = NATIVE.parent / "viewer/data/names.txt"
    if viewer_names.exists():
        assert viewer_names.read_text(encoding="ascii").splitlines() == names
    print("OK names.txt, the same as setup_disc.py's")
    del got["names.txt"]

    reference = NATIVE / "data"
    if (reference / "SLES_039.36").exists():
        for name, (bytes_, _) in got.items():
            other = reference / name
            assert other.exists() and other.stat().st_size == bytes_, name
        print("OK same files and sizes as native/data")
    shutil.rmtree(work)


if __name__ == "__main__":
    main()
