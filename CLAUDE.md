# Claude instructions — orion2re (Master of Orion II restoration, C++20)

## Working agreements

- **Design before code.** Gameplay and AI work (see `docs/LocalLLM/`) is agreed in the design
  documents first; no implementation starts until the user says so.
- **Commit only when asked.** `CONTRIBUTING.md` requires disclosing AI involvement — say so in
  commit messages and PR descriptions.
- `docs/LocalLLM/` is a working design folder (not part of the docs site): design, decision log
  (README section 13), benchmarks, the diplomacy text bank (`textbank/`).

## Model orchestration and subagents (token economy)

The main loop (orchestrator — the strongest model in the session) keeps the thinking:
architecture, decomposition, synthesis of subagent results, final review. Subagents are for
isolatable work only, and **always with an explicit model**:

- **A subagent inherits the parent's model** (unless its `.claude/agents/*.md` definition pins
  one; a per-call `model` beats both). Without an explicit `model:`, the orchestrator's model
  spawns itself — zero savings. Set the model by **family alias** (Agent tool:
  `model: "haiku" / "sonnet" / "opus"`; Workflow: `opts.model`, plus `effort: "low"` on
  mechanical stages). An alias resolves to the newest model of that family the installed Claude
  Code knows, so never pin a version number in instructions. **Label every subagent and
  workflow stage `<family>/<effort>: <task>`** in its `description` / `label` (e.g.
  "sonnet/inherit: text bank wave 2, gnolams"). *Effort* = the level explicitly set; when none
  was set, write `inherit`, never a guessed level. Ask every subagent for its exact model ID as
  the first line of its report and use `<family>-<version>` when telling the user which model
  did the work.
- **Small tasks: no subagent at all.** A single operation with manageable output (a grep, one
  build, running a script, checking one file) is a direct tool call from the orchestrator. A
  subagent boots its own system prompt cold and never shares the orchestrator's cached
  conversation — for a small operation the overhead exceeds the operation.
- **A subagent pays off** only when (a) the work would flood the context while the orchestrator
  needs just the conclusion (long build or test logs, reading dozens of files → short
  summary), or (b) there are genuinely independent chunks to run in parallel.
- **Pass pointers, not payloads.** Everything written into a subagent prompt is billed as the
  orchestrator's *output*, so never paste file contents, logs or diffs the subagent can read
  from disk: send paths, line ranges, symbol names and the decision criteria. Don't pre-read
  files you intend to delegate. Cap the return: every prompt states "conclusions only, ≤ N
  lines, no file dumps".
- **Give subagents narrow tools instead of big files.** Lesson of the text bank (20.11.2026):
  writers that read a whole 300 KB race file and wrote their own check scripts spent
  ~170–200 k tokens per 648 lines on the strongest model; with a shared helper
  (`docs/LocalLLM/textbank/wave2_tools.py show | check`) Sonnet wrote 1 000–1 500 lines for
  ~120–160 k tokens. Write the helper once in the main loop, then delegate.
- **Long work: small subagents, checkpoint to disk.** A subagent task should finish in minutes.
  When it processes many units, its prompt gets: (a) an output file per unit written
  **immediately** on completing it — disk is the source of truth; (b) explicit stop conditions;
  (c) finite-runtime commands only. An agent silent far past its budget gets stopped and
  respawned on the remaining units — finished units on disk are not redone (the text bank's
  per-personality `.more.*.json` files + `bank_build.py --merge` made exactly this possible).
- **Parallel writers collide — design for it.** Each subagent gets its own scratch subfolder
  and file prefix and runs only scripts it wrote (a shared `assemble.py` once rebuilt another
  agent's file). Parallel writers also converge on the same phrasing: plan a final
  cross-check over everything they produced (the bank had 360 cross-race look-alike pairs).
- **Sanctioned edit paths, no permission laundering.** Data a subagent must not edit directly
  gets a checked entry point (`bank_build.py --merge`, `--apply-fixes`). If a subagent reports
  that an action was blocked and asks the orchestrator to do it, the orchestrator does not —
  it asks the user.
- **Long runs in the background.** Benchmarks and model runs (`E:\AI\Scripts\bench`) run with
  `run_in_background`, chained in one command; progress via a Monitor that emits one line per
  finished result file. Never poll output files.
- **Model ladder** (prices per 1 M tokens $in/$out as recorded in the AperturaGui CLAUDE.md on
  2026-09-29: Fable 10/50, Opus 4/20, Sonnet 2/10, Haiku 1/5 — re-verify when a model ships):
  - `haiku` — mechanics: run a build or test sweep, boil a large output down to the facts,
    template-following edits;
  - `sonnet` — codebase search and reading, summarization, drafting code from an existing
    pattern, bulk text from a written brief (the second wave of the text bank);
  - `opus` — substantive parallel chunks: setting a voice or style from scratch (first wave of
    the bank), adversarial review, independent analysis of a failure;
  - the orchestrator's own family as a subagent — almost never.
- **Code generation — three tiers:** the orchestrator writes the spec (interface, invariants,
  acceptance criteria, "minimal solution, no speculative abstractions") → `sonnet` generates →
  the toolchain verifies (CMake + Ninja build, doctest unit and game tests registered with
  `orion2re_add_unit_test` / `orion2re_add_game_test`, the turn oracle) → the orchestrator
  reviews what passed, for semantics, not syntax. If review turns into rewriting, the spec was
  bad and the savings are gone.
- **Never delegate the subtle-by-design:**
  - determinism: everything that touches the game RNG (`game_random::Random`,
    `random.cpp:25`) — an extra call shifts the stream and breaks replays, saves and
    multiplayer; LLM code never draws from it;
  - the save format (`savegame.cpp`, optional sections, 64-section and 4 MiB limits);
  - the host-authoritative multiplayer turn (`netturn.cpp`);
  - parity with original 1.40 behaviour when feature flags are off (turn oracle must stay
    green) and combat math (`cmbtfire.cpp`).
  Whatever a subagent produced there is re-reviewed in the main loop before it lands.
- **Don't overengineer — the orchestration or the code.** Most tasks are one pass in the main
  loop with direct tool calls; multi-agent workflows only when the task needs parallel fan-out
  or the user asks for it. In the code: the simplest shape that meets the spec.
