"""Dumps the combat tables of the decomp (EU build) to JSON: techniques, Digimon, field enemies, encounters, growth.

Sources (read-only), all under dw3_decomp/src: engine/game/items.c (TECHS), engine/game/digimon.c (DIGIMON_DATA),
field/field_mode/data/fieldstg.c (FIELDSTG_enemies, FIELDSTG_encounters), battle/report/data/stfgtrep.c (growth tables).
Also reads engine/game/stats.c (EQUIP_SETS, EQUIP_SET_BONUSES).
Usage:  python tools/dump_tables.py [out.json] [src_dir]
        out.json defaults to <workspace>/docs/data/tables.json, src_dir to <workspace>/dw3_decomp/src
Extractor version: 2 (2 adds `evolutions`: STFGTREP_evolutionLists, the Digimon each partner can learn and their needs)

`#if VERSION_US` branches are dropped and the EU/JP ones kept (the port builds the EU game). Each array is parsed as a C
initializer (nested braces of numbers); a layout it does not understand raises. Field names follow include/engine/game_state.h
(TechData, DigimonData) and battle.h (BattleEnemy = fighter, level, hp, mp, strength). Not extracted: the battle table
(FILE_BATTLE_TABLE, a disc file read by tools/equip_model.py), scripts, texts and the stage overlays' own encounters.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "data", "tables.json")
SRC = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "dw3_decomp", "src")


def read(rel):
    with open(os.path.join(SRC, rel), encoding="utf-8") as f:
        return f.read()


def eu_only(txt):
    """Keeps the EU side of `#if VERSION_US ... #elif VERSION_EU || VERSION_JP ... #endif` blocks."""
    out, stack = [], []  # stack of [active, taken]
    for line in txt.split("\n"):
        s = line.strip()
        if s.startswith("#if"):
            cond = "VERSION_US" not in s or "VERSION_EU" in s
            stack.append([cond, cond])
            continue
        if s.startswith("#elif"):
            top = stack[-1]
            cond = ("VERSION_EU" in s or "VERSION_JP" in s) and not top[1]
            top[0] = cond
            top[1] = top[1] or cond
            continue
        if s.startswith("#else"):
            top = stack[-1]
            top[0] = not top[1]
            continue
        if s.startswith("#endif"):
            stack.pop()
            continue
        if all(t[0] for t in stack):
            out.append(line)
    return "\n".join(out)


TOKEN = re.compile(r"/\*.*?\*/|//[^\n]*|&\w+\[(\d+)\]|-?0x[0-9A-Fa-f]+|-?\d+|[{},]|\w+", re.S)


def parse_init(txt, name):
    """The nested list of numbers of `name[...] = { ... };`; `&X[n]` becomes n, EQUIP_KIND-style words raise."""
    m = re.search(r"\b%s(\[[^\]]*\])+ = \{" % re.escape(name), txt)
    if m is None:
        raise KeyError(name)
    stack, cur = [], None
    for t in TOKEN.finditer(txt, m.end() - 1):
        s = t.group(0)
        if s.startswith("/*") or s.startswith("//") or s == ",":
            continue
        if s == "{":
            new = []
            if cur is not None:
                cur.append(new)
                stack.append(cur)
            cur = new
        elif s == "}":
            if not stack:
                return cur
            cur = stack.pop()
        elif t.group(1) is not None:
            cur.append(int(t.group(1)))
        else:
            cur.append(int(s, 0))
    raise ValueError("unterminated " + name)


def named(rows, fields, first_id=0):
    return [dict(id=i + first_id, **dict(zip(fields, r))) for i, r in enumerate(rows)]


def main():
    items = eu_only(read("engine/game/items.c"))
    digi = eu_only(read("engine/game/digimon.c"))
    fstg = eu_only(read("field/field_mode/data/fieldstg.c"))
    grow = eu_only(read("battle/report/data/stfgtrep.c"))

    techs = named(parse_init(items, "TECHS"),
                  ["mp", "power", "icon", "kind", "accuracy", "element", "elementPower", "family", "effect",
                   "effectChance", "effectPower", "scriptStage", "scriptEffect", "scriptSound", "script", "hitCount"], 1)
    digimon = []
    for i, r in enumerate(parse_init(digi, "DIGIMON_DATA")):
        (id_, battle, resist, skills, pair, sres, slv, klv, expl, pp, exprate, hp, mp, hpg, mpg, sg, rg, blast,
         nameid, fam) = r
        digimon.append(dict(index=i, id=id_, battleStats=battle, resistances=resist, skills=skills, pairTech=pair,
                            statusResists=sres, skillLevels=slv, knownLevels=klv, expLevel=expl, pairPartner=pp,
                            expRate=exprate, hp=hp, mp=mp, hpGrowth=hpg, mpGrowth=mpg, statGrowth=sg,
                            resistGrowth=rg, blastForms=blast, nameId=nameid, family=fam))
    enemies = named(parse_init(fstg, "FIELDSTG_enemies"), ["fighter", "level", "hp", "mp", "strength"])
    encounters = named(parse_init(fstg, "FIELDSTG_encounters"), ["enemies", "ambushChance", "kind", "blocks"])
    for e in encounters:
        e["enemies"] = [x if isinstance(x, int) else x[0] for x in e["enemies"]]
    growth = {"levelExp": parse_init(grow, "STFGTREP_levelExp"), "vitalCuts": parse_init(grow, "STFGTREP_vitalCuts"),
              "vitalRandom": parse_init(grow, "STFGTREP_vitalRandom"), "statGains": parse_init(grow, "STFGTREP_statGains"),
              "resistGains": parse_init(grow, "STFGTREP_resistGains")}
    stats_c = eu_only(read("engine/game/stats.c"))
    sets = {"items": [s[0] for s in parse_init(stats_c, "EQUIP_SETS")], "bonuses": parse_init(stats_c, "EQUIP_SET_BONUSES")}
    # one list of 44 per partner: {digimon (DIGIMON_DATA index + 1), needs [[digimon, level] x2], stat, value}
    evolutions = [[dict(digimon=e[0], needs=e[1], stat=e[2], value=e[3]) for e in lst]
                  for lst in parse_init(grow, "STFGTREP_evolutionLists")]
    out = {"techs": techs, "digimon": digimon, "enemies": enemies, "encounters": encounters, "growth": growth,
           "equipSets": sets, "evolutions": evolutions}
    os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
    tmp = OUT + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    os.replace(tmp, OUT)
    print({k: len(v) for k, v in out.items()}, "->", OUT)


main()
