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


def variables(entry, techs, maxmp=None):
    """(v, bk): v = name -> list of (label, value) for the state variables this entry's conditions (and the MP of its targets 2/3) test;
    bk = name -> Buckets for the numeric ones. maxmp (context mode): MP buckets above it are dropped and turn residues are for n >= 1."""
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
            if name == "mp" and maxmp is not None:
                v[name] = [o for o in v[name] if o[1] == 0 or bk[name].ts[o[1] - 1] <= maxmp]
    if moduli:
        L = math.lcm(*moduli)
        v["turn"] = [("turn%%%d==%d%s" % (L, r, "" if maxmp is None else " (n>=1)"), r) for r in range(L)]
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


def analyze(entry, techs, state=None, maxmp=None):
    """List of (distribution, [scenario dict]) of the entry for a (partial) state; several items = the answer depends on unknowns.
    state keys: hp, php (% of max HP), mp, turn, base, asleep, allies (list of ids, [] none), battle, kind, elem, charge, atk_down, def_down, spd_down."""
    state = state or {}
    v, bk = variables(entry, techs, maxmp)
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


def print_groups(entry, groups, indent="  "):
    if len(groups) > 1:
        print(indent + "%d distinct distributions: the answer depends on the unknown variables below" % len(groups))
    for i, (d, scs) in enumerate(groups, 1):
        print(indent + "[%d] %s" % (i, ", ".join("%s: %s (%.4f)" % (k, p, float(p)) for k, p in sorted(d.items(), key=lambda kv: -kv[1]))))
        keys = list(scs[0])
        if not keys:
            print(indent + "    no state variable involved")
        elif len(scs) <= 6:
            for sc in scs:
                print(indent + "    when " + ", ".join("%s %s" % (k, sc[k]) for k in keys))
        else:
            print(indent + "    %d scenarios; values present per variable (a union, not a product): %s" %
                  (len(scs), "; ".join("%s {%s}" % (k, ", ".join(dict.fromkeys(sc[k] for sc in scs))) for k in keys)))
    if any(c == 1 for t, c, a in entry_actions(entry)[:3]):
        print(indent + "[H] chance conditions assume each reached RANDOM.next() % 128 draw is conditionally uniform given the previous ones")


def show(entry, techs, state):
    groups = analyze(entry, techs, state)
    print("fighter %d  techs %s  actions %s  (target, condition, arg)" % (entry["id"], entry["techs"], entry_actions(entry)))
    print_groups(entry, groups)
    return groups


# ---- v2: the battle context (encounter, position, kind, other occupants, initial MP) comes from the data ----
CONTEXT_NOTES = {
    "random": "",
    "area3-unresolved": "no identified appearance in the random lists of areas 0-2; it is NOT asserted that it cannot appear in random battles",
    "unreferenced": "the encounter has no slot in any list; kept, another way to reach it is unknown [?]",
}


def fighter_contexts(tb, fid, cover):
    """Every (encounter, position) where fighter `fid` stands, with what the data says about it. cover: encounter -> label (battle_lists)."""
    out = []
    for e in tb["encounters"]:
        occ = [tb["enemies"][x]["fighter"] if x else 0 for x in e["enemies"]]
        for pos, x in enumerate(e["enemies"]):
            if x and occ[pos] == fid:
                out.append(dict(encounter=e["id"], position=pos, kind=e["kind"], occupants=occ, mp=tb["enemies"][x]["mp"],
                                cover=cover.get(e["id"], "unreferenced")))
    return out


def ctx_allies(ctx):
    """Ids of the OTHER occupants (the own slot never counts: enemy_turn.c:373-390 skips the active slot)."""
    return [f for p, f in enumerate(ctx["occupants"]) if p != ctx["position"] and f]


def resolve_context(ctxs, fid, encounter, position=None):
    """The one context named by --encounter / --position. A fighter standing in the same encounter more than once needs --position."""
    here = [c for c in ctxs if c["encounter"] == encounter]
    if not here:
        raise ValueError("fighter %d does not appear in encounter %d (encounters: %s)" % (fid, encounter, ", ".join(str(c["encounter"]) for c in ctxs)))
    if position is not None:
        hit = [c for c in here if c["position"] == position]
        if not hit:
            raise ValueError("position %d of encounter %d does not hold fighter %d (positions: %s)" % (position, encounter, fid, ", ".join(str(c["position"]) for c in here)))
        return hit[0]
    if len(here) > 1:
        raise ValueError("fighter %d stands %d times in encounter %d (positions %s): --position is required" % (fid, len(here), encounter, ", ".join(str(c["position"]) for c in here)))
    return here[0]


def ctx_state(ctx, over=None, mp_any=False, turn_any=False):
    over = over or {}
    if mp_any and "mp" in over:
        raise ValueError("--mp-any (any compatible MP) contradicts --mp: give one of them")
    if turn_any and "turn" in over:
        raise ValueError("--turn-any (any n >= 1) contradicts --turn: give one of them")
    if "turn" in over and over["turn"] < 1:
        raise ValueError("--turn %d: with a context the turn counter is >= 1 (the first decision sees 1)" % over["turn"])
    if "mp" in over and not 0 <= over["mp"] <= ctx["mp"]:
        raise ValueError("--mp %d is outside 0..maxMp = %d of %s: it is not a compatible state" % (over["mp"], ctx["mp"], ctx_label(ctx)))
    st = {"battle": ctx["encounter"], "kind": ctx["kind"], "allies": ctx_allies(ctx), "mp": ctx["mp"], "turn": 1}
    if mp_any:
        del st["mp"]
    if turn_any:
        del st["turn"]
    st.update(over)
    return st


def ctx_groups(entry, techs, ctx, over=None, mp_any=False, turn_any=False):
    return analyze(entry, techs, ctx_state(ctx, over, mp_any, turn_any), maxmp=ctx["mp"])


def initial_uses(entry, techs, mp):
    """[(tech id, cost, mp // cost)] for the techs of targets 2 / 3 that cost MP: uses the INITIAL MP funds with no replenishment."""
    out = []
    for t, c, a in entry_actions(entry):
        tid = entry["techs"][t - 1] if t in (2, 3) else 0
        if tid > 0 and techs[tid - 1]["mp"] > 0 and (tid, techs[tid - 1]["mp"], mp // techs[tid - 1]["mp"]) not in out:
            out.append((tid, techs[tid - 1]["mp"], mp // techs[tid - 1]["mp"]))
    return out


def ctx_label(c):
    return "encounter %d position %d" % (c["encounter"], c["position"])


def show_contexts(entry, techs, ctxs, over=None, mp_any=False, turn_any=False):
    """One section per context; contexts are never merged or averaged (an identical result is only pointed out)."""
    print("fighter %d  techs %s  actions %s  (target, condition, arg)" % (entry["id"], entry["techs"], entry_actions(entry)))
    print("contexts: %d. Deduced from the data [D] (tables + stage lists), not observed in the running game." % len(ctxs))
    seen, sections = {}, []
    for c in ctxs:
        groups = ctx_groups(entry, techs, c, over, mp_any, turn_any)
        print("context: %s  coverage %s  kind %d  other occupants %s  initial MP %d" % (ctx_label(c), c["cover"], c["kind"], ctx_allies(c), c["mp"]))
        if CONTEXT_NOTES[c["cover"]]:
            print("  note: " + CONTEXT_NOTES[c["cover"]])
        print("  state: turn %s, MP %s; other variables as given or unknown" % ("any n>=1" if turn_any else (over or {}).get("turn", 1),
              "any value <= maxMp (compatible coverage)" if mp_any else (over or {}).get("mp", "= initial")))
        if "mp" in variables(entry, techs)[0]:
            print("  MP: bound MP <= maxMp = %d [C]; the buckets are compatible coverage, not assumed reachable states" % c["mp"])
        for tid, cost, n in initial_uses(entry, techs, c["mp"]):
            print("  initial uses without replenishment (initial MP // cost): tech %d costs %d -> %d; funds only the initial MP, not a maximum "
                  "(player MP drains may replenish; no upper bound is declared)" % (tid, cost, n))
        sig = tuple((tuple(sorted((k, str(p)) for k, p in d.items())), tuple(tuple(sc.items()) for sc in scs)) for d, scs in groups)
        if sig in seen:
            print("  identical to the result of context %s (not merged)" % ctx_label(seen[sig]))
        seen.setdefault(sig, c)
        print_groups(entry, groups)
        sections.append((c, groups))
    return sections


def load_cover(stages_dir, tb):
    """encounter -> coverage label, read from the stages directory (imported only when a context is asked for)."""
    import battle_lists as bl
    try:
        ap = bl.appearances(bl.scan(stages_dir, len(tb["encounters"])))
    except bl.BattleListError as e:
        sys.exit("enemy_actions: %s" % e)
    return {e["id"]: bl.coverage(ap.get(e["id"], [])) for e in tb["encounters"]}


def selftest(stages_dir=None):
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
    context_selftest(stages_dir, tb, btab, techs)


def _captured(fn, *a, **k):
    import contextlib
    import io
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        fn(*a, **k)
    return buf.getvalue()


def context_selftest(stages_dir, tb, btab, techs):
    import subprocess
    here = os.path.dirname(os.path.abspath(__file__))
    # --no-context keeps the v1 path and never imports the extractor (checked in a clean process)
    code = ("import sys; sys.argv=['x']; import enemy_actions as ea; ea.main(['206','--no-context','--hp','1','--allies','none','--kind','3']); "
            "assert 'battle_lists' not in sys.modules")
    out = subprocess.run([sys.executable, "-c", code], cwd=here, capture_output=True, text=True)
    assert out.returncode == 0 and "tech" in out.stdout, out.stderr
    for bad in (["206"], ["206", "--no-context", "--encounter", "35"], ["206", "--stages-dir", os.path.join(here, "no-such-dir"), "--contexts"]):
        r = subprocess.run([sys.executable, os.path.join(here, "enemy_actions.py")] + bad, capture_output=True, text=True)
        assert r.returncode != 0 and ("--stages-dir" in r.stderr or "--no-context" in r.stderr), (bad, r.stderr)
    dummy = os.path.join(here, "no-such-dir")  # these refusals happen before the directory is read
    for bad, frag in ((["170", "--mp-any", "--mp", "96"], "contradicts --mp"), (["170", "--turn-any", "--turn", "1"], "contradicts --turn"),
                      (["170", "--encounter", "35", "--turn", "0"], ">= 1"), (["170", "--turn", "-2"], ">= 1"),
                      (["170", "--contexts", "--encounter", "35"], "only lists"), (["170", "--contexts", "--position", "0"], "--position needs --encounter"),
                      (["170", "--contexts", "--hp", "10"], "only lists"), (["170", "--contexts", "--mp-any"], "only lists"),
                      (["170", "--position", "0"], "--position needs --encounter"), (["170", "--battle", "3"], "come from the encounter")):
        r = subprocess.run([sys.executable, os.path.join(here, "enemy_actions.py")] + bad[:1] + ["--stages-dir", dummy] + bad[1:], capture_output=True, text=True)
        assert r.returncode != 0 and frag in r.stderr and "Traceback" not in r.stderr, (bad, r.stderr)
    r = subprocess.run([sys.executable, os.path.join(here, "enemy_actions.py"), "206", "--no-context", "--turn", "0"], capture_output=True, text=True)
    assert r.returncode == 0 and "fighter 206" in r.stdout, r.stderr  # v1 keeps accepting turn 0 without a context
    if not stages_dir:
        print("selftest ok (context tests SKIPPED: pass --stages-dir to run them)")
        return
    cover = load_cover(stages_dir, tb)
    ctxs = {fid: fighter_contexts(tb, fid, cover) for fid in btab}
    assert sum(len(v) for v in ctxs.values()) == 522 and all(ctxs.values())
    # 1. fighter 206: 'flee' only in encounter 35, never in the kind 3 encounters
    assert len(ctxs[206]) == 8
    for c in ctxs[206]:
        outs = {o for d, _ in ctx_groups(btab[206], techs, c) for o in d}
        assert ("flee" in outs) == (c["encounter"] == 35), (c, outs)
        assert (c["kind"] == 3) == (c["encounter"] != 35)
    # 2. 178 and 436: every real context, all area3-unresolved, none dropped, nothing called 'event'
    for fid, encs in ((178, [30, 198]), (436, [12, 262, 315, 323])):
        assert sorted(c["encounter"] for c in ctxs[fid]) == encs and {c["cover"] for c in ctxs[fid]} == {"area3-unresolved"}
        txt = _captured(show_contexts, btab[fid], techs, ctxs[fid])
        assert txt.count("context: encounter") == len(encs) and "NOT asserted" in txt and "event" not in txt.lower()
    # 2b. anti-false-classification
    only3 = [f for f, v in ctxs.items() if all(c["cover"] == "area3-unresolved" for c in v)]
    anyrnd = [f for f, v in ctxs.items() if any(c["cover"] == "random" for c in v)]
    assert len(only3) == 44 and len(anyrnd) == 149 and not set(only3) & set(anyrnd), (len(only3), len(anyrnd))
    assert {c["cover"] for v in ctxs.values() for c in v} <= {"random", "area3-unresolved", "unreferenced"}
    # 3. conditions 12 / 13 are true exactly in the context whose encounter / kind they name
    n12 = n13 = 0
    for fid, e in btab.items():
        for t, cnd, arg in entry_actions(e)[:3]:
            if cnd in (12, 13):
                for c in ctxs[fid]:
                    v, bk = variables(e, techs, c["mp"])
                    name = VALUE_VARS[cnd]
                    got = _given(name, ctx_state(c), bk, v[name])
                    truth = (c["encounter"] if cnd == 12 else c["kind"]) == arg
                    assert (got == arg) == truth, (fid, c, cnd, arg)
                    n12 += cnd == 12
                    n13 += cnd == 13
    assert n12 and n13
    # 4. allies: the own slot never counts; fighter 76 with arg 76 needs a second copy
    e76 = btab[76]
    assert any(cnd == 10 and arg == 76 for t, cnd, arg in entry_actions(e76)[:3])
    for c in ctxs[76]:
        assert (76 in ctx_allies(c)) == (c["occupants"].count(76) >= 2), c
    assert any(c["occupants"].count(76) >= 2 for c in ctxs[76]) and any(c["occupants"].count(76) == 1 for c in ctxs[76])
    # position strictness
    dup = [(f, c["encounter"]) for f, v in ctxs.items() for c in v if sum(x["encounter"] == c["encounter"] for x in v) > 1]
    assert dup, "no fighter stands twice in one encounter"
    f, enc = dup[0]
    for kw, frag in (({}, "--position is required"), ({"position": 9}, "does not hold")):
        try:
            resolve_context(ctxs[f], f, enc, **kw)
        except ValueError as ex:
            assert frag in str(ex)
        else:
            raise AssertionError("no error for " + frag)
    assert resolve_context(ctxs[f], f, enc, position=[c["position"] for c in ctxs[f] if c["encounter"] == enc][0])["encounter"] == enc
    try:
        resolve_context(ctxs[f], f, 99999)
    except ValueError as ex:
        assert "does not appear" in str(ex)
    # 5. MP: initial uses are not a maximum, nothing above maxMp, buckets are 'compatible coverage'
    e170 = btab[170]
    assert [x[2] for x in initial_uses(e170, techs, 96)] == [2] and initial_uses(e170, techs, 96)[0][1] == 48
    for c in ctxs[170]:
        v, bk = variables(e170, techs, c["mp"])
        assert all(o[1] == 0 or bk["mp"].ts[o[1] - 1] <= c["mp"] for o in v["mp"])
        txt = _captured(show_contexts, e170, techs, [c], mp_any=True)
        assert "without replenishment" in txt and "compatible coverage" in txt and "not a maximum" in txt and "no upper bound is declared" in txt
        assert "reachable states" in txt and "MP <= maxMp" in txt
    # 5b. an explicit --mp must lie in 0..maxMp of the context: inside is resolved, outside is an error (never approximated to a bucket)
    for fid in (170, 217, 206):
        for c in ctxs[fid]:
            for ok_mp in (0, c["mp"] // 2, c["mp"]):
                g = ctx_groups(btab[fid], techs, c, {"mp": ok_mp})
                assert g and all(sum(d.values()) == 1 for d, _ in g)
            for bad_mp in (c["mp"] + 1, c["mp"] * 2 + 7, -1):
                try:
                    ctx_groups(btab[fid], techs, c, {"mp": bad_mp})
                except ValueError as ex:
                    assert "not a compatible state" in str(ex), ex
                else:
                    raise AssertionError("mp %d accepted above maxMp %d (fighter %d)" % (bad_mp, c["mp"], fid))
    e170 = btab[170]
    c = ctxs[170][0]
    for m, lab in ((47, "[0, 48)"), (48, "[48, inf)"), (96, "[48, inf)")):  # fighter 170: one threshold at its tech cost 48
        assert {sc["mp"] for d, scs in ctx_groups(e170, techs, c, {"mp": m}) for sc in scs} == {lab}, m
    # 6. turns: n >= 1 only, checked with concrete values of n against the residue the entry really tests
    tested = 0
    for fid, e in btab.items():
        mods = [a for t, cnd, a in entry_actions(e)[:3] if cnd == 18 and a > 0]
        if not mods:
            continue
        L = math.lcm(*mods)
        c = ctxs[fid][0]
        free = ctx_groups(e, techs, c, turn_any=True)
        labels = {sc["turn"] for d, scs in free for sc in scs}
        assert labels == {"turn%%%d==%d (n>=1)" % (L, r) for r in range(L)}, (fid, labels)  # all L residues, none missing, none for n = 0
        for n in range(1, 2 * L + 2):  # n = 1 .. 2L+1: n = L is residue 0 (a real, positive turn), no scenario stands for n = 0
            got = ctx_groups(e, techs, c, {"turn": n})
            want = [g for g in free if any(sc["turn"].startswith("turn%%%d==%d " % (L, n % L)) for sc in g[1])]
            key = lambda gs: sorted(sorted((k, str(p)) for k, p in d.items()) for d, _ in gs)  # noqa: E731
            assert want and got and key(got) == key(want), (fid, n)  # the distributions of turn n are those of the free scenarios with n's residue
        for bad_n in (0, -1):
            try:
                ctx_groups(e, techs, c, {"turn": bad_n})
            except ValueError as ex:
                assert ">= 1" in str(ex)
            else:
                raise AssertionError("turn %d accepted in a context" % bad_n)
        tested += 1
    assert tested >= 5, tested
    for bad_over, frag in (({"mp": 96}, "contradicts --mp"), ({"turn": 1}, "contradicts --turn")):
        try:
            ctx_state(ctxs[170][0], bad_over, mp_any="mp" in bad_over, turn_any="turn" in bad_over)
        except ValueError as ex:
            assert frag in str(ex)
        else:
            raise AssertionError("contradictory state accepted: " + frag)
    # --contexts keeps the area-3 note for a fighter with no random appearance, and starts nothing else
    import io, contextlib
    for fid in (178, 436):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            main([str(fid), "--stages-dir", stages_dir, "--contexts"])
        assert buf.getvalue().count("NOT asserted") == len(ctxs[fid]) and "event" not in buf.getvalue().lower()
    # 7. v1 classification (the CLI regression against the v1 file is done by hand, see the commit notes)
    det, rng, state = classify(btab, techs)
    assert (len(det), len(rng), len(state)) == (77, 16, 100)
    # 8. probabilities sum to 1 in every context and nothing is merged: one section per context
    for fid, v in ctxs.items():
        for c in v:
            assert all(sum(d.values()) == 1 for d, _ in ctx_groups(btab[fid], techs, c)), (fid, c)
            assert all(sum(d.values()) == 1 for d, _ in ctx_groups(btab[fid], techs, c, mp_any=True, turn_any=True)), (fid, c)
    sec = show_contexts_count(btab[206], techs, ctxs[206])
    assert sec == len(ctxs[206])
    full, still = context_summary(btab, techs, tb, cover)
    assert len(full) + len(still) == 193
    print("selftest ok")


def show_contexts_count(entry, techs, ctxs):
    return len(_captured_sections(entry, techs, ctxs))


def _captured_sections(entry, techs, ctxs):
    import contextlib
    import io
    with contextlib.redirect_stdout(io.StringIO()):
        return show_contexts(entry, techs, ctxs)


def parse_args(args):
    """-> (state, switches, values). state: variables given by the user; switches: --contexts --no-context --mp-any --turn-any;
    values: --stages-dir --encounter --position."""
    opts = {"--hp": "hp", "--php": "php", "--mp": "mp", "--turn": "turn", "--battle": "battle", "--kind": "kind", "--elem": "elem",
            "--base": "base", "--asleep": "asleep", "--charge": "charge", "--atk-down": "atk_down", "--def-down": "def_down", "--spd-down": "spd_down"}
    state, switches, values, i = {}, set(), {}, 0
    while i < len(args):
        a = args[i]
        if a in ("--contexts", "--no-context", "--mp-any", "--turn-any"):
            switches.add(a)
            i += 1
            continue
        if i + 1 >= len(args):
            sys.exit(__doc__)
        v = args[i + 1]
        if a == "--allies":
            state["allies"] = [] if v == "none" else [0] if v == "any" else [int(x) for x in v.split(",")]
        elif a == "--stages-dir":
            values["stages_dir"] = v
        elif a in ("--encounter", "--position"):
            values[a[2:]] = int(v)
        elif a in opts:
            state[opts[a]] = int(v)
        else:
            sys.exit(__doc__)
        i += 2
    return state, switches, values


def context_summary(btab, techs, tb, cover):
    """Entries whose answer is one single distribution in EVERY one of their contexts (turn 1, MP = initial; HP, player and boosts free)
    against those that still depend on the unknowns of R6."""
    full, still = [], []
    for fid, e in btab.items():
        if all(len(ctx_groups(e, techs, c)) == 1 for c in fighter_contexts(tb, fid, cover)):
            full.append(fid)
        else:
            still.append(fid)
    return full, still


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    cmd = argv[0] if argv else "selftest"
    rest = argv[1:]
    if cmd == "selftest":
        return selftest(parse_args(rest)[2].get("stages_dir"))
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
        sd = parse_args(rest)[2].get("stages_dir")
        if sd:
            full, still = context_summary(btab, techs, tb, load_cover(sd, tb))
            print("by context (turn 1, MP = initial; HP, player state and boosts unknown): %d entries fully determined by the context "
                  "(one distribution in every context), %d still need HP / player / boosts" % (len(full), len(still)))
    elif cmd == "rng-only":
        _, rng, _ = classify(btab, techs)
        for fid in rng:
            show(btab[fid], techs, {"mp": 10 ** 6})
    else:
        state, switches, values = parse_args(rest)
        fid = int(cmd)
        if "--no-context" in switches:
            if set(values) or switches & {"--contexts", "--mp-any", "--turn-any"}:
                sys.exit("--no-context takes none of --stages-dir --encounter --position --contexts --mp-any --turn-any")
            show(btab[fid], techs, state)
            return
        if "stages_dir" not in values:
            sys.exit("enemy_actions: the battle context needs --stages-dir (dw3_decomp/src/field/stages); use --no-context for the free-variable analysis")
        if set(state) & {"battle", "kind", "allies"}:
            sys.exit("enemy_actions: with a context battle, kind and allies come from the encounter; use --no-context to give them by hand")
        if "position" in values and "encounter" not in values:
            sys.exit("enemy_actions: --position needs --encounter")
        if "--contexts" in switches and (set(state) or set(values) - {"stages_dir"} or switches & {"--mp-any", "--turn-any"}):
            sys.exit("enemy_actions: --contexts only lists the contexts; it takes no --encounter, --position, state or --mp-any / --turn-any argument")
        try:
            ctx_state(dict(encounter=0, kind=0, occupants=[], position=0, mp=10 ** 9), state, "--mp-any" in switches, "--turn-any" in switches)
        except ValueError as e:
            sys.exit("enemy_actions: %s" % e)
        ctxs = fighter_contexts(tb, fid, load_cover(values["stages_dir"], tb))
        if "--contexts" in switches:
            print("fighter %d: %d contexts (deduced from the data [D])" % (fid, len(ctxs)))
            for c in ctxs:
                print("  %s  coverage %s  kind %d  other occupants %s  initial MP %d" % (ctx_label(c), c["cover"], c["kind"], ctx_allies(c), c["mp"]))
                if CONTEXT_NOTES[c["cover"]]:
                    print("    note: " + CONTEXT_NOTES[c["cover"]])
            return
        if "encounter" in values:
            try:
                ctxs = [resolve_context(ctxs, fid, values["encounter"], values.get("position"))]
            except ValueError as e:
                sys.exit("enemy_actions: %s" % e)
        try:
            show_contexts(btab[fid], techs, ctxs, state, "--mp-any" in switches, "--turn-any" in switches)
        except ValueError as e:
            sys.exit("enemy_actions: %s" % e)


if __name__ == "__main__":
    main()
