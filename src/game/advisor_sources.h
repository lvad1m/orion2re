#pragma once

#include <string>

#include "game/advisor_directive.h"
#include "game/advisor_view.h"

// Directive sources, asked in order by advisor::Run_Turn (rules, then LLM). A source returns
// true and fills `out` when it has a directive for the empire in `view`; false means "nothing
// to say". `note` receives a short explanation for the journal (which rule fired, on which
// numbers). Sources only read the view; they never touch game state or game_random.
namespace advisor {

    bool Rules_Directive(const s_empire_view* view, s_directive* out, std::string* note);
    bool Llm_Directive(const s_empire_view* view, s_directive* out, std::string* note);

} // namespace advisor
