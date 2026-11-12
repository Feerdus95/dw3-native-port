"""Offline analysis of the enemies' action choice (battle table entries of the player's own disc, native/data/files/0463.bin).

No battle is run. It evaluates dw3_decomp/src/battle/battle_mode/enemy_turn.c (the choice at :223-231, the conditions at :303-433, the
targets at :438-470, the MP of targets 2/3 at :117-136): actions[0..2] in order, the first whose condition holds wins, actions[3] is the
fallback and its condition is never tested. The result is a DISTRIBUTION over outcomes for a stated combat state.
Usage:  python tools/enemy_actions.py selftest | summary | rng-only | <fighter id> [--hp PCT] [--mp N] [--php PCT] [--turn N] [--base 0|1]
        [--asleep 0|1] [--allies ID,ID|any|none] [--battle N] [--kind N] [--elem N] [--charge 0|1] [--atk-down 0|1] [--def-down 0|1] [--spd-down 0|1]
Variables left out are UNKNOWN. They are not enumerated value by value: each one is split only at the values its conditions test
(HP / MP buckets between thresholds, turn residues modulo the lcm of the tested moduli, booleans, tested values + "other"), and the
equal distributions are merged. The output lists every distinct distribution with the scenarios that produce it; nothing is averaged.
Labels: [C] read from the code, [D] table data, [H] hypothesis. Hypotheses that every probability here depends on:
 [H] Each RANDOM.next() % 128 draw that is reached is conditionally uniform given the previous draws and their outcomes (the generator was
     not analysed; marginal uniformity alone would not justify chaining): condition 1 holds with probability arg/128 and a chain of them
     multiplies, (1 - p1) * p2 ...
 [H] `turns` counts the enemy's own decisions starting from some unknown value (every residue is considered possible).
 [H] HP percentages assume maxHp is a multiple of 100 (the game uses (s16)(maxHp / 100) * (arg * 100 / 128)); the exact HP of a given
     enemy can sit on a different side of a threshold by rounding.
The scenarios listed are combinations COMPATIBLE with the conditions, not necessarily states reachable in a real battle.
Not modelled: the final battle (enemies 2 and 3 use other routes), the counterattack (`counter`), sleep / paralysis / confusion that make the
enemy lose its turn before choosing, the flee rule of ESCAPE battles, and whether a "switch to enemy n" target has a valid enemy n.
Model version: 1.
"""
import math
import os
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import equip_model as em  # noqa: E402

COND_NAMES = {0: "always", 1: "chance", 2: "hp<", 3: "hp>=", 4: "mp<", 5: "mp>=", 6: "player hp<", 7: "player hp>=", 8: "player base form",
              9: "player asleep", 10: "other enemy alive", 11: "battle element", 12: "battle number", 13: "encounter kind", 14: "charge",
              15: "atk down", 16: "def down", 17: "spd down", 18: "turn % n == 0"}
FLAG_VARS = {8: "base", 9: "asleep", 14: "charge", 15: "atk_down", 16: "def_down", 17: "spd_down"}
VALUE_VARS = {11: "elem", 12: "battle", 13: "kind"}
HP_COND = {2: ("hp", False), 3: ("hp", True), 6: ("php", False), 7: ("php", True)}


def pct(arg):
    return arg * 100 // 128  # C: arg is an s16, percent an s32


def entry_actions(entry):
    return [tuple(a) for a in entry["actions"]]


class Buckets:
    """The values a numeric variable can take, split at the thresholds its conditions test: k = how many thresholds the value reaches."""
    def __init__(self, ts, unit):
        self.ts, self.unit = sorted(set(ts)), unit

    def options(self):
        out = []
        for k in range(len(self.ts) + 1):
            lo = ("%s%s" % (self.ts[k - 1], self.unit)) if k else None
            hi = ("%s%s" % (self.ts[k], self.unit)) if k < len(self.ts) else None
            out.append((("[%s, %s)" % (lo or "0", hi or "inf")), k))
        return out

    def reaches(self, k, t):  # value >= t
        return self.ts.index(t) < k


def variables(entry, techs):
    """(v, bk): v = name -> list of (label, value) for the state variables this entry's conditions (and the MP of its targets 2/3) test;
    bk = name -> Buckets for the numeric ones."""
    acts = entry_actions(entry)
    thr, moduli, allies, vals, flags = {"hp": [], "php": [], "mp": []}, [], [], {"elem": [], "battle": [], "kind": []}, set()
    for t, c, a in acts[:3]:
        if c in HP_COND:
            if pct(a) > 0:
                thr[HP_COND[c][0]].append(pct(a))
        elif c in (4, 5) and a > 0:
            thr["mp"].append(a)
        elif c == 10:
            allies.append(a)
        elif c in VALUE_VARS:
            vals[VALUE_VARS[c]].append(a)
        elif c in FLAG_VARS:
            flags.add(FLAG_VARS[c])
        elif c == 18 and a > 0:
            moduli.append(a)
    for t, c, a in acts:  # MP paid by targets 2 / 3 (also the fallback's)
        if t in (2, 3) and entry["techs"][t - 1] > 0 and techs[entry["techs"][t - 1] - 1]["mp"] > 0:
            thr["mp"].append(techs[entry["techs"][t - 1] - 1]["mp"])
    v, bk = {}, {}
    for name, ts in thr.items():
        if ts:
            bk[name] = Buckets(ts, "" if name == "mp" else "%")
            v[name] = bk[name].options()
    if moduli:
        L = math.lcm(*moduli)
        v["turn"] = [("turn%%%d==%d" % (L, r), r) for r in range(L)]
    for a in sorted(set(allies)):
        v["ally%d" % a] = [("no", False), ("yes", True)]
    for k, xs in vals.items():
        if xs:
            v[k] = [("=%d" % x, x) for x in sorted(set(xs))] + [("other", None)]
    for f in sorted(flags):
        v[f] = [("no", False), ("yes", True)]
    return v, bk


def holds(c, a, asg, bk):
    """Truth of a non-random condition under an assignment (bk: variable -> Buckets of the entry)."""
    if c in HP_COND:
        name, ge = HP_COND[c]
        p = pct(a)
        if p <= 0:
            reached = True  # hp >= 0 always, hp < 0 never
        else:
            reached = bk[name].reaches(asg[name], p)
        return reached if ge else not reached
    if c in (4, 5):
        reached = True if a <= 0 else bk["mp"].reaches(asg["mp"], a)
        return reached if c == 5 else not reached
    if c == 10:
        return asg["ally%d" % a]  # arg 0: any other enemy alive; else one with that id
    if c in VALUE_VARS:
        return asg[VALUE_VARS[c]] == a
    if c in FLAG_VARS:
        return asg[FLAG_VARS[c]]
    if c == 18:
        if a == 0:
            raise ValueError("condition 18 with arg 0 divides by zero in the game")
        return asg["turn"] % a == 0
    return True  # 0 and the default case of the switch


def outcome(entry, techs, target, asg, bk):
    """target (BattleTableAction.target) -> what the enemy does, with the MP gate of targets 2/3."""
    if target in (1, 2, 3):
        tid = entry["techs"][target - 1]
        if target > 1 and tid > 0:
            cost = techs[tid - 1]["mp"]
            if cost > 0 and not bk["mp"].reaches(asg["mp"], cost):
                return "no action (not enough MP)"
        return "tech %d" % tid if tid else "no tech"
    return {4: "flee", 5: "switch to enemy 0", 6: "switch to enemy 1", 7: "switch to enemy 2", 8: "switch to random enemy"}.get(target, "none (target %d)" % target)


def evaluate(entry, techs, asg, bk):
    acts = entry_actions(entry)
    dist, rem = {}, Fraction(1)

    def add(t, p):
        o = outcome(entry, techs, t, asg, bk)
        dist[o] = dist.get(o, 0) + p
    for t, c, a in acts[:3]:
        if c == 1:
            p = Fraction(min(max(a, 0), 128), 128)  # [H] uniform RANDOM % 128
            add(t, rem * p)
            rem *= 1 - p
        elif holds(c, a, asg, bk):
            add(t, rem)
            rem = Fraction(0)
            break
    if rem:
        add(acts[3][0], rem)
    return dist


def analyze(entry, techs, state=None):
    """List of (distribution, [scenario dict]) of the entry for a (partial) state; several items = the answer depends on unknowns.
    state keys: hp, php (% of max HP), mp, turn, base, asleep, allies (list of ids, [] none), battle, kind, elem, charge, atk_down, def_down, spd_down."""
    state = state or {}
    v, bk = variables(entry, techs)
    fixed = {}
    for name, opts in v.items():
        given = _given(name, state, bk, opts)
        if given is not _MISSING:
            opts = [o for o in opts if o[1] == given]
            if not opts:
                raise ValueError("state value for %s matches no option" % name)
        fixed[name] = opts
    names = list(fixed)
    total = 1
    for n in names:
        total *= len(fixed[n])
    if total > 200000:
        raise ValueError("too many scenarios (%d)" % total)
    groups = []  # [dist, [scenarios]]
    for combo in _product([fixed[n] for n in names]):
        asg = {n: o[1] for n, o in zip(names, combo)}
        if "ally0" in asg and not asg["ally0"] and any(asg[k] for k in asg if k.startswith("ally") and k != "ally0"):
            continue  # an id-specific ally alive implies "some other enemy alive"
        d = evaluate(entry, techs, asg, bk)
        sc = {n: o[0] for n, o in zip(names, combo)}
        for g in groups:
            if g[0] == d:
                g[1].append(sc)
                break
        else:
            groups.append([d, [sc]])
    return [(g[0], g[1]) for g in groups]


_MISSING = object()


def _product(lists):
    out = [[]]
    for lst in lists:
        out = [p + [o] for p in out for o in lst]
    return out


def _given(name, state, bk, opts):
    n = len(opts)
    """The option value a stated variable selects, or _MISSING (unknown)."""
    if name in ("hp", "php"):
        return sum(1 for t in bk[name].ts if state[name] >= t) if name in state else _MISSING  # [H] maxHp multiple of 100
    if name == "mp":
        return sum(1 for t in bk["mp"].ts if state["mp"] >= t) if "mp" in state else _MISSING
    if name == "turn":
        return state["turn"] % n if "turn" in state else _MISSING
    if name.startswith("ally"):
        al = state.get("allies")
        if al is None or (al == [0] and name != "ally0"):  # "any" says nothing about which id
            return _MISSING
        return bool(al) if name == "ally0" else int(name[4:]) in al
    if name in ("elem", "battle", "kind"):
        if name not in state:
            return _MISSING
        return state[name] if state[name] in [o[1] for o in opts] else None  # an untested value = "other"
    return bool(state[name]) if name in state else _MISSING


def classify(btab, techs):
    """The three classes of docs/enemy-ai-and-magic-design.md 1.2: state-free deterministic, state-free random, state-dependent."""
    det, rng, state = [], [], []
    for fid, e in btab.items():
        acts = entry_actions(e)
        if all(c in (0, 1) for t, c, a in acts[:3]):
            groups = analyze(e, techs, {"mp": 10 ** 6})
            (det if len(groups[0][0]) == 1 else rng).append(fid)
        else:
            state.append(fid)
    return det, rng, state


def show(entry, techs, state):
    groups = analyze(entry, techs, state)
    print("fighter %d  techs %s  actions %s  (target, condition, arg)" % (entry["id"], entry["techs"], entry_actions(entry)))
    if len(groups) > 1:
        print("  %d distinct distributions: the answer depends on the unknown variables below" % len(groups))
    for i, (d, scs) in enumerate(groups, 1):
        print("  [%d] %s" % (i, ", ".join("%s: %s (%.4f)" % (k, p, float(p)) for k, p in sorted(d.items(), key=lambda kv: -kv[1]))))
        keys = list(scs[0])
        if not keys:
            print("      no state variable involved")
        elif len(scs) <= 6:
            for sc in scs:
                print("      when " + ", ".join("%s %s" % (k, sc[k]) for k in keys))
        else:
            print("      %d scenarios; values present per variable (a union, not a product): %s" %
                  (len(scs), "; ".join("%s {%s}" % (k, ", ".join(dict.fromkeys(sc[k] for sc in scs))) for k in keys)))
    if any(c == 1 for t, c, a in entry_actions(entry)[:3]):
        print("  [H] chance conditions assume each reached RANDOM.next() % 128 draw is conditionally uniform given the previous ones")
    return groups


def selftest():
    items, tb, btab = em.load()
    techs = tb["techs"]
    big = {"mp": 10 ** 6}
    det, rng, state = classify(btab, techs)
    assert len(btab) == 193 and len(det) + len(rng) + len(state) == 193
    # (11) first action 'always' -> a single outcome from actions[0]; the 77 state-free deterministic entries
    assert (len(det), len(rng), len(state)) == (77, 16, 100), (len(det), len(rng), len(state))
    for fid in det:
        e = btab[fid]
        (d, scs), = analyze(e, techs, big)
        assert len(d) == 1 and sum(d.values()) == 1
    class All:  # every MP threshold reached
        def reaches(self, k, t):
            return True
    for e in btab.values():
        t0 = entry_actions(e)[0]
        if t0[1] == 0:
            g = analyze(e, techs, big)
            assert len(g) == 1 and g[0][0] == {outcome(e, techs, t0[0], {"mp": 0}, {"mp": All()}): 1}
    # (12) fighter 217: [(1,c3,64),(2,c1,64),(1,c0),(1,c0)] -> 50 % threshold
    e217 = btab[217]
    assert entry_actions(e217) == [(1, 3, 64), (2, 1, 64), (1, 0, 0), (1, 0, 0)]
    g = analyze(e217, techs, dict(big, hp=80))
    assert len(g) == 1 and g[0][0] == {"tech %d" % e217["techs"][0]: 1}
    g = analyze(e217, techs, dict(big, hp=20))
    assert g[0][0] == {"tech %d" % e217["techs"][1]: Fraction(1, 2), "tech %d" % e217["techs"][0]: Fraction(1, 2)}
    syn = dict(e217, techs=[e217["techs"][0], next(t["id"] for t in techs if t["mp"] > 0), 0])
    cost = techs[syn["techs"][1] - 1]["mp"]
    g = analyze(syn, techs, {"hp": 20, "mp": cost - 1})
    assert g[0][0] == {"no action (not enough MP)": Fraction(1, 2), "tech %d" % syn["techs"][0]: Fraction(1, 2)}
    g = analyze(syn, techs, {"hp": 20, "mp": cost})
    assert g[0][0]["tech %d" % syn["techs"][1]] == Fraction(1, 2)
    # (13) two chance conditions: a/128, (1 - a/128) * b/128, the rest to the fallback; sums to 1
    chain = dict(e217, techs=[11, 12, 13], actions=[(1, 1, 40), (2, 1, 64), (3, 0, 0), (1, 0, 0)])
    p1, p2, p3 = Fraction(40, 128), (1 - Fraction(40, 128)) * Fraction(1, 2), (1 - Fraction(40, 128)) * Fraction(1, 2) * Fraction(100, 128)
    rest = 1 - p1 - p2 - p3
    chain3 = dict(chain, actions=[(1, 1, 40), (2, 1, 64), (3, 1, 100), (1, 0, 0)])
    (d, _), = analyze(chain3, techs)
    assert d["tech 12"] == p2 and d["tech 13"] == p3 and sum(d.values()) == 1 and d["tech 11"] == p1 + rest
    # (14) fighter 206
    e206 = btab[206]
    assert entry_actions(e206) == [(1, 3, 20), (6, 10, 365), (4, 13, 1), (1, 0, 0)]
    g = analyze(e206, techs)
    assert all(sum(d.values()) == 1 for d, _ in g) and len(g) >= 3
    t1 = "tech %d" % e206["techs"][0]
    assert analyze(e206, techs, {"hp": 90, "allies": [], "kind": 1})[0][0] == {t1: 1}
    assert analyze(e206, techs, {"hp": 1, "allies": [365], "kind": 1})[0][0] == {"switch to enemy 1": 1}
    assert analyze(e206, techs, {"hp": 1, "allies": [], "kind": 1})[0][0] == {"flee": 1}
    assert analyze(e206, techs, {"hp": 1, "allies": [], "kind": 3})[0][0] == {t1: 1}
    assert len(analyze(e206, techs, {"hp": 1})) > 1  # unknown allies / kind -> several distributions, no average
    # (15) probabilities sum to 1 in every entry and every scenario; the state-dependent ones give sets when a variable is missing
    nsets = 0
    for fid, e in btab.items():
        g = analyze(e, techs)
        assert all(sum(d.values()) == 1 for d, _ in g), fid
        nsets += len(g) > 1
    assert nsets >= 50, nsets
    # (16) no target 2 / 3 with an empty techs[k]
    for e in btab.values():
        for t, c, a in entry_actions(e):
            assert t not in (2, 3) or e["techs"][t - 1] > 0, e["id"]
    print("selftest ok")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "selftest"
    if cmd == "selftest":
        return selftest()
    items, tb, btab = em.load()
    techs = tb["techs"]
    if cmd == "summary":
        det, rng, state = classify(btab, techs)
        print("%d entries: %d state-free deterministic, %d state-free random (exact, [H] conditionally uniform RNG draws), %d state-dependent" %
              (len(btab), len(det), len(rng), len(state)))
        slots = {}
        for e in btab.values():
            for t, c, a in entry_actions(e)[:3]:
                slots[c] = slots.get(c, 0) + 1
        print("slots actions[0..2] by condition: " + ", ".join("%s %d" % (COND_NAMES.get(c, c), n) for c, n in sorted(slots.items())))
    elif cmd == "rng-only":
        _, rng, _ = classify(btab, techs)
        for fid in rng:
            show(btab[fid], techs, {"mp": 10 ** 6})
    else:
        opts = {"--hp": "hp", "--php": "php", "--mp": "mp", "--turn": "turn", "--battle": "battle", "--kind": "kind", "--elem": "elem",
                "--base": "base", "--asleep": "asleep", "--charge": "charge", "--atk-down": "atk_down", "--def-down": "def_down", "--spd-down": "spd_down"}
        args, state = sys.argv[2:], {}
        for i in range(0, len(args) - 1, 2):
            if args[i] == "--allies":
                state["allies"] = [] if args[i + 1] == "none" else [0] if args[i + 1] == "any" else [int(x) for x in args[i + 1].split(",")]
            elif args[i] in opts:
                state[opts[args[i]]] = int(args[i + 1])
            else:
                sys.exit(__doc__)
        show(btab[int(cmd)], techs, state)


if __name__ == "__main__":
    main()
