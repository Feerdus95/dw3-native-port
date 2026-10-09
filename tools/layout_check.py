"""Checks that the PC build lays the game's structs out as the PSX did: the
decomp's headers carry each field's MIPS offset as a /* 0xNN */ comment, and
every one becomes a _Static_assert(offsetof(...)) compiled with the game's
flags. A struct the PC packs differently (pointer or s64 alignment, padding)
fails with its name and field. Run from native/: python tools/layout_check.py"""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build  # noqa: E402

OPEN = re.compile(r"^\s*(?:/\*.*?\*/\s*)?(typedef\s+)?(struct|union)\s*(\w+)?\s*\{")
CLOSE = re.compile(r"^\s*\}\s*(\w+)?\s*(?:\[[^\]]*\])?\s*;")
FIELD = re.compile(r"^\s*/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*(.+?;)")
FUNC_PTR = re.compile(r"\(\s*\*\s*(\w+)\s*\)")
NAME = re.compile(r"(\w+)\s*(?:\[[^\]]*\]\s*)*;$")


def field_name(decl):
    """The name a declaration declares, or None (bit-fields, several names)"""
    decl = re.sub(r"/\*.*?\*/", "", decl).strip()
    if ":" in decl or "," in decl.split("(")[0]:
        return None
    m = FUNC_PTR.search(decl)
    if m:
        return m.group(1)
    m = NAME.search(decl)
    return m.group(1) if m else None


MACROS = {"VERSION_EU": 1, "VERSION_US": 0, "NATIVE": 1,
          "NAME_ENTRY_HAS_UNK98": 0, "NAME_ENTRY_HAS_HIDE": 0}  # the comments' offsets are for the overlays without it

# Structs whose comments give the US offsets from a field on, as the headers say:
# (type, first field) -> what the European version adds
EU_SHIFT = {("BattleFuncs", "testRunAway"): 4, ("MemCardSaves", "choice"): 8}


def eu_lines(text):
    """The lines an EU native build compiles: #if/#ifdef on the macros above,
    anything else taken as true"""
    def value(cond):
        cond = cond.strip()
        neg = cond.startswith("!")
        name = re.sub(r"defined\s*\(?\s*(\w+)\s*\)?", r"\1", cond.lstrip("!")).strip()
        v = MACROS.get(name, 1)
        return (not v) if neg else bool(v)

    stack = []  # [active, taken] per #if
    for line in text.splitlines():
        d = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
        if d:
            kw, cond = d.group(1), d.group(2)
            if kw in ("ifdef", "ifndef"):
                v = (cond.strip() in MACROS and MACROS[cond.strip()] != 0) == (kw == "ifdef")
                stack.append([v, v])
            elif kw == "if":
                v = value(cond)
                stack.append([v, v])
            elif kw == "elif" and stack:
                v = not stack[-1][1] and value(cond)
                stack[-1] = [v, stack[-1][1] or v]
            elif kw == "else" and stack:
                stack[-1] = [not stack[-1][1], True]
            elif kw == "endif" and stack:
                stack.pop()
            continue
        if all(a for a, _ in stack):
            yield line


def asserts(text):
    """(type, field, offset) of the fields at the top level of each struct"""
    out, stack = [], []  # stack entries: [kind, tag, fields] (None for nested)
    for line in eu_lines(text):
        m = OPEN.match(line)
        if m:
            stack.append([m.group(2), m.group(3), [], bool(m.group(1))])
            if "}" in line[m.end():]:  # a one-line struct
                stack.pop()
            continue
        m = CLOSE.match(line)
        if m and stack:
            kind, tag, fields, typedef = stack.pop()
            if stack or kind != "struct":
                continue  # nested ones are checked through their own typedefs
            name = m.group(1) if typedef and m.group(1) else (f"struct {tag}" if tag else None)
            if name:
                shift = 0
                for f, off in fields:
                    shift = EU_SHIFT.get((name, f), shift)
                    out.append((name, f, off + shift))
            continue
        m = FIELD.match(line)
        if m and len(stack) == 1:
            f = field_name(m.group(2))
            if f:
                stack[0][2].append((f, int(m.group(1), 16)))
    return out


def main():
    build.apply_patches()
    include = build.PATCHED / "include"
    headers = sorted(p for p in include.rglob("*.h"))
    out_dir = build.OUT / "layout"
    out_dir.mkdir(parents=True, exist_ok=True)
    total, bad, broken = 0, [], []
    for h in headers:
        checks = asserts(h.read_text(errors="replace"))
        if not checks:
            continue
        rel = h.relative_to(include).as_posix()
        src = out_dir / (rel.replace("/", "_") + ".c")
        src.write_text('#include <stddef.h>\n#include "game.h"\n'
                       f'#include "{rel}"\n'
                       + "".join(f'_Static_assert(__builtin_offsetof({t}, {f}) == {o:#x}, "{t}.{f} @ {o:#x}");\n'
                                 for t, f, o in checks))
        r = subprocess.run([build.CC, *build.GAME_CFLAGS, "-fsyntax-only", "-ferror-limit=0",
                            f"-I{include}", src], capture_output=True, text=True)
        total += len(checks)
        failed = re.findall(r'static assertion failed[^"]*"([^"]+)"', r.stderr)
        bad += [(rel, f) for f in failed]
        if r.returncode and not failed:
            broken.append((rel, r.stderr.strip().splitlines()[0] if r.stderr.strip() else "?"))
    print(f"{total} offsets checked in {len(headers)} headers")
    for rel, f in bad:
        print(f"MISMATCH {rel}: {f}")
    for rel, err in broken:
        print(f"NOT CHECKED {rel}: {err}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
