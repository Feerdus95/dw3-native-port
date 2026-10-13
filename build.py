"""Builds the native port: the C of dw3_decomp (EU), with native/patches
applied, plus the platform layer (native/platform) and SDL3, into
native/build/dw2003.exe. Whatever the game calls that nothing implements yet
gets a stub that logs once and returns 0; native/build/report.md lists them.

    python native/build.py          incremental
    python native/build.py --clean  from scratch
    python native/build.py --exe dev.exe   link under another name (while
                                           dw2003.exe is open and playing)
    python native/build.py --sanitize      bounds/null checks (UBSan) into
                                           native/build-san, logging and going on
    python native/build.py --O2            the game optimised, into native/build-o2
                                           (to become the default once played through)
"""
import collections
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parent
ROOT = NATIVE.parent
DECOMP = ROOT / "dw3_decomp"
# --sanitize: array indices out of bounds and null accesses the PSX let pass,
# logged and survived. UBSan only: ASan's instrumentation pushes the game's
# memory past 16 MB, and signed overflow is left out, as the PSX wraps and
# the game counts on it
SANITIZE = "--sanitize" in sys.argv
SAN_FLAGS = ["-fsanitize=bounds,null", "-fsanitize-recover=bounds,null",
             "-DNATIVE_HEAP_SIZE=(4*1024*1024)"] if SANITIZE else []
# the checks bloat -O0 code past the 16 MB too: that build is -O1, with the
# PSX GCC's leniency on type punning and overflow kept
GAME_OPT = ["-O1", "-fno-strict-aliasing", "-fwrapv"] if SANITIZE else ["-O0"]
# --O2: the PSX GCC's leniency kept (type punning, overflow, reads through
# NULL), and the IR emitted unoptimised so the rewrites below see the game's
# own operations before -O2 works on them
O2 = "--O2" in sys.argv and not SANITIZE
LENIENT = ["-fno-strict-aliasing", "-fwrapv", "-fno-delete-null-pointer-checks"]
EMIT_OPT = ["-O0", "-Xclang", "-disable-O0-optnone", *LENIENT] if O2 else GAME_OPT
GAME_OPT = ["-O2", *LENIENT] if O2 else GAME_OPT
OUT = NATIVE / ("build-san" if SANITIZE else "build-o2" if O2 else "build")
PATCHED = OUT / "patched"


def toolchain():
    """llvm-mingw's bin/ (github.com/mstorsjo/llvm-mingw): LLVM_MINGW=<its
    folder>, or its bin/ on the PATH, or a copy installed by a retcomm toolchain"""
    if os.environ.get("LLVM_MINGW"):
        return Path(os.environ["LLVM_MINGW"]) / "bin"
    if shutil.which("i686-w64-mingw32-clang"):
        return Path(shutil.which("i686-w64-mingw32-clang")).parent
    local = Path(os.environ.get("LOCALAPPDATA", "")) / "retcomm/toolchains/cmake-clang-v1/1.0.14/bin"
    if local.exists():
        return local
    sys.exit("no llvm-mingw: set LLVM_MINGW to its folder, or put its bin/ on the PATH (README, Building)")


TC = toolchain()
CC = TC / "i686-w64-mingw32-clang.exe"
NM = TC / "llvm-nm.exe"
OBJCOPY = TC / "llvm-objcopy.exe"
SDL = NATIVE / "third_party/SDL3-3.4.16/i686-w64-mingw32"
STAGE = re.compile(r"(wstag\d+)(_head)?\.c$")

GAME_CFLAGS = [
    "-std=gnu89", *EMIT_OPT, "-g", "-fcommon", "-Wno-everything", "-fsigned-char", "-fno-builtin", "-nostdinc",
    "-ferror-limit=0", "-DNATIVE", "-Werror=excess-initializers", "-Dmain=psx_main", "-include", "native.h",
    # K&R-style calls through `()` pointers and loose casts the PSX GCC took
    "-Wno-error=incompatible-function-pointer-types", "-Wno-error=incompatible-pointer-types",
    f"-I{NATIVE / 'include'}", f"-I{PATCHED / 'include'}",
    f"-I{DECOMP / 'external/psyq_headers/psyq_lib47/include'}", f"-I{DECOMP / 'src/stages'}",
    "-DVERSION_EU", "-D_LANGUAGE_C", "-DLANGUAGE_C", "-DSKIP_ASM", '-DASM_DIR="asm/eu"', *SAN_FLAGS,
]
PLATFORM_CFLAGS = ["-std=c11", "-c", "-O2", "-g", "-Wall", "-Wno-unused-parameter", f"-I{SDL / 'include'}", *SAN_FLAGS]
# fixed image base (no ASLR): the GPU tags keep 24-bit addresses, so the
# game's memory has to stay below 16 MB (main.c checks it at startup)
LDFLAGS = ["-g", "-Wl,--no-dynamicbase",
           f"-L{SDL / 'lib'}", "-lSDL3", *SAN_FLAGS]


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)


def apply_patches():
    """native/patches/<path in dw3_decomp>.patch -> build/patched/<path>. The
    whole include/ is mirrored there, since a quoted #include looks in the
    including header's own directory first and would miss a patched copy."""
    for h in (DECOMP / "include").rglob("*"):
        out = PATCHED / "include" / h.relative_to(DECOMP / "include")
        if h.is_file() and not (NATIVE / "patches" / f"{out.relative_to(PATCHED).as_posix()}.patch").exists():
            if not out.exists() or out.stat().st_mtime < h.stat().st_mtime:
                out.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(h, out)
    for patch in (NATIVE / "patches").rglob("*.patch"):
        rel = patch.relative_to(NATIVE / "patches").with_suffix("")
        out = PATCHED / rel
        if out.exists() and out.stat().st_mtime >= max(patch.stat().st_mtime, (DECOMP / rel).stat().st_mtime):
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        r = run(["patch", "-s", "-o", out, DECOMP / rel, patch])
        if r.returncode != 0:
            sys.exit(f"patch {patch} failed:\n{r.stdout}{r.stderr}")


def game_sources():
    """mk/version/eu.mk's C_SRC: every .c under src/ but the shared *.inc.c"""
    return sorted(p for p in (DECOMP / "src").rglob("*.c") if not p.name.endswith(".inc.c"))


RET_DEFINE = re.compile(r"^(define [^@]*?)\b(zeroext|signext) (i8|i16) @")
RET = re.compile(r"^(\s*)ret (i8|i16) (.+?)(, !dbg !\d+)?$")


def widen_returns(ll):
    """MIPS callees extend a u8/s16/... result to 32 bits, and the game relies
    on it: it calls `u8 f()` through `s32 (*)()` pointers and prototypes.
    x86 leaves the upper bits as garbage, so every game function returning
    i8/i16 is made to return the extended i32; callers reading 8 or 16 bits
    of it still get the same value."""
    out, ext, n = [], None, 0
    for line in ll.splitlines():
        m = RET_DEFINE.match(line)
        if m:
            ext = "zext" if m.group(2) == "zeroext" else "sext"
            line = RET_DEFINE.sub(r"\1i32 @", line, count=1)
        if line.startswith("define "):
            # the same for parameters: a MIPS callee masks a u8/s16 argument
            # itself, and the game passes full ints through `()` pointers;
            # without zeroext/signext LLVM doesn't trust the upper bits either
            name, at, params = line.partition("@")
            line = name + at + re.sub(r"\b(?:zeroext|signext) ", "", params)
        elif line == "}":
            ext = None
        elif ext and (m := RET.match(line)):
            n += 1
            out.append(f"{m.group(1)}%__ret{n} = {ext} {m.group(2)} {m.group(3)} to i32")
            line = f"{m.group(1)}ret i32 %__ret{n}{m.group(4) or ''}"
        out.append(line)
    return "\n".join(out) + "\n"


GLOBAL = re.compile(r"^(@\S+ = (?:[a-z_]+ )*global .*?)(, align \d+.*)$")
SUB_OVERLAYS = {"stages", "wfightmn", "wfightts"}


def overlay_sections(ll, group):
    """An overlay's variables go to .data$dw3<group>1, between the markers of
    platform/memory.c, so a load can put them back to their initial values
    as the PSX did by copying the file again. .bss too: zeros in the file.
    Group g is main's, m the mode overlays', s the stages'. A common symbol
    (main's game_bss.c, sound.c: each defined once) would ignore the section,
    so it becomes a plain definition."""
    sect = f', section ".data$dw3{group}1"'
    return "".join(m.group(1).replace(" = common ", " = ", 1) + sect + m.group(2) + "\n" if (m := GLOBAL.match(line)) and " external " not in line
                   and " section " not in line else line + "\n" for line in ll.splitlines())


DIV = re.compile(r"^(\s*%\S+ = )(sdiv|srem|udiv|urem)(?: exact)? i32 (\S+), (\S+?)(, !dbg !\d+)?$")


def mips_divisions(ll):
    """x86 traps on a division by zero (or INT_MIN / -1); the game, built for
    the PSX, can get there (a zeroed .bss where the PSX left old bytes), so a
    division by a variable calls platform/etc.c's native_<op>, which gives the
    R3000's result instead and logs it. A nonzero constant divisor stays."""
    out, used = [], set()
    for line in ll.splitlines():
        m = DIV.match(line)
        if m and (not re.fullmatch(r"-?\d+", m.group(4)) or m.group(4) in ("0", "-1")):
            used.add(m.group(2))
            line = f"{m.group(1)}call i32 @native_{m.group(2)}(i32 {m.group(3)}, i32 {m.group(4)}){m.group(5) or ''}"
        out.append(line)
    out += [f"declare i32 @native_{op}(i32, i32)" for op in sorted(used)]
    return "\n".join(out) + "\n"


def deps(d):
    """The files a clang -MD dependency file lists (spaces escaped as a
    backslash and a space, lines continued by a backslash)"""
    text = d.read_text(errors="replace").replace("\\\n", " ")
    text = text[text.index(": ") + 2:] if ": " in text else ""
    return [Path(p.replace("\\ ", " ")) for p in re.split(r"(?<!\\)\s+", text.strip()) if p]


def stale(obj, real, d):
    """Whether obj is older than its source, any header it included, or this
    script (whose IR rewrites go into every object)"""
    if not obj.exists() or not d.exists():
        return True
    t = obj.stat().st_mtime
    return any(not p.exists() or p.stat().st_mtime > t for p in [real, Path(__file__), *deps(d)])


def compile_game(src, force):
    rel = src.relative_to(DECOMP / "src")
    real = PATCHED / "src" / rel
    if not real.exists():
        real = src
    obj = OUT / "obj" / rel.with_suffix(".o")
    d = obj.with_suffix(".d")
    if not force and not stale(obj, real, d):
        return rel, obj, None, False
    obj.parent.mkdir(parents=True, exist_ok=True)
    ll = obj.with_suffix(".ll")
    r = run([CC, *GAME_CFLAGS, "-S", "-emit-llvm", "-MD", "-MF", d, f"-I{src.parent}", real, "-o", ll])
    if r.returncode != 0:
        return rel, None, r.stderr, True
    ll_text = mips_divisions(widen_returns(ll.read_text()))
    # main's variables in .data$dw3g1: with the overlays' after them, the
    # game's whole state is one range (platform/state.c's savestates)
    ll_text = overlay_sections(ll_text, "g" if rel.parts[0] == "main" else "s" if rel.parts[0] in SUB_OVERLAYS else "m")
    ll.write_text(ll_text)
    r = run([CC, "-c", *GAME_OPT, "-g", "-Wno-override-module", *SAN_FLAGS, ll, "-o", obj])
    return rel, obj if r.returncode == 0 else None, r.stderr, True


def prefix_stages(objs):
    """Each stage's own symbols get its name as a prefix (WSTAG931_startStage),
    as the decomp's link does, so the 294 stages share one executable. A
    stage's _head.c and main file are renamed together, references included."""
    stages = collections.defaultdict(list)
    for obj in objs:
        m = STAGE.match(obj.with_suffix(".c").name)
        if m:
            stages[m.group(1)].append(obj)
    for stage, group in stages.items():
        names = set()
        for obj in group:
            names.update(run([NM, "--format=just-symbols", "--defined-only", "--extern-only", obj]).stdout.split())
        pairs = OUT / "obj" / f"{stage}.syms"
        pairs.write_text("".join(f"{n} _{stage.upper()}_{n.lstrip('_')}\n" for n in sorted(names)))
        for obj in group:
            run([OBJCOPY, f"--redefine-syms={pairs}", obj])


def cname(sym):
    """The C name of an i686 symbol: cdecl adds one underscore. Only one goes,
    so the compiler's runtime (___divdi3 = __divdi3) stays apart from the game's."""
    return sym[1:] if sym.startswith("_") else sym


def symbols(obj):
    """(defined, undefined) external symbols, from one nm: "name type ..." lines"""
    defined, undefined = set(), set()
    for line in run([NM, "--format=posix", "--extern-only", obj]).stdout.splitlines():
        name, kind = line.split()[:2]
        (undefined if kind in "Uw" else defined).add(cname(name))
    return defined, undefined


# The dw3_decomp commit the patches and checks were made against. To move on:
# git pull in dw3_decomp, python native/build.py (patches that no longer apply
# fail here, by name), tools/layout_check.py, a smoke boot, then bump this.
DECOMP_COMMIT = "241649d58b5a6efdff87766a675e00faf5a49851"


def main():
    clean = "--clean" in sys.argv
    head = run(["git", "-C", DECOMP, "rev-parse", "HEAD"]).stdout.strip()
    if head and head != DECOMP_COMMIT:
        print(f"warning: dw3_decomp is at {head[:7]}, the port was made against {DECOMP_COMMIT[:7]} (see DECOMP_COMMIT)")
    if clean and OUT.exists():
        shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True, exist_ok=True)
    apply_patches()

    srcs = game_sources()
    # a stage's files are renamed together, so when one changes both rebuild
    stale_stages = set()
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        results = list(ex.map(lambda s: compile_game(s, clean), srcs))
    for rel, obj, err, built in results:
        m = STAGE.match(rel.name)
        if built and m:
            stale_stages.add(m.group(1))
    redo = [s for s in srcs if (m := STAGE.match(s.name)) and m.group(1) in stale_stages]
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        again = {r[0]: r for r in ex.map(lambda s: compile_game(s, True), redo)}
    results = [again.get(r[0], r) for r in results]
    prefix_stages([obj for rel, obj, err, built in results if obj is not None and STAGE.match(rel.name) and STAGE.match(rel.name).group(1) in stale_stages])

    failed = [(rel, err) for rel, obj, err, _ in results if obj is None]
    for rel, err in failed:
        print(f"FAILED {rel}\n{err[:2000]}")
    if failed:
        sys.exit(f"{len(failed)} game files failed")

    platform = []
    for src in sorted((NATIVE / "platform").glob("*.c")):
        obj = OUT / "platform" / src.with_suffix(".o").name
        obj.parent.mkdir(parents=True, exist_ok=True)
        if clean or not obj.exists() or obj.stat().st_mtime < max(src.stat().st_mtime, *(h.stat().st_mtime for h in (NATIVE / "platform").glob("*.h"))):
            r = run([CC, *PLATFORM_CFLAGS, src, "-o", obj])
            if r.returncode != 0:
                sys.exit(f"FAILED {src.name}\n{r.stderr}")
            if r.stderr:
                print(r.stderr)
        platform.append(obj)

    # the stages' setups, by name, for --dump-stages (platform/stagedump.c)
    stage_list = OUT / "stage_list.c"
    names = sorted(p.stem for p in (OUT / "obj").glob("wstag*.syms") if "_setupStage " in p.read_text())
    with open(stage_list, "w") as f:
        f.writelines(f"void {n.upper()}_setupStage(void); void {n.upper()}_startStage(void);\n" for n in names)
        f.write("struct { const char *name; void (*setup)(void); void *start; } native_stage_list[] = {\n")
        f.writelines(f'    {{"{n}", {n.upper()}_setupStage, (void *){n.upper()}_startStage}},\n' for n in names)
        f.write("    {0, 0, 0},\n};\n")
    stage_list_obj = OUT / "stage_list.o"
    r = run([CC, *PLATFORM_CFLAGS, stage_list, "-o", stage_list_obj])
    if r.returncode != 0:
        sys.exit(r.stderr)
    platform.append(stage_list_obj)

    game_objs = [obj for _, obj, _, _ in results]
    defined, undefined = set(), collections.defaultdict(int)
    with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
        syms = list(ex.map(symbols, game_objs + platform))
    for d, u in syms:
        defined |= d
        for s in u:
            undefined[s] += 1
    host = set(run([NM, "--format=just-symbols", "--defined-only", "--extern-only", *TC.parent.glob("i686-w64-mingw32/lib/libmsvcrt.a")]).stdout.split())
    host = {cname(s) for s in host}
    missing = sorted(s for s in undefined if s not in defined and s not in host
                     and not s.startswith(("SDL_", "_")))  # _x: the C runtime's and compiler's

    stubs = OUT / "stubs.c"
    with open(stubs, "w") as f:
        f.write('#include <stdio.h>\nvoid log_once(const char *what);\n')
        for n in missing:
            f.write(f'int {n}(void) {{ log_once("stub: {n}"); return 0; }}\n')
    stub_obj = OUT / "stubs.o"
    r = run([CC, *PLATFORM_CFLAGS, stubs, "-o", stub_obj])
    if r.returncode != 0:
        sys.exit(r.stderr)

    rsp = OUT / "objs.rsp"
    rsp.write_text("\n".join('"%s"' % str(o).replace("\\", "/") for o in game_objs + platform + [stub_obj]))
    exe = OUT / (sys.argv[sys.argv.index("--exe") + 1] if "--exe" in sys.argv else "dw2003.exe")
    r = run([CC, f"@{rsp}", "-o", exe, *LDFLAGS])
    if r.returncode != 0:
        sys.exit(f"link failed:\n{r.stderr[:4000]}")
    # the game's own tables out of the exe, read back from the player's disc (platform/fill.c)
    sys.path.insert(0, str(NATIVE / "tools"))
    import asset_scan
    stripped = asset_scan.strip(exe)
    dll = OUT / "SDL3.dll"
    if not dll.exists() or dll.read_bytes() != (SDL / "bin/SDL3.dll").read_bytes():  # a running game locks it
        shutil.copy2(SDL / "bin/SDL3.dll", dll)

    with open(OUT / "report.md", "w", encoding="utf-8") as f:
        f.write(f"# Native build\n\n{len(srcs)} game files, {len(platform)} platform files, "
                f"{len(missing)} PsyQ functions still stubbed\n\n## Stubbed\n\n")
        f.writelines(f"- {n}\n" for n in missing)
    print(f"OK {exe} ({len(missing)} stubs left, see build/report.md; {stripped} bytes of the game's tables "
          f"left to the player's disc)")


def selftest():
    ll = ("define dso_local zeroext i8 @f(ptr noundef %0) #0 !dbg !5 {\n  ret i8 -1, !dbg !9\n}\n"
          "define dso_local signext i16 @g() #0 {\n  ret i16 %3\n}\n"
          "define dso_local i32 @h() #0 {\n  ret i32 0\n}\n")
    want = ("define dso_local i32 @f(ptr noundef %0) #0 !dbg !5 {\n  %__ret1 = zext i8 -1 to i32\n"
            "  ret i32 %__ret1, !dbg !9\n}\ndefine dso_local i32 @g() #0 {\n  %__ret2 = sext i16 %3 to i32\n"
            "  ret i32 %__ret2\n}\ndefine dso_local i32 @h() #0 {\n  ret i32 0\n}\n")
    assert widen_returns(ll) == want, widen_returns(ll)
    ll = "define dso_local i32 @k(i8 noundef zeroext %0, i16 noundef signext %1) #0 {\n  ret i32 0\n}\n"
    want = "define dso_local i32 @k(i8 noundef %0, i16 noundef %1) #0 {\n  ret i32 0\n}\n"
    assert widen_returns(ll) == want, widen_returns(ll)
    ll = ("@a = dso_local global i32 1, align 4, !dbg !0\n@b = internal global [2 x i8] zeroinitializer, align 1\n"
          "@c = dso_local constant i32 2, align 4\n@d = external dso_local global i32, align 4\n"
          "@e = common dso_local global [2 x i32] zeroinitializer, align 4\n")
    want = ('@a = dso_local global i32 1, section ".data$dw3m1", align 4, !dbg !0\n'
            '@b = internal global [2 x i8] zeroinitializer, section ".data$dw3m1", align 1\n'
            "@c = dso_local constant i32 2, align 4\n@d = external dso_local global i32, align 4\n"
            '@e = dso_local global [2 x i32] zeroinitializer, section ".data$dw3m1", align 4\n')
    assert overlay_sections(ll, "m") == want, overlay_sections(ll, "m")
    ll = "  %5 = srem i32 %3, %4, !dbg !7\n  %6 = sdiv i32 %3, 10\n  %7 = udiv exact i32 %3, 0\n"
    want = ("  %5 = call i32 @native_srem(i32 %3, i32 %4), !dbg !7\n  %6 = sdiv i32 %3, 10\n"
            "  %7 = call i32 @native_udiv(i32 %3, i32 0)\ndeclare i32 @native_srem(i32, i32)\n"
            "declare i32 @native_udiv(i32, i32)\n")
    assert mips_divisions(ll) == want, mips_divisions(ll)
    import tempfile
    d = Path(tempfile.mkdtemp()) / "t.d"
    d.write_text("F:\\a\\b\\ c\\x.ll: F:\\a\\b\\ c\\x.c \\\n  F:\\inc\\game.h F:\\inc\\heap\\ h.h\n")
    assert [str(p) for p in deps(d)] == ["F:\\a\\b c\\x.c", "F:\\inc\\game.h", "F:\\inc\\heap h.h"], deps(d)
    print("selftest OK")


if __name__ == "__main__":
    selftest() if "--selftest" in sys.argv else main()
