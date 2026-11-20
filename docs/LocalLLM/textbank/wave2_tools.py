# -*- coding: utf-8 -*-
"""Small helpers for Wave 2 writers, so nobody has to read a whole race file.

  python wave2_tools.py show <race> <personality>      existing lines of one personality (compact)
  python wave2_tools.py check <race> <personality>     check bank/<race>.more.<personality>.json before merging:
                                                       structure (18 situations x 7 ru + 7 en), flags,
                                                       alphabet, repeats against the race's own lines and
                                                       against other races (same situation, word overlap > 0.6)
Then merge: python bank_build.py --merge <race>
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import textbank as tb  # noqa: E402

BANK = os.path.join(HERE, "bank")
ALPHABET = {lang: re.compile(p) for lang, p in tb.PATTERN.items()}


def load(race):
    with open(os.path.join(BANK, f"{race}.json"), encoding="utf-8") as f:
        return json.load(f)["lines"]


def show(race, pers):
    d = load(race).get(pers, {})
    for sit in tb.SITUATIONS:
        cell = d.get(sit, {})
        print(f"[{sit}]")
        for lang in ("ru", "en"):
            for t in cell.get(lang, []):
                print(f"  {lang}: {t}")


def check(race, pers):
    path = os.path.join(BANK, f"{race}.more.{pers}.json")
    with open(path, encoding="utf-8") as f:
        more = json.load(f)["lines"].get(pers, {})
    own = load(race).get(pers, {})
    others = {r: load(r) for r in tb.RACES if r != race and os.path.exists(os.path.join(BANK, f"{r}.json"))}
    problems = 0
    for sit in tb.SITUATIONS:
        cell = more.get(sit)
        if not cell:
            print(f"MISSING {sit}")
            problems += 1
            continue
        for lang in ("ru", "en"):
            new = cell.get(lang, [])
            if len(new) != 7:
                print(f"COUNT {sit}/{lang}: {len(new)} instead of 7")
                problems += 1
            existing = own.get(sit, {}).get(lang, [])
            for i, t in enumerate(new):
                fl = tb.flags_for(t, race, sit, lang)
                if not ALPHABET[lang].match(t):
                    fl.append("outside the alphabet: " + "".join(sorted({c for c in t if not ALPHABET[lang].match(c)})))
                for e in existing + new[:i]:
                    if tb.jaccard(t, e) > 0.6:
                        fl.append(f"repeats own line: {e}")
                for r, lines in others.items():
                    for p2, by in lines.items():
                        for e in by.get(sit, {}).get(lang, []):
                            if tb.jaccard(t, e) > 0.6:
                                fl.append(f"too close to {r}/{p2}: {e}")
                if fl:
                    problems += 1
                    print(f"FLAG {sit}/{lang} #{i}: {t}\n     -> " + "\n     -> ".join(fl))
    print(f"{race}/{pers}: {problems} problems")


def crossdupes(out_path):
    """List lines to rewrite so that no two races share a line (word overlap > 0.6, same
    situation and language). From each pair one side is chosen; a line already chosen covers
    all its pairs. Writes [{"race", "personality", "situation", "lang", "old", "conflicts": [...]}]."""
    import itertools
    banks = {r: load(r) for r in tb.RACES if os.path.exists(os.path.join(BANK, f"{r}.json"))}
    chosen = {}
    for sit in tb.SITUATIONS:
        for lang in ("ru", "en"):
            items = [(r, p, t) for r, b in banks.items() for p, by in b.items() for t in by[sit][lang]]
            for a, b in itertools.combinations(items, 2):
                if a[0] == b[0] or tb.jaccard(a[2], b[2]) <= 0.6:
                    continue
                ka, kb = (a[0], a[1], sit, lang, a[2]), (b[0], b[1], sit, lang, b[2])
                if ka in chosen:
                    chosen[ka].append(f"{b[0]}/{b[1]}: {b[2]}")
                elif kb in chosen:
                    chosen[kb].append(f"{a[0]}/{a[1]}: {a[2]}")
                else:
                    chosen[kb] = [f"{a[0]}/{a[1]}: {a[2]}"]
    todo = [{"race": k[0], "personality": k[1], "situation": k[2], "lang": k[3], "old": k[4], "conflicts": v}
            for k, v in sorted(chosen.items())]
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(todo, f, ensure_ascii=False, indent=1)
    print(f"{len(todo)} lines to rewrite -> {out_path}")


if __name__ == "__main__":
    if sys.argv[1] == "crossdupes":
        crossdupes(sys.argv[2])
    else:
        cmd, race, pers = sys.argv[1:4]
        {"show": show, "check": check}[cmd](race, pers)
