"""Reads the random-battle lists of the field stages in the decomp and says where each encounter index appears.

Only reading, validating and returning facts: no analysis of enemy behaviour (that is tools/enemy_actions.py, which consumes this).
Source (read-only): <stages dir> = dw3_decomp/src/field/stages, every **/wstag*.c. In each file:
  AreaBattle  name = { encounter, stage, MUSIC(..) };         one slot of a list (encounter 0 = empty slot)
  BattleList  name = { count, { &slot x8 } };                 one area's list
  FieldBattles stageBattlesN[] = { { serial, place, pad, { &list x4 } }, ... };   lists of the four areas 0-3 for each place
Usage:  python tools/battle_lists.py --stages-dir DIR [selftest | summary | <encounter>]
        python tools/battle_lists.py selftest          (synthetic files only)
The stages directory is always explicit: there is no default and no path is built in.
Anything the parser does not understand raises BattleListError with file:line; nothing is skipped silently.
Facts only: the slot weights (k/8) and the rates are not encounter probabilities and are not exposed as such.
Version: 1.
"""
import glob
import os
import re
import sys
import tempfile

N_AREAS, N_SLOTS = 4, 8
N_RATES = 6  # FIELDSTG_battleRates has 6 entries (fieldstg.c); BattleList.count indexes it
SERIAL_MAX = 448  # FieldBattles.serial: "1 to 448 over the stages" (field_map.h)
# Every declaration may be indented; a declaration that starts anywhere but matches the strict pattern is an error (_all_recognised).
_AB = re.compile(r"^[ \t]*AreaBattle[ \t]+(\w+)[ \t]*=[ \t]*\{([^{}]*)\}[ \t]*;", re.M)
_BL = re.compile(r"^[ \t]*BattleList[ \t]+(\w+)[ \t]*=\s*\{\s*([^,{}]+),\s*\{([^{}]*)\}\s*,?\s*\}\s*;", re.M)
_FB = re.compile(r"^[ \t]*FieldBattles[ \t]+(\w+)[ \t]*\[\s*\]\s*=\s*\{(.*?)^[ \t]*\}[ \t]*;", re.M | re.S)
_FB_ROW = re.compile(r"\{\s*([^,{}]+),\s*([^,{}]+),\s*([^,{}]+),\s*\{([^{}]*)\}\s*\}")
_REF = re.compile(r"&(\w+)")
_DECL = {"AreaBattle": re.compile(r"^[ \t]*AreaBattle\b(?![ \t]*\*)[^\n;]*=", re.M),
         "BattleList": re.compile(r"^[ \t]*BattleList\b(?![ \t]*\*)[^\n;]*=", re.M),
         "FieldBattles": re.compile(r"^[ \t]*FieldBattles\b(?![ \t]*\*)[^\n;]*=", re.M)}
_MUSIC = re.compile(r"MUSIC_\w+|MUSIC\(\s*\w+\s*,\s*\w+\s*\)")


class BattleListError(Exception):
    pass


def _line(text, pos):
    return text.count("\n", 0, pos) + 1


def _num(s, where):
    try:
        return int(s.strip(), 0)
    except ValueError:
        raise BattleListError("%s: %r is not a number" % (where, s.strip()))


def _all_recognised(text, path, kind, matches):
    """Every `<kind> ... =` declaration of the file must be one the strict pattern matched; otherwise stop at its line."""
    ok = {m.start() for m in matches}
    for d in _DECL[kind].finditer(text):
        if d.start() not in ok:
            raise BattleListError("%s:%d: a %s definition has a layout the parser does not understand" % (path, _line(text, d.start()), kind))


def _define(table, name, value, where, kind):
    if name in table:
        raise BattleListError("%s: %s %s is defined twice" % (where, kind, name))
    table[name] = value


def parse_text(text, path, n_encounters=None):
    """-> list of entries {variant, serial, place, lists: [4 lists of 8 encounter indices], line} for one file ([] if it has none).
    Every AreaBattle / BattleList / FieldBattles declaration, all its fields and all its references are validated."""
    slots, lists = {}, {}
    ab = list(_AB.finditer(text))
    _all_recognised(text, path, "AreaBattle", ab)
    for m in ab:
        where = "%s:%d" % (path, _line(text, m.start()))
        parts = [p.strip() for p in m.group(2).split(",", 2)]
        if len(parts) != 3:
            raise BattleListError("%s: AreaBattle %s does not have 3 fields" % (where, m.group(1)))
        enc = _num(parts[0], where)
        if _num(parts[1], where) < 0:
            raise BattleListError("%s: AreaBattle %s has a negative stage" % (where, m.group(1)))
        if not _MUSIC.fullmatch(parts[2]):
            raise BattleListError("%s: AreaBattle %s music %r is neither MUSIC_x nor MUSIC(a, b)" % (where, m.group(1), parts[2]))
        if enc < 0 or (n_encounters is not None and enc >= n_encounters):
            raise BattleListError("%s: encounter %d outside the table (0..%s)" % (where, enc, (n_encounters or 0) - 1))
        _define(slots, m.group(1), enc, where, "AreaBattle")
    bl = list(_BL.finditer(text))
    _all_recognised(text, path, "BattleList", bl)
    for m in bl:
        where = "%s:%d" % (path, _line(text, m.start()))
        count = _num(m.group(2), where)
        if not 0 <= count < N_RATES:
            raise BattleListError("%s: BattleList %s count %d is not an index of FIELDSTG_battleRates (0..%d)" % (where, m.group(1), count, N_RATES - 1))
        refs = _REF.findall(m.group(3))
        if len(refs) != N_SLOTS or re.sub(r"[\s,]", "", _REF.sub("", m.group(3))):
            raise BattleListError("%s: BattleList %s must hold exactly %d '&slot' entries, found %d" % (where, m.group(1), N_SLOTS, len(refs)))
        for r in refs:
            if r not in slots:
                raise BattleListError("%s: BattleList %s refers to unknown AreaBattle %s" % (where, m.group(1), r))
        _define(lists, m.group(1), [slots[r] for r in refs], where, "BattleList")
    fb = list(_FB.finditer(text))
    _all_recognised(text, path, "FieldBattles", fb)
    out = []
    for m in fb:
        base = _line(text, m.start(2))
        rows = list(_FB_ROW.finditer(m.group(2)))
        if not rows:
            raise BattleListError("%s:%d: FieldBattles %s has no entries" % (path, base, m.group(1)))
        if re.sub(r"[\s,]", "", _FB_ROW.sub("", m.group(2))):
            raise BattleListError("%s:%d: FieldBattles %s has text the parser does not understand" % (path, base, m.group(1)))
        for r in rows:
            line = base + m.group(2).count("\n", 0, r.start())
            where = "%s:%d" % (path, line)
            serial, place, pad = _num(r.group(1), where), _num(r.group(2), where), _num(r.group(3), where)
            if not 1 <= serial <= SERIAL_MAX:
                raise BattleListError("%s: FieldBattles serial %d outside 1..%d" % (where, serial, SERIAL_MAX))
            if place < 0:
                raise BattleListError("%s: FieldBattles place %d is negative" % (where, place))
            if pad != 0:
                raise BattleListError("%s: FieldBattles third field is %d, 0 in every list of the game" % (where, pad))
            refs = _REF.findall(r.group(4))
            if len(refs) != N_AREAS or re.sub(r"[\s,]", "", _REF.sub("", r.group(4))):
                raise BattleListError("%s: FieldBattles entry must hold exactly %d '&list' entries, found %d" % (where, N_AREAS, len(refs)))
            for x in refs:
                if x not in lists:
                    raise BattleListError("%s: FieldBattles entry refers to unknown BattleList %s" % (where, x))
            out.append(dict(variant=m.group(1), serial=serial, place=place, lists=[lists[x] for x in refs], line=line))
    return out


def scan(stages_dir, n_encounters=None):
    """-> dict stage number -> entries of parse_text, for every wstag*.c under stages_dir that defines lists."""
    if not stages_dir or not os.path.isdir(stages_dir):
        raise BattleListError("the stages directory %r does not exist; pass --stages-dir (dw3_decomp/src/field/stages)" % (stages_dir,))
    files = sorted(glob.glob(os.path.join(stages_dir, "**", "wstag*.c"), recursive=True))
    if not files:
        raise BattleListError("no wstag*.c under %s" % stages_dir)
    out = {}
    for f in files:
        m = re.search(r"wstag(\d+)\.c$", f)
        with open(f, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        ents = parse_text(text, f, n_encounters)
        if m and ents:
            out[int(m.group(1))] = ents
    return out


def appearances(stages):
    """-> dict encounter index (>0) -> list of (stage, variant, place, area, slot). Pure facts, no weights."""
    ap = {}
    for st, ents in stages.items():
        for e in ents:
            for area, lst in enumerate(e["lists"]):
                for slot, enc in enumerate(lst):
                    if enc:
                        ap.setdefault(enc, []).append((st, e["variant"], e["place"], area, slot))
    return ap


def coverage(apps):
    """Label of one encounter from its appearances: 'random' (some list of areas 0-2), 'area3-unresolved' (only area 3), 'unreferenced'."""
    if any(a[3] < 3 for a in apps):
        return "random"
    return "area3-unresolved" if apps else "unreferenced"


def selftest(stages_dir=None):
    ab = "".join("AreaBattle a%dB%d = { %d, 7, MUSIC(1, 0) };\n" % (a, s, 5 if (a, s) == (0, 0) else 9 if a == 3 and s == 0 else 0)
                 for a in range(4) for s in range(8))
    bl = "".join("BattleList area%dBattles = {\n    0,\n    { %s },\n};\n" % (a, ", ".join("&a%dB%d" % (a, s) for s in range(8))) for a in range(4))
    fb = "FieldBattles stageBattles[] = {\n    { 1, 2, 0, { &area0Battles, &area1Battles, &area2Battles, &area3Battles } },\n};\n"
    good = ab + bl + fb
    ents = parse_text(good, "t.c", 20)
    assert len(ents) == 1 and ents[0]["place"] == 2 and ents[0]["lists"][0][0] == 5 and ents[0]["lists"][3][0] == 9 and ents[0]["line"] == 50
    ap = appearances({7: ents})
    assert coverage(ap[5]) == "random" and coverage(ap[9]) == "area3-unresolved" and coverage([]) == "unreferenced"
    assert ap[9] == [(7, "stageBattles", 2, 3, 0)]
    assert parse_text("int x;\n", "t.c") == []

    def fails(text, frag, n=20):
        try:
            parse_text(text, "t.c", n)
        except BattleListError as e:
            assert frag in str(e), str(e)
            return
        raise AssertionError("no error for " + frag)
    def at(text, frag, line, n=20):  # the message names the file AND the exact line
        fails(text, frag, n)
        try:
            parse_text(text, "t.c", n)
        except BattleListError as e:
            assert str(e).startswith("t.c:%d:" % line), (line, str(e))
    at(good.replace("&a0B7", "&nope"), "unknown AreaBattle", 33)
    at(good.replace("&area3Battles", "&area9"), "unknown BattleList", 50)
    at(good.replace(", &area3Battles", ""), "exactly 4 '&list' entries, found 3", 50)
    at(good.replace("&a1B3, ", ""), "exactly 8 '&slot' entries, found 7", 37)
    at(good.replace("{ 5, 7,", "{ 25, 7,"), "outside the table", 1)
    at(good.replace("{ 5, 7,", "{ x, 7,"), "not a number", 1)
    at(good.replace("{ 5, 7, MUSIC(1, 0)", "{ 5, -7, MUSIC(1, 0)"), "negative stage", 1)
    at(good.replace("{ 5, 7, MUSIC(1, 0)", "{ 5, 7, nothing"), "neither MUSIC_x nor MUSIC(a, b)", 1)
    at(good.replace("area0Battles = {\n    0,", "area0Battles = {\n    9,"), "not an index of FIELDSTG_battleRates", 33)
    at(good.replace("area1Battles = {\n    0,", "area1Battles = {\n    x,"), "not a number", 37)
    at(good.replace("area1Battles = {\n    0,", "area1Battles = {\n    -1,"), "not an index of FIELDSTG_battleRates", 37)
    at(good.replace("{ 1, 2, 0, {", "{ 1, 2, 5, {"), "0 in every list", 50)
    at(good.replace("{ 1, 2, 0, {", "{ 0, 2, 0, {"), "serial 0 outside", 50)
    at(good.replace("{ 1, 2, 0, {", "{ 449, 2, 0, {"), "serial 449 outside", 50)
    at(good.replace("{ 1, 2, 0, {", "{ 1, 2, z, {"), "not a number", 50)
    at(good + "AreaBattle a0B0 = { 1, 7, MUSIC(1, 0) };\n", "defined twice", 52)
    at(good + "AreaBattle weird = {\n 1 };\n", "3 fields", 52)
    at(good + "AreaBattle weird = other;\n", "layout", 52)
    at(good + "  AreaBattle weird = other;\n", "layout", 52)          # indented declarations are looked at too
    at(good + "	BattleList weird = { 0 };\n", "layout", 52)
    at(good + "FieldBattles broken[] = {\n    { 1, 2, 0, { &area0Battles } }\n", "layout", 52)  # no closing '};'
    at(good.replace("{ 1, 2, 0, {", "{ 1, 2, {"), "no entries", 49)
    at(good.replace("} },\n};", "} }, junk\n};"), "understand", 49)
    assert len(parse_text("  " + good.replace("\n", "\n  "), "t.c", 20)) == 1  # indentation alone is not a problem
    assert parse_text("extern FieldBattles stageBattles0[];\nextern AreaBattle *p;\n", "t.c") == []

    for bad in (None, os.path.join(tempfile.gettempdir(), "no-such-stages-dir-xyz")):
        try:
            scan(bad)
        except BattleListError as e:
            assert "--stages-dir" in str(e)
        else:
            raise AssertionError("scan accepted a missing directory")
    with tempfile.TemporaryDirectory() as d:  # the file name and line travel in the error
        os.makedirs(os.path.join(d, "x"))
        with open(os.path.join(d, "x", "wstag9.c"), "w", encoding="utf-8") as fh:
            fh.write(good.replace("&a0B7", "&nope"))
        try:
            scan(d)
        except BattleListError as e:
            assert "wstag9.c:" in str(e)
        else:
            raise AssertionError("scan hid a broken file")
        with open(os.path.join(d, "x", "wstag9.c"), "w", encoding="utf-8") as fh:
            fh.write(good)
        assert list(scan(d, 20)) == [9]
    if stages_dir:  # the figures recorded in docs/battle-context-research.md section 7
        st = scan(stages_dir, 335)
        ap = appearances(st)
        assert len(st) == 203, len(st)
        assert sum(len(e) for v in st.values() for e in v) > 0
        assert sum(len(e["lists"]) * N_SLOTS for v in st.values() for e in v) == 14336
        assert set(range(1, 335)) - set(ap) == {292, 294}, sorted(set(range(1, 335)) - set(ap))
        print("real stages: %d stages, %d referenced encounters, unreferenced %s" %
              (len(st), len(ap), sorted(e for e in range(1, 335) if e not in ap)))
    print("selftest ok" + ("" if stages_dir else " (synthetic files only; pass --stages-dir for the real directory)"))


def main(argv):
    args = list(argv)
    sd = None
    if "--stages-dir" in args:
        i = args.index("--stages-dir")
        if i + 1 >= len(args):
            sys.exit("--stages-dir needs a directory")
        sd = args[i + 1]
        del args[i:i + 2]
    cmd = args[0] if args else "selftest"
    if cmd == "selftest":
        return selftest(sd)
    st = scan(sd, 335)
    ap = appearances(st)
    if cmd == "summary":
        labels = {}
        for e in range(1, 335):
            labels.setdefault(coverage(ap.get(e, [])), []).append(e)
        print("%d stages with lists, %d encounters referenced" % (len(st), len(ap)))
        for k, v in sorted(labels.items()):
            print("  %-17s %d" % (k, len(v)))
    else:
        e = int(cmd)
        print("encounter %d: %s" % (e, coverage(ap.get(e, []))))
        for a in ap.get(e, []):
            print("  stage %d %s place %d area %d slot %d" % a)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except BattleListError as e:
        sys.exit("battle_lists: %s" % e)
