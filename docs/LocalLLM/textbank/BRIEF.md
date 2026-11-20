# Brief: writing the diplomacy text bank for one race

> In the repository this folder is `docs/LocalLLM/textbank`; the paths below point to the
> original workshop `E:\AI\Scripts\textbank`, where the bank was written.

Project: **orion2re**, a fork that restores and extends Master of Orion II (1996). AI empires
talk to the player on the diplomacy screen. The game engine has **already decided** what the
leader does (reject a treaty, declare war, ...); a line in this bank only **says it**, in the
voice of the race and its personality. The bank ships with the game as data, so machines
without a GPU still get varied, characterful speech.

## Grid for one race

6 personalities × 18 situations × 3 variants × 2 languages (ru, en) = 648 lines.

File: `E:\AI\Scripts\textbank\bank\<race>.json`, UTF-8, exactly this structure:

```json
{"race": "sakkra",
 "lines": {
  "xenophobic": {
   "first_contact": {"ru": ["...", "...", "..."], "en": ["...", "...", "..."]},
   "...": {}
  },
  "...": {}
 }}
```

Reference for quality and tone: `bank\psilons.json` and `bank\klackons.json` (6 situations each,
written first). Read them before you start.

## Personalities

| Key | Voice |
|---|---|
| xenophobic | distrusts all aliens, cold, wants as little contact as possible |
| ruthless | cynical and calculating, any means are fine, no sentiment |
| aggressive | belligerent, quick to threaten, values strength above all |
| erratic | unpredictable, sudden mood swings, odd turns of phrase |
| honorable | keeps its word, formal, values honesty and agreements |
| pacifistic | prefers peace, gentle, reluctant to fight |

## Situations (the decision the line must express)

| Key | The engine decided | The line must contain (checked) |
|---|---|---|
| first_contact | greet the other empire at first contact, show your attitude | — |
| accept_trade | ACCEPT the trade treaty they propose | an acceptance |
| reject_trade | REJECT the trade treaty they propose | a clear refusal |
| accept_research | ACCEPT the research treaty they propose | an acceptance |
| reject_research | REJECT the research treaty they propose | a clear refusal |
| accept_alliance | ACCEPT the alliance they propose | an acceptance |
| reject_alliance | REJECT the alliance they propose | a clear refusal |
| accept_nap | ACCEPT the non-aggression pact they propose | an acceptance |
| accept_peace | ACCEPT the peace they propose; the war ends | the word peace |
| reject_peace | REJECT the peace they ask for; the war goes on | a clear refusal |
| declare_war | DECLARE WAR on them, now | the word war |
| break_treaty | BREAK our treaty with them | treaty / pact / agreement / alliance |
| threaten | THREATEN them: stop what you are doing or face consequences | — |
| demand_tribute | DEMAND a tribute, or else | the placeholder, exactly once |
| thank_gift | THANK them for a gift they sent | thanks or gift |
| caught_spy | PROTEST: we caught one of their spies in our empire | spy / agent |
| council_vote_for | tell them we will VOTE FOR them in the Galactic Council | council or vote |
| council_vote_against | tell them we will VOTE AGAINST them in the Galactic Council | council or vote |

What the automatic check looks for (`textbank.py`, `flags_for`):

- refusal — ru: `не`, `нет`, `отказ…`, `отверг…`, `отклон…`, `никогда`, and **no**
  «принимаем / согласны / соглашаемся / с радостью»; en: `no`, `not`, `never`, `reject`,
  `refuse`, `decline`, `denied`, `n't`, and no "we accept / we agree / gladly accept / we welcome your";
- acceptance — ru: `принима…`, `принят…`, `соглас…`, `да`, `с радостью`, `одобр…`;
  en: yes, agree(d), accept(ed/s), gladly;
- peace — ru `мир…`, en `peace`; war — ru `войн…`, en `war`; spy — ru `шпион…`, `агент…`,
  `лазутчик…`, en spy / spies / agent / infiltrator; council — ru `совет…`, `голос…`, en council /
  vote; treaty — ru `договор…`, `пакт…`, `соглашени…`, `союз…`, en treaty / pact / agreement /
  alliance; gift — ru `спасиб…`, `благодар…`, `дар…`, `подар…`, en thank / gift / grateful.

## Rules for every line

1. One or two sentences, 15–220 characters, spoken **directly to the listener**.
2. Express exactly the engine decision; never soften it into its opposite. A refusal is a
   refusal even when polite; an acceptance is an acceptance even when grudging.
3. The personality must be audible (a pacifist declares war with regret, a ruthless empire
   accepts peace because it pays, an erratic one changes its mind mid-sentence, ...).
4. Never name the listener's race or leader; say "you". Russian: address as «вы».
5. No stage directions, no narration ("he snarls", «шипит»), no asterisks, no quotation marks,
   no numbers, no invented planets, systems or people.
6. `demand_tribute` only: the placeholder exactly once — ru `{СУММА}`, en `{AMOUNT}`. It
   already contains the sum **with the currency** ("200 BC"), so never write BC or a currency
   after it. No braces anywhere else.
7. Alphabet (enforced in the game by a JSON-schema pattern):
   - ru: Cyrillic letters, digits, space and `. , : ; ! ? ( ) « » — – … % -` only — **no Latin
     letters at all**, no straight quotes;
   - en: Latin letters, digits, space and `. , : ; ! ? ( ) ' — – … % -` only, no double quotes.
8. Three variants per cell must differ in idea or imagery, not only in wording.
9. Russian and English are written **independently**, each natural in its own language — not
   translations of each other. Russian must be literate: correct cases and agreement, no
   calques from English.
10. Russian names of races decline badly in templates: prefer referring to yourselves with "мы"
    and a common noun of your own (улей, Конклав, Империя, стая, Доминион, ...). Do not
    inflect your own race name in oblique cases.
11. Original wording only: do not quote or paraphrase text from the original game.
12. Klackons only: always speak as "we" (мы / нас / наш…; we / us / our).

## Process

1. Read this brief, `bank\psilons.json`, `bank\klackons.json`, and `textbank.py` (RACES has
   the one-line style note of every race).
2. Write `bank\<race>.json` (all 6 × 18 cells, 3 ru + 3 en lines each). Writing it
   personality by personality is fine.
3. Check: `cd E:\AI\Scripts\textbank` then `PYTHONIOENCODING=utf-8 python bank_build.py --race <race>`.
   Fix every real problem. A flag that is a false positive may stay — list it in your report.
4. Do not edit any other file (other races, `textbank.py`, `bank_build.py`). Writers work in
   parallel and may share a scratch folder: keep your drafts and scripts in a subfolder named
   after your race, give them `<race>_` names, and run only scripts you wrote yourself.
5. Compare your lines with the other races' banks too (same situation, word overlap ≤ 0.6), so
   two races never say the same thing.
6. Report back: number of lines, structure problems left (should be none), flags left and why
   each is a false positive, and anything worth knowing about the race's voice.

## Wave 2: from 3 to 10 variants per cell

The bank target is **10 variants per cell and language** (6 × 18 × 10 × 2 = 2160 lines per
race), so a player almost never hears the same line twice. Wave 2 adds **7 new variants** to
every cell that has 3.

1. Re-read your race file first: the new variants must not repeat the existing ones in idea or
   imagery (the check flags pairs with more than 60 % shared words, across all 10).
2. Do not edit `bank\<race>.json` directly (it is large). Write the new lines into separate
   files with the same structure, one per personality:
   `bank\<race>.more.<personality>.json` →
   `{"race": "<race>", "lines": {"<personality>": {"<situation>": {"ru": [7 lines], "en": [7 lines]}}}}`
3. Merge and check: `PYTHONIOENCODING=utf-8 python bank_build.py --merge <race>`. It appends
   the new variants to `bank\<race>.json` (exact duplicates are skipped), renames the merged
   files to `*.merged`, and prints the flags. Fix real problems in `bank\<race>.json` with small
   edits (or write a fixing script). The run ends with `0 cells below 10 variants`.
4. All rules above still apply. Vary the angle: short and long lines, a question, an aside,
   an image from the race's world, a reference to the relationship so far, a warning, irony.
