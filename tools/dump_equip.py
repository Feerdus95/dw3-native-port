"""Dumps the equipment tables of the decomp to JSON (read-only on the decomp).

Source: dw3_decomp/src/engine/game/items.c (WEAPON_DATA, ARMOR_DATA, ACCESSORY_DATA, joined by index to ITEMS).
Usage:  python tools/dump_equip.py [out.json] [items.c]
        out.json defaults to <workspace>/docs/data/equip.json, items.c to <workspace>/dw3_decomp/src/engine/game/items.c
Extractor version: 1

Fields: id/hex/index (ITEMS id, table index), price, sellPrice, type (ItemInfo.type), then the record's own fields as
named in include/engine/game_state.h (amounts are unsigned halfwords: 0xFFF9 = -7). Armour pad0/pad1 are the two
padding bytes at 0x0E; weapon fields are in struct order. Not extracted: ITEM_KIND, item names and texts, the set bonuses
(EQUIP_SETS), the enemy and technique tables. The parser reads the C source with regexes: a change in the layout of
items.c (one record per line, ITEMS entries as `/* 0xNN */ {{.x = &TABLE[i]}, price, sell, kind, type}`) breaks it
loudly (KeyError/AttributeError), it does not guess.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # native/tools -> workspace
SRC = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "dw3_decomp", "src", "engine", "game", "items.c")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "data", "equip.json")

KIND = {"EQUIP_KIND_RIGHT_HAND": 1, "EQUIP_KIND_LEFT_HAND": 2, "EQUIP_KIND_EITHER_HAND": 3, "EQUIP_KIND_HEAD": 4,
        "EQUIP_KIND_BODY": 5, "EQUIP_KIND_ACCESSORY": 6, "EQUIP_KIND_BOTH_HANDS": 7, "EQUIP_KIND_GROUP_ACCESSORY": 8}


def num(s):
    s = s.strip()
    return KIND[s] if s in KIND else int(s, 0)


def block(txt, name):
    m = re.search(r"\b%s\[\d+\] = \{\n(.*?)\n\};" % name, txt, re.S)
    rows = []
    for line in m.group(1).splitlines():
        line = line.strip().rstrip(",")
        if not line.startswith("{"):
            continue
        flat = re.sub(r"[{}]", " ", line)
        rows.append([num(x) for x in flat.split(",") if x.strip()])
    return rows


def main():
    with open(SRC, encoding="utf-8") as f:
        txt = f.read()
    W, A, C = block(txt, "WEAPON_DATA"), block(txt, "ARMOR_DATA"), block(txt, "ACCESSORY_DATA")
    out = {"weapons": [], "armors": [], "accessories": []}
    items = re.findall(r"/\* 0x([0-9A-F]+) \*/ \{\{\.(weapon|armor|acc) = &(\w+)\[(\d+)\]\}, (\d+), (\d+), (ITEM_KIND_\w+), (0x[0-9A-Fa-f]+|\d+)\}", txt)
    for hid, _, tbl, idx, price, sell, kind, typ in items:
        idx = int(idx)
        rec = {"id": int(hid, 16), "hex": "0x%X" % int(hid, 16), "index": idx, "price": int(price),
               "sellPrice": int(sell), "type": int(typ, 0)}
        if tbl == "WEAPON_DATA":
            rec.update(zip(["charisma", "kind", "group", "partners", "amount0", "amount1", "atk", "stat0", "stat1",
                            "accuracy", "hitEffect", "statusChance", "statusPower", "family"], W[idx]))
            out["weapons"].append(rec)
        elif tbl == "ARMOR_DATA":
            rec.update(zip(["charisma", "kind", "group", "partners", "amount0", "amount1", "stat0", "stat1", "def",
                            "pad0", "pad1", "evasion"], A[idx]))
            out["armors"].append(rec)
        else:
            rec.update(zip(["charisma", "kind", "group", "partners", "amount", "stat"], C[idx]))
            out["accessories"].append(rec)
    os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
    tmp = OUT + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    os.replace(tmp, OUT)
    print({k: len(v) for k, v in out.items()}, "rows parsed:", len(W), len(A), len(C), "->", OUT)


main()
