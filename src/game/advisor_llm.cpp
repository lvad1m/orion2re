#include "pch.h"

#include "game/advisor_sources.h"

// Stub. Future asynchronous flow (docs/LocalLLM/README.md, section 4.2):
// - at the end of turn N the advisor sends one request per computer empire (the formatted
//   empire summary) to the local model and does not wait for the answer;
// - the answers are parsed and validated (Validate_Directive) whenever they arrive and held
//   as pending directives;
// - at the start of turn N+1 Run_Turn applies the pending directives, so the game never blocks
//   on the model and the turn does not depend on how fast the model answered;
// - applied directives are recorded in the save (optional advisor section), so a replay or a
//   multiplayer client reads them from the record and never asks the model again.
// Stage 1 has no networking and no model: the source always reports "nothing to say".
namespace advisor {

    bool Llm_Directive(const s_empire_view*, s_directive*, std::string*) {
        return false;
    }

} // namespace advisor
