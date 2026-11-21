# Self-play bench

Compares advisor variants on AI-only games. Design and results: `docs/LocalLLM/README.md`,
sections 10.3 and 10.4 (results), 10.5 (roadmap).

## What it needs

- a **debug** build of `orion2re` (the turn oracle switches exist only in debug builds),
  copied into a folder with the original game data;
- PowerShell 7 and Python 3.

## Run

```powershell
pwsh tools/selfplay/selfplay.ps1 -GameDir D:\bench\game -OutDir D:\bench\run1 -Seeds 1..8 -Difficulties 2,3
python tools/selfplay/report.py D:\bench\run1
```

`selfplay.ps1` records one game per variant, seed and difficulty with

```text
orion2re /config selfplay-<variant>.cfg /oracle-record <dir> /oracle-turns 100 /oracle-seed <n> /oracle-difficulty <0-4>
```

and keeps only the advisor journal (`d<difficulty>-s<seed>-<variant>.jsonl`). Variants are config
lines appended to a copy of `data/orion2re.cfg`; pass your own with `-Variants @{ name = "lines" }`.

Games start minimized in the taskbar without taking the keyboard focus (`SW_SHOWMINNOACTIVE`, through
`CreateProcess`: PowerShell's `Start-Process -WindowStyle Minimized` still activates the window), so a
long bench does not interrupt other work; `-Visible` starts them as usual.

The default variants:

| Variant | Config | Meaning |
|---|---|---|
| `observe` | advisor on, every mechanic off | plays exactly like the original AI (the advisor never draws from the game RNG) but writes the journal: the baseline |
| `advisor` | advisor on with defaults | everything the rules advisor does |

Head-to-head variants are named `h2h-<mask>` (optionally with a suffix, `h2h-21-nowar`) and set
`advisor active_players = <mask>`: only those player slots are advised. `report.py` then prints how
much of the galaxy's colonies, population and industry the advised slots hold compared with the
same slots in the baseline game of the same seed. Use two complementary masks (for example 21 =
players 1, 3, 5 and 10 = players 2, 4) so that start positions even out.

`report.py` prints, per difficulty, the mean over seeds of the final-turn state summed over all
empires (colonies including outposts, outposts, population, industry, research, technologies,
spies abroad, treasury, fleet strength) and, for every variant, the colony difference against the
baseline seed by seed. Head-to-head shares are given for all colonies, populated colonies,
population and industry.

## Startup switches (debug builds)

| Switch | Meaning |
|---|---|
| `/oracle-record <dir>` | play AI-only turns from a new game and store every turn's save |
| `/oracle-turns <n>` | number of turns to record (1-100000, default 100) |
| `/oracle-seed <n>` | game seed of the new game (1-2147483647, default 1) |
| `/oracle-difficulty <0-4>` | tutor to impossible; default: the configured difficulty |
| `/oracle-tactical` | record with tactical-combat settings (`selfplay.ps1 -Tactical`): computer players design ships for tactical combat; battles between computer players are still resolved strategically |
| `/oracle-replay <dir>` | replay a recorded history and compare the saves byte for byte |
