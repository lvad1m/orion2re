#include "pch.h"

#include "game/debug_overlay.h"
#include "game/mentat.h"
#include "game/texts.h"

#if defined(ORION2RE_ENABLE_DEBUG_OVERLAY)
#include <imgui.h>

// The mentat's window (docs/LocalLLM/mentat.md section 4): a Dear ImGui window of the overlay that works with the debugger
// off. F9 toggles it (debug_overlay.cpp), it opens itself as the settings say (mentat::Take_Open_Request), it can be
// moved, scrolled, closed with its X and collapsed to a one-line bar "Mentat: N new". It only draws what mentat.cpp
// built; it changes nothing in the game. The window's place and state are kept in memory for the session only.
namespace mentat {

    namespace {
        bool g_collapsed = false;
        bool g_switch_place = false;
        ImVec2 g_place = ImVec2(8.0f, 8.0f);

        constexpr ImVec4 k_threat_color = ImVec4(1.0f, 0.55f, 0.45f, 1.0f);
        constexpr ImVec4 k_head_color = ImVec4(0.55f, 0.8f, 1.0f, 1.0f);

        bool Take_Open_() {
            if (!Take_Open_Request()) {
                return false;
            }
            g_collapsed = false;  // a situation that calls for attention opens the whole window
            g_switch_place = true;
            return true;
        }

        void Wrapped_(const std::string& text) {
            ImGui::TextWrapped("%s", text.c_str());
        }

        void Bullet_(const std::string& text, const ImVec4* color = nullptr) {
            ImGui::Bullet();
            ImGui::SameLine();
            if (color != nullptr) {
                ImGui::PushStyleColor(ImGuiCol_Text, *color);
            }
            Wrapped_(text);
            if (color != nullptr) {
                ImGui::PopStyleColor();
            }
        }

        std::string Label_(const char* key, const char* id) {
            return std::string(texts::Get_Utf8(key)) + "###" + id;
        }

        void Draw_Bar_(int32_t new_count) {
            ImGui::SetNextWindowPos(g_place, g_switch_place ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            g_switch_place = false;
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
            if (ImGui::Begin("###mentat_bar", nullptr, flags)) {
                const std::string text = new_count > 0
                    ? Utf8(s_text{"mentat.collapsed", {{"count", std::to_string(new_count)}}})
                    : std::string(texts::Get_Utf8("mentat.collapsed_none"));
                if (ImGui::Button((text + "###mentat_expand").c_str())) {
                    g_collapsed = false;
                    g_switch_place = true;
                }
                g_place = ImGui::GetWindowPos();
            }
            ImGui::End();
        }

        void Draw_Summary_(const s_summary& summary) {
            const ImVec4 head = k_head_color;
            const bool several = summary.sections.size() > 1;
            if (ImGui::CollapsingHeader(Label_("mentat.section.summary", "mentat_h_summary").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const s_section& section : summary.sections) {
                    if (several) {
                        ImGui::PushStyleColor(ImGuiCol_Text, head);
                        ImGui::TextUnformatted(texts::Get_Utf8(section.title_key.c_str()));
                        ImGui::PopStyleColor();
                    }
                    for (const s_text& line : section.lines) {
                        Bullet_(Utf8(line));
                    }
                }
            }
        }

        void Draw_Threats_(const s_summary& summary) {
            if (ImGui::CollapsingHeader(Label_("mentat.section.threats", "mentat_h_threats").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (summary.alerts.empty()) {
                    Wrapped_(texts::Get_Utf8("mentat.none_threats"));
                }
                for (const s_alert& alert : summary.alerts) {
                    Bullet_(Utf8(alert.text), &k_threat_color);
                }
            }
        }

        void Draw_Window_(const debug_overlay::s_panel_context& context) {
            (void)context;
            const s_summary* summary = Current();
            const int32_t new_count = New_Count();
            if (g_collapsed) {
                Draw_Bar_(new_count);
                return;
            }
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(g_place, g_switch_place ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
            g_switch_place = false;
            ImGui::SetNextWindowSize(ImVec2(std::min(380.0f, io.DisplaySize.x - 16.0f), std::min(320.0f, io.DisplaySize.y - 16.0f)), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 80.0f), ImVec2(io.DisplaySize.x, io.DisplaySize.y));
            bool open = true;
            if (ImGui::Begin(Label_("mentat.title", "mentat_window").c_str(), &open)) {
                g_place = ImGui::GetWindowPos();
                if (ImGui::Button(Label_("mentat.button.collapse", "mentat_collapse").c_str())) {
                    g_collapsed = true;
                    g_switch_place = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", texts::Get_Utf8("mentat.hint"));
                ImGui::Separator();
                if (summary == nullptr) {
                    Wrapped_(texts::Get_Utf8("mentat.waiting"));
                } else {
                    Mark_Seen();
                    Draw_Summary_(*summary);
                    Draw_Threats_(*summary);
                    if (summary->advice_pending && ImGui::CollapsingHeader(Label_("mentat.section.advice", "mentat_h_advice").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        Wrapped_(texts::Get_Utf8("mentat.advice_later"));
                    }
                    if (summary->detail >= DETAIL_DETAILED && ImGui::CollapsingHeader(Label_("mentat.section.why", "mentat_h_why").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const s_text& line : summary->why) {
                            Bullet_(Utf8(line));
                        }
                    }
                }
            }
            ImGui::End();
            if (!open) {
                debug_overlay::Set_Mentat_Visible(false);
            }
        }

        struct s_registration {
            s_registration() {
                debug_overlay::Register_Mentat_Window(Available, Take_Open_, Draw_Window_);
            }
        } g_registration;
    } // namespace

} // namespace mentat
#endif
