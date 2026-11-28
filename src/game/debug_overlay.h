#pragma once

namespace debug_overlay {
    constexpr int16_t k_all_screens = INT16_MIN;
    extern uint8_t g_enabled;
    extern uint16_t g_font_size;
    extern uint16_t g_ui_scale_percent;

    struct s_panel_context {
        int16_t screen_id;
        int16_t previous_screen_id;
        int16_t player_id;
        bool screen_entered;
    };

    using panel_callback = void (*)(const s_panel_context& context);

    class s_screen_panel_registration {
      public:
        s_screen_panel_registration(int16_t screen_id, panel_callback callback);
    };

    class s_global_pane_registration {
      public:
        s_global_pane_registration(const char* label, panel_callback callback);
    };

    bool Initialize(SDL_Renderer* renderer);
    void Shutdown();
    void Handle_Display_Size(int32_t width, int32_t height);

    bool Handle_Key_Event(const SDL_KeyboardEvent& event, SDL_Window* window);
    bool Handle_Text_Input(const char* text);
    bool Handle_Mouse_Position(
        int16_t game_x,
        int16_t game_y,
        float raw_x,
        float raw_y,
        int32_t raw_width,
        int32_t raw_height);
    bool Handle_Mouse_Button(
        uint8_t button,
        bool down,
        int16_t game_x,
        int16_t game_y,
        float raw_x,
        float raw_y,
        int32_t raw_width,
        int32_t raw_height);
    bool Handle_Mouse_Wheel(float x, float y);
    void Handle_Focus(bool focused);

    void Build_Game_Frame();
    void Publish_Game_Frame(uint32_t generation);
    void Render_Published_Frame(SDL_Renderer* renderer, uint32_t generation);

    bool Register_Screen_Panel(int16_t screen_id, panel_callback callback);
    bool Register_Global_Panel(panel_callback callback);
    bool Register_Global_Pane(const char* label, panel_callback callback);
    // True while the overlay wants the operating system's cursor and the game must not see the mouse: the debugger is
    // open, or the mentat panel is open and the mouse is over it.
    bool Visible();

    // The mentat panel (mentat_panel.cpp): a window of its own, drawn without the debugger and toggled by F9.
    // `available` says whether F9 is live (the mentat setting is on, not a network game); `take_open_request` is asked
    // every frame and opens the panel by itself when it answers true; `draw` draws the window while it is open (with
    // the mentat's Cyrillic font when a system font was found). Only the panel's own area captures the mouse; other
    // keys and the rest of the screen go to the game.
    using mentat_available = bool (*)();
    void Register_Mentat_Window(mentat_available available, mentat_available take_open_request, panel_callback draw);
    bool Mentat_Visible();
    void Set_Mentat_Visible(bool visible);
} // namespace debug_overlay