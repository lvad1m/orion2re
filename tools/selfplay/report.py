"""Compare self-play bench variants from advisor journals.

Usage: python tools/selfplay/report.py <bench dir> [baseline variant, default "observe"] [--turn N]

Reads <bench dir>/d<difficulty>-s<seed>-<variant>.jsonl written by selfplay.ps1 and prints, per
difficulty, the mean over seeds of the state summed over all empires at the last recorded turn (or
at turn N, counted from the start of the game, with --turn), and how each variant compares with
the baseline seed by seed (colonies).
"""
import collections
import json
import re
import statistics
import sys
from pathlib import Path

METRICS = ["colonies", "outposts", "population", "industry", "research", "techs", "spies", "treasury", "fleet"]
NAME = re.compile(r"^d(\d+)-s(\d+)-(.+)\.jsonl$")
GAME_START_STARDATE = 35000
TURN = None  # --turn N: snapshot at this turn instead of the last one


def snapshot_stardate(rows):
    """The last recorded stardate, or with --turn the last one not after that turn."""
    stardates = [r["stardate"] for r in rows]
    if TURN is None:
        return max(stardates)
    target = GAME_START_STARDATE + TURN
    earlier = [s for s in stardates if s <= target]
    return max(earlier) if earlier else min(stardates)


def final_players(path):
    """Colonies, population and industry per player at the snapshot turn."""
    rows = [json.loads(line) for line in path.open(encoding="utf-8")]
    last = snapshot_stardate(rows)
    players = {}
    for r in rows:
        if r["stardate"] != last:
            continue
        s = r["summary"]
        outposts = re.search(r"\((\d+) outposts\)", s)
        colonies = int(re.search(r"(\d+) colonies", s).group(1))
        players[r["player"]] = {
            "colonies": colonies,
            "populated": colonies - (int(outposts.group(1)) if outposts else 0),
            "population": int(re.search(r"population (\d+)", s).group(1)),
            "industry": int(re.search(r"industry (\d+)/turn", s).group(1)),
        }
    return players


def head_to_head(bench, difficulty, variant, baseline):
    """Share of the advised slots (mask from the variant name "h2h-<mask>[-suffix]") against the share of
    the same slots in the baseline run, seed by seed. Positive: advised empires took more."""
    mask = int(re.match(r"h2h-(\d+)", variant).group(1))
    print(f"    head to head {variant} (players {[i + 1 for i in range(8) if mask >> i & 1]}) vs the same slots in {baseline}:")
    for metric in ("colonies", "populated", "population", "industry"):
        deltas = []
        for path in sorted(bench.glob(f"d{difficulty}-s*-{variant}.jsonl")):
            seed = NAME.match(path.name).group(2)
            base_path = bench / f"d{difficulty}-s{seed}-{baseline}.jsonl"
            if not base_path.exists():
                continue
            smart = final_players(path)
            base = final_players(base_path)

            def share(players):
                total = sum(p[metric] for p in players.values()) or 1
                return sum(p[metric] for i, p in players.items() if mask >> i & 1) / total

            deltas.append(100.0 * (share(smart) - share(base)))
        if deltas:
            better = sum(1 for d in deltas if d > 0)
            worse = sum(1 for d in deltas if d < 0)
            print(f"      {metric:10s} share change {statistics.mean(deltas):+.1f} points (better {better}, worse {worse} of {len(deltas)})")


def final_state(path):
    rows = [json.loads(line) for line in path.open(encoding="utf-8")]
    last = snapshot_stardate(rows)
    totals = dict.fromkeys(METRICS, 0)
    for r in rows:
        if r["stardate"] != last:
            continue
        s = r["summary"]
        totals["colonies"] += int(re.search(r"(\d+) colonies", s).group(1))
        totals["population"] += int(re.search(r"population (\d+)", s).group(1))
        outposts = re.search(r"\((\d+) outposts\)", s)
        totals["outposts"] += int(outposts.group(1)) if outposts else 0
        totals["industry"] += int(re.search(r"industry (\d+)/turn", s).group(1))
        totals["research"] += int(re.search(r"research (\d+) RP", s).group(1))
        totals["treasury"] += int(re.search(r"treasury (-?\d+) BC", s).group(1))
        totals["fleet"] += int(re.search(r"strength (\d+)\. Command", s).group(1))
        techs = re.search(r"Technologies known: (\d+)", s)
        totals["techs"] += int(techs.group(1)) if techs else 0
        totals["spies"] += sum(int(m.group(1)) for m in re.finditer(r"our spies in their empire: (\d+)", s))
    changes = 0
    previous = {}
    rules = collections.Counter()
    for r in rows:
        objective = re.search(r"Objective: (\w+)", r["summary"]).group(1)
        if r["player"] in previous and previous[r["player"]] != objective:
            changes += 1
        previous[r["player"]] = objective
        rules.update(m.group(1) for m in re.finditer(r"rule (\d+)", r["rule_note"]))
    return totals, changes, rules


def main():
    global TURN
    args = sys.argv[1:]
    if "--turn" in args:
        position = args.index("--turn")
        TURN = int(args[position + 1])
        del args[position:position + 2]
    bench = Path(args[0])
    baseline = args[1] if len(args) > 1 else "observe"
    if TURN is not None:
        print(f"(snapshot at turn {TURN})")
    runs = collections.defaultdict(dict)  # (difficulty, variant) -> seed -> result
    variants = []
    for path in sorted(bench.glob("*.jsonl")):
        match = NAME.match(path.name)
        if not match:
            continue
        difficulty, seed, variant = int(match.group(1)), int(match.group(2)), match.group(3)
        if variant not in variants:
            variants.append(variant)
        runs[(difficulty, variant)][seed] = final_state(path)

    for difficulty in sorted({d for d, _ in runs}):
        print(f"=== difficulty {difficulty}")
        print("    " + "variant".ljust(18) + "".join(m.rjust(11) for m in METRICS) + "obj.changes".rjust(12) + "seeds".rjust(7))
        for variant in variants:
            results = runs.get((difficulty, variant), {})
            if not results:
                continue
            cells = "".join(f"{statistics.mean(r[0][m] for r in results.values()):11.1f}" for m in METRICS)
            changes = statistics.mean(r[1] for r in results.values())
            print("    " + variant.ljust(18) + cells + f"{changes:12.1f}" + f"{len(results):7d}")
        base = runs.get((difficulty, baseline), {})
        for variant in variants:
            if variant.startswith("h2h-") and base:
                head_to_head(bench, difficulty, variant, baseline)
        for variant in variants:
            if variant == baseline or not base or variant.startswith("h2h-"):
                continue
            results = runs.get((difficulty, variant), {})
            seeds = sorted(set(results) & set(base))
            deltas = [results[s][0]["colonies"] - base[s][0]["colonies"] for s in seeds]
            better = sum(1 for d in deltas if d > 0)
            worse = sum(1 for d in deltas if d < 0)
            print(f"    {variant} vs {baseline}, colonies per seed: {deltas} (better {better}, worse {worse})")


if __name__ == "__main__":
    main()
