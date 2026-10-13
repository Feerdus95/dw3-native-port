"""The game's bytes in the exe. The decomp's C carries the game's own tables
(file sectors, stats, scripts...), so they end up in the exe's .data and
.rdata. The build takes them out (strip): every 32-byte run of those sections
that also sits in the player's SLES_039.36 or a disc file (the overlays and
the stages' data, where the decomp's tables come from), at the same 4-byte
alignment, is zeroed, and <exe>.fill lists where each run comes from:

    crc <crc32 of all the runs' bytes, in order> <count>
    <rva> <length> <file under data/> <offset>

platform/fill.c reads them back from the player's files at startup, before
anything runs. Runs of one or two byte values (zeros, padding) aren't
looked at.

    python native/tools/asset_scan.py [exe]       what of the game is in it
                                                  (any disc file); 0 to ship
"""
import multiprocessing
import struct
import sys
import zlib
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1]
DATA = NATIVE / "data"
W = 32


def sections(exe):
    """(file offset, size, rva) of the PE's initialised data sections"""
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    count = struct.unpack_from("<H", exe, pe + 6)[0]
    opt = struct.unpack_from("<H", exe, pe + 20)[0]
    out = []
    for i in range(count):
        s = pe + 24 + opt + i * 40
        name = exe[s:s + 8].rstrip(b"\0")
        vsize, rva, size, raw = struct.unpack_from("<IIII", exe, s + 8)
        if name in (b".data", b".rdata") and size:
            out.append((raw, min(size, vsize), rva))
    return out


def index(exe, nonzero):
    """each non-plain 32-byte window of the data sections, with at least
    `nonzero` bytes that aren't 0: its offsets"""
    idx = {}
    for off, size, _ in sections(exe):
        for o in range(off, off + size - W + 1, 4):
            w = exe[o:o + W]
            if len(set(w)) > 2 and W - w.count(0) >= nonzero:
                idx.setdefault(w, []).append(o)
    return idx


def sources():
    """names under data/: the executable, then every disc file"""
    return ["SLES_039.36"] + [f"files/{p.name}" for p in sorted((DATA / "files").glob("*.bin"))]


_windows = None


def _init(windows):
    global _windows
    _windows = windows


def _first_places(name):
    """the windows of the exe this file has: [(window, offset)], each its first place"""
    b, found = (DATA / name).read_bytes(), {}
    for o in range(0, len(b) - W + 1, 4):
        w = b[o:o + W]
        if w in _windows and w not in found:
            found[w] = o
    return name, list(found.items())


def owners(exe, nonzero=0):
    """exe offset -> (source, offset) for every byte found in the game's files.
    The files are searched on every core; a window goes to the first file, in
    sources() order, that has it"""
    idx, own = index(exe, nonzero), {}
    with multiprocessing.Pool(initializer=_init, initargs=(set(idx),)) as pool:
        for name, places in pool.imap(_first_places, sources(), chunksize=8):
            for w, o in places:
                for e in idx.pop(w, ()):
                    for k in range(W):
                        own.setdefault(e + k, (name, o + k))
    return own


def runs(own):
    """[(exe offset, length, source, source offset)], contiguous in both"""
    out = []
    for e in sorted(own):
        name, o = own[e]
        if out and out[-1][0] + out[-1][1] == e and out[-1][2] == name and out[-1][3] + out[-1][1] == o:
            out[-1][1] += 1
        else:
            out.append([e, 1, name, o])
    return out


def rva_of(exe, off):
    for raw, size, rva in sections(exe):
        if raw <= off < raw + size:
            return rva + off - raw
    raise ValueError(off)


def strip(exe_path):
    """zeroes the game's runs in the exe and writes <exe>.fill; returns their
    bytes. Without data/ (a first build) the tables stay and the .fill is
    empty: that exe extracts the disc itself, the next build strips"""
    if not (DATA / "SLES_039.36").exists():
        exe_path.with_suffix(".fill").write_text("crc 00000000 0\n")
        return 0
    exe = bytearray(exe_path.read_bytes())
    rs = runs(owners(bytes(exe)))
    crc = 0
    lines = []
    for e, n, name, o in rs:
        crc = zlib.crc32(exe[e:e + n], crc)
        lines.append(f"{rva_of(exe, e):x} {n} {name} {o}\n")
        exe[e:e + n] = bytes(n)
    exe_path.write_bytes(exe)
    exe_path.with_suffix(".fill").write_text(f"crc {crc:08x} {len(rs)}\n" + "".join(lines))
    return sum(r[1] for r in rs)


def main():
    exe = Path(sys.argv[1] if len(sys.argv) > 1 else NATIVE / "build/dw2003.exe").read_bytes()
    # zeroing leaves windows of mostly zeros that match the disc's padding by
    # chance; a table of the game has some values in 32 bytes
    rs = runs(owners(exe, nonzero=8))
    total = sum(s for _, s, _ in sections(exe))
    found = sum(r[1] for r in rs)
    print(f"data sections {total} bytes, {found} of them are the game's ({100 * found / total:.2f}%)")
    for e, n, name, o in sorted(rs, key=lambda r: -r[1])[:10]:
        print(f"  exe+{e:#x}: {n} bytes = {name}+{o:#x}  {exe[e:e + 16].hex()}")
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
