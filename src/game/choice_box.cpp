#include "pch.h"

#include "game/choices.h"
#include "game/gendraw.h"

// The window of a choice (docs/LocalLLM/human-ui.md section 5): a dimmed screen, a panel of PANEL_WIDTH pixels
// centred on it with the title, the body as a formatted paragraph and the options as lines "1. ..." to "3. ...",
// each a clickable field with the hotkey of its number. Modelled on Confirmation_Box (gendraw.cpp): the same
// startup and cleanup, the same font colours, the body font shrinks from 4 to 2 until the body fits
// BODY_MAX_HEIGHT. A body of 400 characters takes about 8 lines at font 4 and the panel then stays under 330
// pixels high; 3 option lines of 60 characters take one or two lines each.
namespace GENDRAW {

    namespace {
        constexpr int16_t PANEL_WIDTH = 420;
        constexpr int16_t MARGIN = 16;
        constexpr int16_t TITLE_HEIGHT = 34;
        constexpr int16_t BODY_MAX_HEIGHT = 190;
        constexpr int16_t OPTION_GAP = 6;
        constexpr int16_t SCREEN_WIDTH = 640;
        constexpr int16_t SCREEN_HEIGHT = 480;
    } // namespace

    int16_t Choice_Box(const choices::s_text& text) {
        const int32_t count = std::clamp<int32_t>(text.count, 1, 3);
        uint8_t color_1;
        uint8_t color_2;
        s_colors font_colors;

        TEXTBOX::Get_Mox_Text_Box_Base_Colors(2, &color_1, &color_2);
        Message_Box_Startup();

        const int16_t inner = PANEL_WIDTH - 2 * MARGIN;

        // The body font: the largest that fits.
        int16_t font_idx = 4;
        int16_t body_height = 0;
        do {
            font_colors = *MISC::Font_Colors2(font_idx, color_1, color_2);
            fonts::Set_Remap_Font_Style(font_idx, &font_colors);
            fonts::Set_Font_LF(0x12);
            body_height = FMTPARA::Get_Formatted_Paragraph_Max_Height(inner, text.body);
            if (body_height <= BODY_MAX_HEIGHT) break;
            font_idx--;
        } while (font_idx >= 2);
        const int16_t body_font = std::max<int16_t>(font_idx, 2);

        // The option lines, in the body's font so that the heights are known.
        char line[3][104];
        int16_t line_height[3] = {};
        int16_t options_height = 0;
        for (int32_t i = 0; i < count; ++i) {
            snprintf(line[i], sizeof(line[i]), "%d. %s", (int)(i + 1), text.option[i]);
            line_height[i] = FMTPARA::Get_Formatted_Paragraph_Max_Height(inner, line[i]);
            options_height += line_height[i] + OPTION_GAP;
        }

        const int16_t panel_height = std::min<int16_t>(SCREEN_HEIGHT - 20, TITLE_HEIGHT + std::min(body_height, BODY_MAX_HEIGHT) + 10 + options_height + MARGIN);
        const int16_t px = (SCREEN_WIDTH - PANEL_WIDTH) / 2;
        const int16_t py = (SCREEN_HEIGHT - panel_height) / 2;

        graphics::Darken_Fill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
        graphics::Fill(px, py, px + PANEL_WIDTH - 1, py + panel_height - 1, 0);
        graphics::Box(px, py, PANEL_WIDTH, panel_height, 2, color_2);

        // The title, then the body.
        font_colors = *MISC::Font_Colors2(4, color_1, color_2);
        fonts::Set_Remap_Font_Style(4, &font_colors);
        fonts::Set_Font_LF(0x12);
        fonts::Print_Centered(px + PANEL_WIDTH / 2, py + 10, text.title);

        font_colors = *MISC::Font_Colors2(body_font, color_1, color_2);
        fonts::Set_Remap_Font_Style(body_font, &font_colors);
        fonts::Set_Font_LF(0x12);
        FMTPARA::Print_Formatted_Paragraph(px + MARGIN, py + TITLE_HEIGHT, inner, text.body, 0);

        // The options.
        int16_t fields_of[3] = {-1, -1, -1};
        int16_t y = py + TITLE_HEIGHT + std::min(body_height, BODY_MAX_HEIGHT) + 10;
        for (int32_t i = 0; i < count; ++i) {
            const char hotkey[2] = {static_cast<char>('1' + i), '\0'};
            fields_of[i] = fields::Add_Hidden_Field(px + 6, y - 2, px + PANEL_WIDTH - 7, y + line_height[i] + 2, hotkey, 0x29);
            FMTPARA::Print_Formatted_Paragraph(px + MARGIN, y, inner, line[i], 0);
            y += line_height[i] + OPTION_GAP;
        }

        video::Copy_Off_To_Back();
        setvesa::Add_Square_To_Scan_List(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
        RUSS::Mox_Sync_Update();

        int16_t picked = -1;
        do {
            timer::Mark_Time();
            const int16_t input = fields::Get_Input();
            if (input == 0) {
                RUSS::Mox_Sync_Update();
            }
            for (int32_t i = 0; i < count; ++i) {
                if (input == fields_of[i]) picked = static_cast<int16_t>(i);
            }
            timer::Release_Time(2);
        } while (picked < 0);

        Message_Box_Cleanup();
        MOX::FULL_DRAW_SCREEN_FLAG = 1;

        return picked;
    }

} // namespace GENDRAW
