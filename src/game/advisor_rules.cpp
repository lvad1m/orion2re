#include "pch.h"

#include "game/advisor_sources.h"

namespace advisor {

    // Stage 1: the rule-based source has no rules yet (stage 2).
    bool Rules_Directive(const s_empire_view*, s_directive*, std::string*) {
        return false;
    }

} // namespace advisor
