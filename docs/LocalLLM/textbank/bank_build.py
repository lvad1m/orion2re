# -*- coding: utf-8 -*-
"""Validate and assemble the diplomacy text bank written by Claude.

The bank lives in bank/<race>.json:
  {"race": "psilons",
   "lines": {"<personality>": {"<situation>": {"ru": [3 lines], "en": [3 lines]}}}}

  python bank_build.py                 check every race, write out/bank/lines.jsonl, review_<race>.md, summary.md
  python bank_build.py --race sakkra   check one race and print its flags (for the writer to fix)

Flags are heuristics (textbank.flags_for + alphabet pattern + near-duplicates + coverage):
a flagged line is not dropped, it is listed for review.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import textbank as tb  # noqa: E402

BANK = os.path.join(HERE, "bank")
OUT = os.path.join(HERE, "out", "bank")
ALPHABET = {lang: re.compile(p) for lang, p in tb.PATTERN.items()}


def load_race(race):
    with open(os.path.join(BANK, f"{race}.json"), encoding="utf-8") as f:
        return json.load(f)


MIN_VARIANTS, TARGET_VARIANTS = 3, 10


def merge_more(race):
    """Append variants from bank/<race>.more*.json (same structure) to bank/<race>.json.
    Exact duplicates are skipped, so merging twice is harmless; merged files are renamed *.merged."""
    path = os.path.join(BANK, f"{race}.json")
    data = load_race(race)
    added = 0
    for name in sorted(os.listdir(BANK)):
        if not (name.startswith(f"{race}.more") and name.endswith(".json")):
            continue
        with open(os.path.join(BANK, name), encoding="utf-8") as f:
            more = json.load(f)
        for pers, by_sit in more.get("lines", {}).items():
            for sit, cell in by_sit.items():
                target = data["lines"].setdefault(pers, {}).setdefault(sit, {"ru": [], "en": []})
                for lang in ("ru", "en"):
                    for text in cell.get(lang, []):
                        if text not in target.setdefault(lang, []):
                            target[lang].append(text)
                            added += 1
        os.replace(os.path.join(BANK, name), os.path.join(BANK, name[:-5] + ".merged"))
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"{race}: merged {added} new lines")


def apply_fixes(path):
    """Replace single lines from a list of fixes, checking every new line first.
    File: [{"race", "personality", "situation", "lang", "old", "new"}, ...].
    A fix is rejected (and listed) if the old line is missing or the new line has a flag,
    repeats a line of its cell, or is too close (word overlap > 0.6) to any other race's line
    in the same situation and language. Rejected fixes change nothing."""
    with open(path, encoding="utf-8") as f:
        fixes = json.load(f)
    banks = {r: load_race(r) for r in tb.RACES if os.path.exists(os.path.join(BANK, f"{r}.json"))}
    applied, rejected = 0, []
    for fx in fixes:
        race, pers, sit, lang = fx["race"], fx["personality"], fx["situation"], fx["lang"]
        cell = banks[race]["lines"][pers][sit][lang]
        old, new = fx["old"], fx["new"].strip()
        if old not in cell:
            rejected.append((fx, "old line not found"))
            continue
        probs = tb.flags_for(new, race, sit, lang)
        if not ALPHABET[lang].match(new):
            probs.append("outside the alphabet")
        probs += [f"repeats own line: {t}" for t in cell if t != old and tb.jaccard(new, t) > 0.6]
        for r2, data in banks.items():
            if r2 == race:
                continue
            for p2, by in data["lines"].items():
                probs += [f"too close to {r2}/{p2}: {t}" for t in by[sit][lang] if tb.jaccard(new, t) > 0.6]
        if probs:
            rejected.append((fx, "; ".join(probs)))
            continue
        cell[cell.index(old)] = new
        applied += 1
    for race, data in banks.items():
        with open(os.path.join(BANK, f"{race}.json"), "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"applied {applied}, rejected {len(rejected)}")
    for fx, why in rejected:
        print(f"  REJECTED {fx['race']}/{fx['personality']}/{fx['situation']}/{fx['lang']}: {why}\n    new: {fx['new']}")


def check_race(race, data):
    lines, problems = [], []
    by_pers = data.get("lines", {})
    for pers in tb.PERSONALITIES:
        for sit in tb.SITUATIONS:
            cell = by_pers.get(pers, {}).get(sit)
            if not cell:
                problems.append(f"missing: {pers}/{sit}")
                continue
            for lang in ("ru", "en"):
                variants = cell.get(lang) or []
                if not MIN_VARIANTS <= len(variants) <= TARGET_VARIANTS:
                    problems.append(f"{pers}/{sit}/{lang}: {len(variants)} variants (expected {MIN_VARIANTS}..{TARGET_VARIANTS})")
                for i, text in enumerate(variants):
                    fl = tb.flags_for(text, race, sit, lang)
                    if not ALPHABET[lang].match(text):
                        bad = sorted({c for c in text if not ALPHABET[lang].match(c)})
                        fl.append("outside the alphabet: " + "".join(bad))
                    for j in range(i):
                        if tb.jaccard(text, variants[j]) > 0.6:
                            fl.append(f"near-duplicate of #{j}")
                    lines.append({"key": f"{race}/{pers}/{sit}/{lang}", "race": race, "personality": pers,
                                  "situation": sit, "lang": lang, "variant": i, "text": text, "flags": fl,
                                  "author": "claude-opus-5-5"})
    extra = [f"{p}/{s}" for p, d in by_pers.items() for s in d if p not in tb.PERSONALITIES or s not in tb.SITUATIONS]
    problems += [f"unknown cell: {x}" for x in extra]
    return lines, problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--race", default="")
    ap.add_argument("--merge", default="", help="race: append bank/<race>.more*.json to bank/<race>.json, then check it")
    ap.add_argument("--apply-fixes", default="", help="JSON list of single-line replacements, checked before applying")
    args = ap.parse_args()
    if args.apply_fixes:
        apply_fixes(args.apply_fixes)
        return
    if args.merge:
        merge_more(args.merge)
        args.race = args.merge
    races = [args.race] if args.race else [r for r in tb.RACES if os.path.exists(os.path.join(BANK, f"{r}.json"))]

    all_lines, summary = [], []
    for race in races:
        lines, problems = check_race(race, load_race(race))
        flagged = [l for l in lines if l["flags"]]
        summary.append((race, len(lines), len(flagged), problems))
        all_lines += lines
        if args.race:
            short = sum(1 for p in tb.PERSONALITIES for s in tb.SITUATIONS for lg in ("ru", "en")
                        if len(load_race(race)["lines"].get(p, {}).get(s, {}).get(lg, [])) < TARGET_VARIANTS)
            print(f"{race}: {len(lines)} lines, {len(flagged)} flagged, {len(problems)} structure problems, "
                  f"{short} cells below {TARGET_VARIANTS} variants")
            for p in problems:
                print("  STRUCTURE", p)
            for l in flagged:
                print(f"  FLAG {l['key']} #{l['variant']}: {'; '.join(l['flags'])} | {l['text']}")
    if args.race:
        return

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "lines.jsonl"), "w", encoding="utf-8") as f:
        for l in all_lines:
            f.write(json.dumps(l, ensure_ascii=False) + "\n")
    for race in races:
        tb.write_review([l for l in all_lines if l["race"] == race], os.path.join(OUT, f"review_{race}.md"), "claude-opus-5-5")
    full = len(tb.PERSONALITIES) * len(tb.SITUATIONS) * TARGET_VARIANTS * 2
    md = ["# Банк реплик: сводка\n",
          f"Цель: {len(tb.RACES)} рас × {len(tb.PERSONALITIES)} характеров × {len(tb.SITUATIONS)} ситуаций × {TARGET_VARIANTS} вариантов × 2 языка "
          f"= {len(tb.RACES) * full} строк; на расу — {full}.\n",
          "| Раса | Строк | С пометками | Проблемы структуры |", "|---|---:|---:|---|"]
    for race, n, nf, probs in summary:
        md.append(f"| {race} | {n} | {nf} | {'; '.join(probs[:5]) + (' …' if len(probs) > 5 else '') if probs else '—'} |")
    md.append(f"\nВсего: {len(all_lines)} строк, с пометками {sum(s[2] for s in summary)}.")
    open(os.path.join(OUT, "summary.md"), "w", encoding="utf-8").write("\n".join(md))
    print("\n".join(md))


if __name__ == "__main__":
    main()
