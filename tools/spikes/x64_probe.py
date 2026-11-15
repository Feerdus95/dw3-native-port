"""How many places in the game's C care that a pointer is 4 bytes? Syntax-only pass over every game source with
the x86_64 target and the pointer/int warnings on. Nothing is written but the report."""
import sys, re, collections, concurrent.futures as cf
sys.path.insert(0, r"F:\Juegos\DW2003 - Decomp\native")
import build

CC64 = build.TC / "x86_64-w64-mingw32-clang.exe"
flags = [f for f in build.GAME_CFLAGS if f != "-Wno-everything"]
flags = [f for f in flags if f not in ("-g",)]
flags += ["-fsyntax-only", "-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wint-conversion",
          "-Wno-error", "-w", "-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wint-conversion"]
# -w silences everything; re-enable just the ones we want after it
flags = [f for f in flags if f != "-w"]
flags += ["-Wno-everything", "-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wint-conversion"]
WARN = re.compile(r"^(.*?):(\d+):\d+: warning: (.*?) \[-W([a-z-]+)\]", re.M)


def one(src):
    rel = src.relative_to(build.DECOMP / "src")
    real = build.PATCHED / "src" / rel
    if not real.exists():
        real = src
    r = build.run([CC64, *flags, f"-I{src.parent}", real])
    return rel, r.stderr, r.returncode


srcs = build.game_sources()
by_flag, by_file, errs = collections.Counter(), collections.Counter(), []
sample = collections.defaultdict(list)
with cf.ThreadPoolExecutor(max_workers=4) as ex:
    for rel, err, rc in ex.map(one, srcs):
        for m in WARN.finditer(err):
            by_flag[m.group(4)] += 1
            by_file[str(rel)] += 1
            if len(sample[m.group(4)]) < 6:
                sample[m.group(4)].append(f"{rel}:{m.group(2)} {m.group(3)[:90]}")
        if rc != 0:
            errs.append((str(rel), err.strip().splitlines()[-1] if err.strip() else ""))
print("files:", len(srcs), "failed to parse:", len(errs))
print("by warning:", dict(by_flag))
print("files with at least one:", len(by_file))
print("top files:", by_file.most_common(12))
for k, v in sample.items():
    print("--", k)
    for s in v:
        print("   ", s)
for e in errs[:5]:
    print("ERR", e)
