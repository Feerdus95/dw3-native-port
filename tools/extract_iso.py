"""BYO ISO: checks the user's Digimon World 2003 (Europe, SLES-03936) disc and
extracts what the native build reads into native/data/:

    data/SLES_039.36        the executable (for the boot image)
    data/files/NNNN.bin     every file of the game's file table, by fid
    data/files/NNNN.str     the movies again, whole sectors (2336 bytes:
                            XA subheader + data), as their audio is in
                            Form 2 sectors that the 2048-byte copy loses

    python native/tools/extract_iso.py "path/to/Digimon World 2003 (Europe).bin"

The game finds its files by fid through two tables in the executable
(FILE_SECTORS: first sector, FILE_SECTOR_COUNTS: sectors); the native build
reads data/files/<fid>.bin instead, so a file there can be any size.
"""
import hashlib
import struct
import sys
from pathlib import Path

EXE_NAME = "SLES_039.36"
EXE_SHA1 = "d1b7e4d646e3a9c2b88fdb25d20b5f7116bbb06d"
EXE_VRAM = 0x80010000
FILE_SECTORS = 0x80044F6C  # s32[2382]
FILE_SECTOR_COUNTS = 0x800474A4  # u16[2382]
FILE_COUNT = 2382
OUT = Path(__file__).resolve().parents[1] / "data"


class Disc:
    def __init__(self, path):
        self.f = open(path, "rb")
        size = Path(path).stat().st_size
        self.raw = size % 2352 == 0  # raw 2352-byte sectors (bin) or plain 2048 (iso)
        self.sectors = size // (2352 if self.raw else 2048)

    def read(self, lba, count=1):
        """The 2048 user bytes of each sector (Mode 2 Form 1 in a raw image)."""
        out = bytearray()
        for n in range(lba, lba + count):
            if self.raw:
                self.f.seek(n * 2352 + 24)
            else:
                self.f.seek(n * 2048)
            out += self.f.read(2048)
        return bytes(out)

    def read_raw(self, lba, count):
        """Whole sectors past the sync and header: subheader + 2328 bytes."""
        assert self.raw, "the movies need a raw (2352-byte sector) image: .bin/.cue"
        out = bytearray()
        for n in range(lba, lba + count):
            self.f.seek(n * 2352 + 16)
            out += self.f.read(2336)
        return bytes(out)

    def find(self, name):
        """A file in the ISO 9660 root directory: (lba, size)."""
        pvd = self.read(16)
        assert pvd[1:6] == b"CD001", "not an ISO 9660 disc"
        root_lba, root_size = struct.unpack_from("<I", pvd, 156 + 2)[0], struct.unpack_from("<I", pvd, 156 + 10)[0]
        data = self.read(root_lba, (root_size + 2047) // 2048)
        i = 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            ident = data[i + 33:i + 33 + data[i + 32]].decode("ascii", "replace").split(";")[0]
            if ident == name:
                return struct.unpack_from("<I", data, i + 2)[0], struct.unpack_from("<I", data, i + 10)[0]
            i += n
        raise FileNotFoundError(name)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = Path(sys.argv[1])
    if path.suffix.lower() == ".cue":
        path = path.with_suffix(".bin")
    disc = Disc(path)

    lba, size = disc.find(EXE_NAME)
    exe = disc.read(lba, (size + 2047) // 2048)[:size]
    if hashlib.sha1(exe).hexdigest() != EXE_SHA1:
        sys.exit(f"{EXE_NAME} doesn't match: this build needs Digimon World 2003 (Europe), SLES-03936")

    def table(addr, fmt, n):
        return struct.unpack_from(f"<{n}{fmt}", exe, addr - EXE_VRAM + 0x800)

    starts = table(FILE_SECTORS, "i", FILE_COUNT)
    counts = table(FILE_SECTOR_COUNTS, "H", FILE_COUNT)
    (OUT / "files").mkdir(parents=True, exist_ok=True)
    (OUT / EXE_NAME).write_bytes(exe)
    done = skipped = movies = 0
    for fid, (start, count) in enumerate(zip(starts, counts)):
        if start <= 0 or count == 0 or start + count > disc.sectors:
            skipped += 1
            continue
        (OUT / "files" / f"{fid:04d}.bin").write_bytes(disc.read(start, count))
        if disc.raw:
            raw = disc.read_raw(start, count)
            if any(raw[i + 2] & 0x20 for i in range(0, len(raw), 2336)):  # a Form 2 sector
                (OUT / "files" / f"{fid:04d}.str").write_bytes(raw)
                movies += 1
        done += 1
    print(f"{EXE_NAME} OK, {done} files extracted ({movies} movies), {skipped} empty or out of range -> {OUT}")


if __name__ == "__main__":
    main()
