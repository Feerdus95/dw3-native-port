"""Offline model of equipment in battle: expected values from the game's formulas, no battle is run.

Reads docs/data/equip.json and docs/data/tables.json (tools/dump_equip.py, tools/dump_tables.py) and, for enemy stats,
the battle table of the player's own disc (native/data/files/0463.bin, FILE_BATTLE_TABLE EU). Nothing is written.
Usage:  python tools/equip_model.py selftest | report [partner] | tech <partner> <level> <enemy#> <item ids...> [--form N] [--tech N]
        --form N: active Digimon = evolution N (DIGIMON_DATA[N-1]); --tech N: a physical technique id (default: its skills[0])
Model version: 2 (2 adds --form: the form's battleStats and basic attack, and --tech). Formulas: dw3_decomp/src/battle/battle_mode/{damage,rolls,stats}.c and engine/game/stats.c, documented in
docs/equipment-balance-baseline.md. It is an ESTIMATE under stated assumptions, not the game's real time-to-kill.

What it models: a partner's expected stats at a level (initial + mean level-up gains, no training), equipment totals
(computeStats), the partner's basic attack against one enemy: hit chance, critical chance, exact damage over every critical
roll, Triple (3 hits of 0.4x), element / family / damage-cut terms, and the enemy's first technique against the partner.
What it does NOT model: turn order and speed ties, the enemy's AI, the player's magic techniques, MP, statuses and weapon effects
(poison, drain, KO...), charge (damageBonus), the battle's element boost, boosts/lowered stats, `special` fighters, the form's
resistances and family, learning (which techniques a form has at which level), training, multi-enemy battles, the final
battle's fixed odds. A fighter's `special` flag is taken as 0. Which technique the player or the AI picks is NOT modelled:
the caller names it (--tech) or gets the active Digimon's basic attack.
"""
import json
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, "docs", "data")
TABLE = os.path.join(ROOT, "native", "data", "files", "0463.bin")


def cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def load():
    eq = json.load(open(os.path.join(DOCS, "equip.json"), encoding="utf-8"))
    tb = json.load(open(os.path.join(DOCS, "tables.json"), encoding="utf-8"))
    items = {}
    for kind in ("weapons", "armors", "accessories"):
        for e in eq[kind]:
            items[e["id"]] = dict(e, cls=kind)
    btab = {}
    with open(TABLE, "rb") as f:
        raw = f.read()
    for i in range(len(raw) // 0x46):
        e = raw[i * 0x46:(i + 1) * 0x46]
        id_ = struct.unpack_from("<h", e, 0)[0]
        if id_ == 0:
            break
        btab[id_] = dict(id=id_, itemChance=struct.unpack_from("<h", e, 4)[0], techs=list(struct.unpack_from("<3h", e, 8)),
                         stats=list(struct.unpack_from("<5h", e, 0x0E)), resist=list(struct.unpack_from("<12h", e, 0x18)),
                         family=e[0x30])
    return items, tb, btab


def signed16(v):
    return v - 0x10000 if v >= 0x8000 else v


def partner_base(tb, partner, level):
    """Expected stats [STR, DEF, SPI, WIS, SPD, CHR] at `level` (initial + mean gain per level-up; training excluded)."""
    d = tb["digimon"][partner]
    v = [float(x) for x in d["battleStats"]]
    for lv in range(2, level + 1):
        tier = 0 if lv < 5 else 1 if lv < 20 else 2 if lv < 40 else 3 if lv < 60 else 4 if lv < 80 else 5
        for i in range(6):
            g = d["statGrowth"][i]
            row = tb["growth"]["statGains"][tier]
            v[i] = min(v[i] + sum(row[g:g + 5]) / 5.0, 999)
    return v


def equip_totals(items, tb, partner, base, equip):
    """engine/game/stats.c computeStats for equip[0..5]: returns the six battle stats (+ set bonus), pre-clamp to >= 1."""
    v = list(base)
    for it in equip:
        if not it:
            continue
        e = items[it]
        if e["cls"] == "weapons":
            v[0] = min(v[0] + e["atk"], 999)
        elif e["cls"] == "armors":
            v[1] = min(v[1] + e["def"], 999)
        bonuses = [(e["stat0"], e["amount0"]), (e["stat1"], e["amount1"])] if e["cls"] != "accessories" else [(e["stat"], e["amount"])]
        for stat, amt in bonuses:
            if stat == 7:
                v = [min(x + signed16(amt), 999) for x in v]
            elif 1 <= stat <= 6:
                v[stat - 1] = min(v[stat - 1] + signed16(amt), 999)
        v[5] = min(v[5] + e["charisma"], 999)
    v[0], v[1], v[4] = max(v[0], 0), max(v[1], 0), max(v[4], 0)
    sets = tb["equipSets"]
    if list(equip[:4]) == sets["items"][partner]:
        v = [x + b for x, b in zip(v, sets["bonuses"][partner])]
    return [max(int(x), 1) for x in v]


def battle_fields(items, equip):
    """battle_mode/stats.c: the BattleStats fields the model uses, from the six slots."""
    f = dict(accuracy=0, evasion=0, families=[], crit=0, triple=0, element=0, epower=0, cut=0)
    for it in equip[:4]:
        if it:
            e = items[it]
            if e["cls"] == "weapons":
                f["accuracy"] += e["accuracy"]
                if e["family"] >= 2:
                    f["families"].append(e["family"])
            else:
                f["evasion"] += e.get("evasion", 0)
    found = False
    for i, it in enumerate(equip[:4]):
        if it and i != 1:
            if it in (0x96, 0xBF):
                f["crit"] = items[it]["statusPower"]
            if it in (0x97, 0xD2, 0xB4, 0xC2, 0x6D, 0xBA, 0x5E, 0x93, 0xAD, 0x96, 0xBF):
                found = True
    for it in equip[4:6]:
        if not it:
            continue
        a = items[it]["amount"] if it in items and items[it]["cls"] == "accessories" else 0
        if not found:
            if it == 0x13C:
                f["triple"] = 1
            elif it == 0x13D:
                f["crit"] = a
        if 0x153 <= it <= 0x167:
            f["element"] = 2 + (it - 0x153) // 3
            f["epower"] = a
        elif 0x145 <= it <= 0x146:
            f["cut"] = a
        elif 0x14B <= it <= 0x14C:
            f["accuracy"] += a
        elif 0x14D <= it <= 0x14E:
            f["evasion"] += a
    return f


def prob(chance):
    return min(max(chance, 0), 128) / 128.0


def adjust(value, tech, user_f, target, crit_roll=None):
    """damage.c adjustDamage without the battle's element boost and charge. target: dict(family, resist[7], cut)."""
    r = value
    if tech["family"] >= 2:
        if tech["family"] == target["family"]:
            r += r // 2
    elif any(w == target["family"] for w in user_f["families"][:3]):
        r += r // 2
    if tech["element"] >= 2:
        r += cdiv(r * tech["elementPower"] * 2, target["resist"][tech["element"] - 2])
    elif tech["script"] not in (11, 12) and user_f["element"] != 0:
        r += cdiv(r * user_f["epower"] * 2, target["resist"][user_f["element"] - 2])
    if tech["effect"] < 2 and user_f["triple"] and tech["script"] not in (11, 12):
        r = r * 4 // 10
    if crit_roll is not None:
        r += r * ((crit_roll & 0x3F) + 0x20) // 64
    if target["cut"]:
        r = max(r - target["cut"], 0)
    r = min(r, value * 5)
    return 9999 if r >= 10000 else r


def player_attack(tech, atk, atk_f, atk_lvl, enemy, enemy_lvl):
    """Expected damage of one use of a physical technique by the player (rollHit / rollCritical / computeDamage)."""
    value = cdiv(tech["power"] * atk[0], enemy["stats"][1])
    diff = atk[4] + atk_f["accuracy"] - enemy["stats"][4]
    lvl = atk_lvl - enemy_lvl
    if tech["effect"] < 2:
        chance = tech["accuracy"] + cdiv(tech["accuracy"] * (cdiv(diff, 8) + (lvl - atk_f["crit"])), 128)
    else:
        chance = tech["accuracy"] + cdiv(tech["accuracy"] * (cdiv(diff, 8) + lvl), 128)
    p_hit = prob(chance)
    cc = 4
    if tech["effect"] < 2 and atk_f["crit"]:
        cc = atk_f["crit"] + 4
    if tech["family"] >= 2:
        cc += 0x3C if tech["family"] == enemy["family"] else 0
    elif any(w == enemy["family"] for w in atk_f["families"][:3]):
        cc += 0x10
    p_crit = prob(cc)
    t = dict(enemy, cut=0)
    plain = adjust(value, tech, atk_f, t)
    crit = sum(adjust(value, tech, atk_f, t, r) for r in range(64)) / 64.0
    per_hit = p_hit * ((1 - p_crit) * plain + p_crit * crit)
    hits = 3 if atk_f["triple"] and tech["effect"] < 2 else 1
    return dict(p_hit=p_hit, p_crit=p_crit, plain=plain, crit=crit, per_use=per_hit * hits, hits=hits)


def enemy_attack(tech, enemy, enemy_lvl, strength, dfn, dfn_f, lvl):
    """Expected damage of the enemy's technique against the partner (rollHit side 1, computeDamage / computeMagicDamage)."""
    es = [cdiv(s * strength, 16) for s in enemy["stats"]]
    if tech["icon"] == 3:  # magic: no accuracy from SPD/evasion, mean over the critical roll is ignored (documented)
        base = cdiv(tech["power"] * strength, 16)
        power = base * (es[2] * 50 // dfn[2] + 50) // 100
        power = min(max(power, base // 2), base * 2)
        return max(power - dfn_f["cut"], 0), prob(tech["accuracy"] + cdiv(tech["accuracy"] * (cdiv(es[3] - dfn[3], 8) + enemy_lvl - lvl), 128))
    value = cdiv(cdiv(tech["power"] * strength, 16) * es[0], dfn[1])
    diff = es[4] - dfn[4] - dfn_f["evasion"]
    chance = max(tech["accuracy"] + cdiv(tech["accuracy"] * (cdiv(diff, 8) + enemy_lvl - lvl), 128), 0x20)
    dmg = max(min(value, value * 5) - dfn_f["cut"], 0)
    return dmg, prob(chance)


def scenario(items, tb, btab, partner, level, enemy_idx, equip, form=None, tech_id=None):
    """form: evolution index+1 (DIGIMON_DATA[form-1]) of the active Digimon, None for the partner itself. Its battleStats[0..4]
    are added to the partner's totals (battle_mode/stats.c:72-76). The technique is tech_id, else the active Digimon's skills[0]
    (first_tech.c:13). Only physical player techniques are modelled."""
    e = tb["enemies"][enemy_idx]
    bt = btab[e["fighter"]]
    base = partner_base(tb, partner, level)
    atk = equip_totals(items, tb, partner, base, equip)
    if form is not None:  # added after computeStats' clamp to >= 1: the same unless a total was below 1
        atk = [max(a + b, 1) for a, b in zip(atk, tb["digimon"][form - 1]["battleStats"][:5])] + atk[5:]
    f = battle_fields(items, equip)
    owner = tb["digimon"][form - 1] if form is not None else tb["digimon"][partner]
    tech = tb["techs"][(tech_id or owner["skills"][0]) - 1]
    if tech["icon"] != 2:
        raise ValueError("technique %d is not physical: the player's magic damage is not modelled" % tech["id"])
    en = dict(stats=[cdiv(s * e["strength"], 16) for s in bt["stats"]], family=bt["family"], resist=bt["resist"][:7])
    enemy_raw = dict(stats=bt["stats"], family=bt["family"], resist=bt["resist"][:7])
    out = player_attack(tech, atk, f, level, dict(en, stats=en["stats"]), e["level"])
    out["ttk_turns"] = math.ceil(e["hp"] / out["per_use"]) if out["per_use"] > 0 else None
    et = tb["techs"][bt["techs"][0] - 1] if bt["techs"][0] > 0 else None
    out["stats"], out["enemy_hp"], out["enemy_level"] = atk, e["hp"], e["level"]
    if et:
        d, p = enemy_attack(et, enemy_raw, e["level"], e["strength"], atk, f, level)
        out["enemy_tech"], out["enemy_dmg"], out["enemy_p_hit"] = et["id"], d, p
        out["partner_hp"] = None
    return out


def selftest():
    assert cdiv(-7, 2) == -3 and cdiv(7, 2) == 3
    assert abs(sum(((r & 0x3F) + 0x20) / 64 for r in range(64)) / 64 - 0.9921875) < 1e-9  # mean critical bonus
    t = dict(family=1, element=1, elementPower=1, script=5, effect=1)
    noeq = dict(accuracy=0, evasion=0, families=[], crit=0, triple=0, element=0, epower=0, cut=0)
    tgt = dict(family=1, resist=[100] * 7, cut=0)
    assert adjust(120, t, noeq, tgt) == 120
    assert adjust(120, t, dict(noeq, triple=1), tgt) == 48  # Triple: 0.4x per hit
    assert adjust(100, t, dict(noeq, element=2, epower=24), dict(tgt, resist=[50] * 7)) == 196  # +24*2/50 = +96 %
    assert adjust(100, t, noeq, dict(tgt, cut=20)) == 80 and adjust(10, t, noeq, dict(tgt, cut=20)) == 0
    assert adjust(100, t, dict(noeq, families=[3]), dict(tgt, family=3)) == 150
    items, tb, btab = load()
    assert len(tb["techs"]) == 443 and len(items) == 268 and len(btab) == 193
    assert len(tb["evolutions"]) == 8 and all(len(x) == 44 for x in tb["evolutions"])
    # forms (v2): evo 9 = DIGIMON_DATA[8], basic attack technique 9 (power 100), stats added to the partner's
    assert tb["digimon"][8]["skills"][0] == 9 and tb["techs"][8]["power"] == 100 and tb["digimon"][0]["skills"][0] == 1
    cand = next(i for i, x in enumerate(tb["enemies"]) if x["fighter"] in btab and x["strength"] == 16 and x["hp"] > 0)
    a, b = scenario(items, tb, btab, 0, 12, cand, [0] * 6), scenario(items, tb, btab, 0, 12, cand, [0] * 6, form=9)
    assert [y - x for x, y in zip(a["stats"], b["stats"])][:5] == tb["digimon"][8]["battleStats"][:5]
    print("selftest ok")


def report(partner):
    items, tb, btab = load()
    names = ["partner %d" % partner]
    buckets = [(12, "early"), (32, "mid"), (52, "late")]
    cands = [(i, e) for i, e in enumerate(tb["enemies"]) if e["fighter"] in btab and e["strength"] == 16 and e["hp"] > 0]
    for lvl, tag in buckets:
        idx, e = min(cands, key=lambda c: abs(c[1]["level"] - lvl))
        print("\n== %s: enemy #%d fighter %d level %d hp %d (strength 16/16); %s at level %d ==" %
              (tag, idx, e["fighter"], e["level"], e["hp"], names[0], e["level"]))
        mask = 1 << partner
        weapons = [w for w in items.values() if w["cls"] == "weapons" and w["partners"] & mask and w["kind"] in (1, 3, 7)]
        rows = []
        bare = scenario(items, tb, btab, partner, e["level"], idx, [0] * 6)
        for w in weapons:
            r = scenario(items, tb, btab, partner, e["level"], idx, [0, 0, w["id"], 0, 0, 0])
            rows.append((r["per_use"], w["hex"], w["atk"], w["accuracy"], r["p_hit"], r["ttk_turns"]))
        rows.sort(reverse=True)
        print("  partner stats unarmed %s; enemy stats %s" % (bare["stats"], [cdiv(s * e["strength"], 16) for s in btab[e["fighter"]]["stats"]]))
        print("  unarmed: P(hit) %.2f, %.1f dmg/turn, %s turns" % (bare["p_hit"], bare["per_use"], bare["ttk_turns"]))
        print("  weapon   atk  acc  P(hit)  dmg/turn  x unarmed  turns")
        for per, hx, atk, acc, ph, tt in (rows[:4] + rows[-2:]):
            print("  %-7s %4d %4d  %5.2f  %8.1f  %8.2f  %s" % (hx, atk, acc, ph, per, per / bare["per_use"], tt))
        print("  (%d weapons usable; top 4 and bottom 2 by expected damage per turn; basic attack only)" % len(rows))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "selftest"
    if cmd == "selftest":
        selftest()
    elif cmd == "report":
        report(int(sys.argv[2]) if len(sys.argv) > 2 else 0)
    elif cmd == "tech":
        args, opt = sys.argv[2:], {"--form": None, "--tech": None}
        for k in opt:
            if k in args:
                i = args.index(k)
                opt[k] = int(args[i + 1])
                del args[i:i + 2]
        p, lv, en = int(args[0]), int(args[1]), int(args[2])
        ids = [int(x, 0) for x in args[3:]] + [0] * 6
        items, tb, btab = load()
        print(json.dumps(scenario(items, tb, btab, p, lv, en, ids[:6], opt["--form"], opt["--tech"]), indent=1))
    else:
        sys.exit(__doc__)


main()
