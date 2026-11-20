# -*- coding: utf-8 -*-
"""Offline text bank for orion2re: diplomacy lines by race x personality x situation x language.

Decision of 20.11.2026 (docs/LocalLLM/README.md, section 13): a pre-generated bank of lines
ships as game data, so machines without a GPU (level B) still get varied leader speech.

  python textbank.py --pilot                       2 races x 6 personalities x 6 situations x ru/en
  python textbank.py --races all --situations all  full bank
  python textbank.py --pilot --model qwen35-9b     another model from the catalog

Every request asks for 3 clearly different variants of one line, under a JSON schema whose
pattern limits the alphabet (Cyrillic only for Russian, Latin only for English).
Output: out/<stamp>/lines.jsonl (one line per variant, with automatic flags) and review.md.
"""
import argparse
import concurrent.futures as cf
import datetime as dt
import json
import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def _bench():
    """llama.cpp helpers from E:\\AI\\Scripts\\bench (server start/stop, chat). Needed only to generate
    lines with a local model; checking the bank (bank_build.py) works without them."""
    sys.path.insert(0, os.path.join(HERE, "..", "bench"))
    import bench
    return bench

# ---------------------------------------------------------------------------
# Races: how they speak. Short style notes, written for this project.

RACES = {
    "alkari":     ("алкари", "Alkari", "Avian race of proud aristocrats. Dignified, honour-bound; images of sky, wings, flight and heights."),
    "bulrathi":   ("булраси", "Bulrathi", "Huge bear-like warriors. Blunt, short sentences, pride in raw strength, little patience for words."),
    "darloks":    ("дарлоки", "Darloks", "Shapeshifting spies. Secretive, ambiguous, quietly menacing; hints and half-truths, never a straight answer."),
    "elerians":   ("элериане", "Elerians", "Telepathic matriarchy. Regal and mystical; speak of foresight, minds and destiny."),
    "gnolams":    ("гноламы", "Gnolams", "Merchant race. Everything is a deal: profit, prices, contracts, interest, bargains."),
    "humans":     ("люди", "Humans", "Diplomats of a federation. Eloquent, polite, formal; parliamentary and legal turns of phrase."),
    "klackons":   ("клаконы", "Klackons", "Insect hive mind. ALWAYS speak as 'we' for the whole hive; simple, repetitive, work and the Queen."),
    "meklars":    ("меклары", "Meklars", "Cyborgs. Cold machine logic: calculations, probabilities, protocols, efficiency."),
    "mrrshan":    ("мррашаны", "Mrrshan", "Feline warrior race. Hunters' pride, predator and hunt metaphors, honour in battle."),
    "psilons":    ("псилоны", "Psilons", "Race of scientists. Intellectual superiority, data, hypotheses and experiments; condescending to lesser minds."),
    "sakkra":     ("саккра", "Sakkra", "Reptilian breeders. Territorial, aggressive expansion, the swarm and the brood; hissing menace."),
    "silicoids":  ("силикоиды", "Silicoids", "Crystalline rock-eaters. Slow, alien, indifferent; geological time scales, stone and crystal."),
    "trilarians": ("трилариане", "Trilarians", "Aquatic race. Calm and mysterious; oceans, currents, tides and depths."),
}

PERSONALITIES = {
    "xenophobic": "distrusts all aliens, cold, wants as little contact as possible",
    "ruthless":   "cynical and calculating, any means are fine, no sentiment",
    "aggressive": "belligerent, quick to threaten, values strength above all",
    "erratic":    "unpredictable, sudden mood swings, odd turns of phrase",
    "honorable":  "keeps its word, formal, values honesty and agreements",
    "pacifistic": "prefers peace, gentle, reluctant to fight",
}

# Situations: the engine has already decided; the line only expresses the decision.
# check: which consistency check applies to the text.
SITUATIONS = {
    "first_contact":  ("Greet the other empire at first contact and show your attitude toward them.", None),
    "reject_trade":   ("REJECT the trade treaty they propose.", "reject"),
    "accept_peace":   ("ACCEPT the peace treaty they propose; the war between you ends.", "peace"),
    "declare_war":    ("DECLARE WAR on them, now.", "war"),
    # The placeholder carries the amount WITH the currency ("200 BC"): the Russian pattern has no Latin letters.
    "demand_tribute": ("DEMAND a tribute of {AMOUNT} from them, or else (the engine puts the sum with the currency in place of the placeholder).", "amount"),
    "caught_spy":     ("PROTEST: you caught one of their spies in your empire.", "spy"),
    # Full bank (not in the pilot yet)
    "accept_trade":     ("ACCEPT the trade treaty they propose.", "accept"),
    "accept_research":  ("ACCEPT the research treaty they propose.", "accept"),
    "reject_research":  ("REJECT the research treaty they propose.", "reject"),
    "accept_alliance":  ("ACCEPT the alliance they propose.", "accept"),
    "reject_alliance":  ("REJECT the alliance they propose.", "reject"),
    "accept_nap":       ("ACCEPT the non-aggression pact they propose.", "accept"),
    "reject_peace":     ("REJECT the peace they ask for; the war goes on.", "reject"),
    "break_treaty":     ("BREAK your treaty with them.", "treaty"),
    "threaten":         ("THREATEN them: stop what they are doing or face consequences.", None),
    "thank_gift":       ("THANK them for a gift they sent.", "gift"),
    "council_vote_for": ("Tell them you will VOTE FOR them in the Galactic Council.", "council"),
    "council_vote_against": ("Tell them you will VOTE AGAINST them in the Galactic Council.", "council"),
}
PILOT_RACES = ["psilons", "klackons"]
PILOT_SITUATIONS = ["first_contact", "reject_trade", "accept_peace", "declare_war", "demand_tribute", "caught_spy"]

PLACEHOLDER = {"ru": "{СУММА}", "en": "{AMOUNT}"}
PATTERN = {
    "ru": "^[А-Яа-яЁё0-9 .,:;!?()«»—–…%{}-]*$",
    "en": "^[A-Za-z0-9 .,:;!?()'—–…%{}-]*$",
}

SYSTEM = """You write the lines that alien leaders say to another empire's ruler on the diplomacy screen of Master of Orion II.
The game engine has ALREADY made the decision. Each line must express exactly that decision and never change it.
Each line: one or two sentences, spoken directly to the listener. Stay in the voice of the race and the personality.
No stage directions, no narration, no quotation marks, no names of the listener's race, no invented numbers or planets.
Write 3 clearly different variants. Answer with JSON only."""


def user_prompt(race, pers, sit, lang):
    ru_name, en_name, style = RACES[race]
    decision, _ = SITUATIONS[sit]
    ph = PLACEHOLDER[lang]
    decision = decision.replace("{AMOUNT}", ph)
    if lang == "ru":
        lang_rule = (f"Write in RUSSIAN. Address the listener as «вы». The speaker's race in Russian: {ru_name}. "
                     f"If the decision has the placeholder {ph}, copy it exactly once, as is; otherwise use no braces.")
    else:
        lang_rule = (f"Write in ENGLISH. Address the listener as 'you'. "
                     f"If the decision has the placeholder {ph}, copy it exactly once, as is; otherwise use no braces.")
    return (f"Speaker: the {en_name.upper()}. {style}\n"
            f"Personality: {pers.upper()} - {PERSONALITIES[pers]}.\n"
            f"ENGINE DECISION: {decision}\n"
            f"{lang_rule}")


def schema(lang):
    return {"type": "object", "additionalProperties": False, "required": ["variants"],
            "properties": {"variants": {"type": "array", "minItems": 3, "maxItems": 3,
                                        "items": {"type": "string", "pattern": PATTERN[lang]}}}}


# ---------------------------------------------------------------------------
# Automatic flags (a flagged line is not dropped: it goes to review)

RX = {
    "ru": {"accept": r"(?<!не )\b(принимаем|согласны|соглашаемся|с радостью)",
           "reject_ok": r"\bне\b|\bнет\b|отказ|откаж|отверг|отклон|никогда",
           "peace": r"\bмир", "war": r"войн", "spy": r"шпион|агент|лазутчик|соглядата",
           "narration": r"\b(говорит|рычит|шипит|произносит|восклицает)\b|\*",
           "we": r"\b(мы|нас|нам|нами|наш\w*)\b",
           "accept_ok": r"принима|приня|соглас|соглаш|\bда\b|с радостью|одобр",
           "council": r"совет|голос", "treaty": r"договор|пакт|соглашени|союз",
           "gift": r"спасиб|благодар|\bдар|подар"},
    "en": {"accept": r"\b(we accept|we agree|gladly accept|we welcome your)\b",
           "reject_ok": r"\b(no|not|never|cannot|reject|rejected|rejects|refuse|refused|refuses|decline|declined|declines|denied)\b|n't\b",
           "peace": r"\bpeace", "war": r"\bwar\b", "spy": r"\b(spy|spies|agent|infiltrator)",
           "narration": r"\b(says|snarls|hisses|growls|exclaims)\b|\*",
           "we": r"\b(we|us|our|ours)\b",
           "accept_ok": r"\b(yes|agree|agreed|agrees|accept|accepted|accepts|gladly)\b",
           "council": r"council|vot", "treaty": r"treaty|pact|agreement|alliance",
           "gift": r"thank|gift|grateful"},
}


def flags_for(text, race, sit, lang):
    rx = RX[lang]
    low = text.lower()
    f = []
    if not 15 <= len(text) <= 220:
        f.append(f"length {len(text)}")
    check = SITUATIONS[sit][1]
    ph = PLACEHOLDER[lang]
    if check == "amount":
        if text.count(ph) != 1:
            f.append("placeholder missing or repeated")
    elif "{" in text or "}" in text:
        f.append("stray braces")
    if check == "reject" and (re.search(rx["accept"], low) or not re.search(rx["reject_ok"], low)):
        f.append("does not read as a refusal")
    if check == "accept" and not re.search(rx["accept_ok"], low):
        f.append("does not read as an acceptance")
    if check == "council" and not re.search(rx["council"], low):
        f.append("no word for council or vote")
    if check == "treaty" and not re.search(rx["treaty"], low):
        f.append("no word for treaty")
    if check == "gift" and not re.search(rx["gift"], low):
        f.append("no thanks or gift")
    if check == "peace" and not re.search(rx["peace"], low):
        f.append("no word for peace")
    if check == "war" and not re.search(rx["war"], low):
        f.append("no word for war")
    if check == "spy" and not re.search(rx["spy"], low):
        f.append("no word for spy")
    if re.search(rx["narration"], low):
        f.append("narration")
    if race == "klackons" and not re.search(rx["we"], low):
        f.append("hive does not say 'we'")
    return f


def jaccard(a, b):
    wa, wb = set(re.findall(r"\w+", a.lower())), set(re.findall(r"\w+", b.lower()))
    return len(wa & wb) / max(1, len(wa | wb))


# ---------------------------------------------------------------------------

def generate(port, model_key, job, max_tokens):
    bench = _bench()
    race, pers, sit, lang = job
    key = f"{race}/{pers}/{sit}/{lang}"
    task = {"system": SYSTEM, "user": user_prompt(race, pers, sit, lang), "schema": schema(lang)}
    seed = zlib.crc32(key.encode()) & 0x7FFFFFFF
    res = bench.chat(port, model_key, task, seed, max_tokens)
    out = []
    if "error" in res:
        return [{"key": key, "race": race, "personality": pers, "situation": sit, "lang": lang,
                 "variant": -1, "text": "", "flags": ["error: " + res["error"][:200]], "wall_s": res.get("wall_s")}]
    try:
        variants = bench.parse_answer(res["content"])["variants"]
    except Exception as e:
        variants = []
        out.append({"key": key, "race": race, "personality": pers, "situation": sit, "lang": lang,
                    "variant": -1, "text": res.get("content", "")[:300], "flags": [f"bad json: {e}"], "wall_s": res["wall_s"]})
    for i, v in enumerate(variants):
        v = v.strip()
        fl = flags_for(v, race, sit, lang)
        for j in range(i):
            if jaccard(v, variants[j]) > 0.6:
                fl.append(f"near-duplicate of #{j}")
        out.append({"key": key, "race": race, "personality": pers, "situation": sit, "lang": lang,
                    "variant": i, "text": v, "flags": fl, "seed": seed, "wall_s": round(res["wall_s"], 1),
                    "tokens": (res.get("timings") or {}).get("predicted_n")})
    return out


def write_review(lines, path, model_label):
    by = {}
    for ln in lines:
        by.setdefault((ln["race"], ln["situation"]), []).append(ln)
    total = len([l for l in lines if l["variant"] >= 0])
    flagged = len([l for l in lines if l["flags"]])
    rv = [f"# Банк реплик — пилот ({model_label})\n",
          f"Строк: {total}, с пометками: {flagged}. ⚑ — автоматическая пометка (строка не удалена).\n"]
    for (race, sit), group in sorted(by.items()):
        rv.append(f"\n## {RACES[race][1]} — {sit}\n")
        for lang in ("ru", "en"):
            rv.append(f"**{lang}**\n")
            for ln in sorted((g for g in group if g["lang"] == lang), key=lambda x: (x["personality"], x["variant"])):
                mark = (" ⚑ " + "; ".join(ln["flags"])) if ln["flags"] else ""
                rv.append(f"- *{ln['personality']}* — {ln['text']}{mark}")
            rv.append("")
    open(path, "w", encoding="utf-8").write("\n".join(rv))


def main():
    bench = _bench()
    ap = argparse.ArgumentParser()
    ap.add_argument("--pilot", action="store_true")
    ap.add_argument("--races", default="")
    ap.add_argument("--situations", default="")
    ap.add_argument("--langs", default="ru,en")
    ap.add_argument("--model", default="qwen35-4b", help="catalog key, optionally with ';'-separated overrides")
    ap.add_argument("--port", type=int, default=8092)
    ap.add_argument("--ctx", type=int, default=8192)
    ap.add_argument("--np", type=int, default=4)
    ap.add_argument("--fit-target", type=int, default=256)
    ap.add_argument("--max-tokens", type=int, default=450)
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    args.extra = []

    races = PILOT_RACES if args.pilot and not args.races else (list(RACES) if args.races in ("", "all") else args.races.split(","))
    sits = PILOT_SITUATIONS if args.pilot and not args.situations else (list(SITUATIONS) if args.situations in ("", "all") else args.situations.split(","))
    langs = args.langs.split(",")
    jobs = [(r, p, s, l) for r in races for p in PERSONALITIES for s in sits for l in langs]

    cat = bench.load_catalog()
    key, ov, label = bench.parse_spec(args.model)
    out_dir = args.out or os.path.join(HERE, "out", dt.datetime.now().strftime("%Y%m%d-%H%M") + ("-pilot" if args.pilot else "") + "-" + label)
    os.makedirs(out_dir, exist_ok=True)
    print(f"{len(jobs)} requests x 3 variants -> {out_dir}", flush=True)

    proc, logf, cmd, reasoning = bench.start_server(cat, key, ov, args, os.path.join(out_dir, "server.log"))
    lines = []
    t0 = dt.datetime.now()
    try:
        bench.wait_ready(args.port, proc)
        with cf.ThreadPoolExecutor(args.np) as ex, open(os.path.join(out_dir, "lines.jsonl"), "w", encoding="utf-8") as f:
            futs = [ex.submit(generate, args.port, key, j, args.max_tokens) for j in jobs]
            for n, fut in enumerate(cf.as_completed(futs), 1):
                for ln in fut.result():
                    ln["model"] = label
                    lines.append(ln)
                    f.write(json.dumps(ln, ensure_ascii=False) + "\n")
                if n % 12 == 0 or n == len(jobs):
                    print(f"  {n}/{len(jobs)} requests, {(dt.datetime.now() - t0).seconds} s", flush=True)
    finally:
        bench.stop_server(proc, logf)
    write_review(lines, os.path.join(out_dir, "review.md"), label)
    ok = [l for l in lines if l["variant"] >= 0]
    print(f"done: {len(ok)} lines, {sum(1 for l in ok if l['flags'])} flagged, {(dt.datetime.now() - t0).seconds} s", flush=True)


if __name__ == "__main__":
    main()
