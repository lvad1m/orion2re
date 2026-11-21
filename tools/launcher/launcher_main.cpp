// orion2re-launcher: choose the language (original data folder), a rules preset and the advisor
// mechanics, then start the game. Writes only <exe dir>/data/user.cfg (read by the game after its
// main config) and <exe dir>/launcher.ini (the launcher's own settings). docs/LocalLLM/launcher.md.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "launcher_config.h"

namespace {
    namespace fs = std::filesystem;
    using launcher::e_language;
    using launcher::e_preset;

    struct s_text {
        const char* en;
        const char* ru;
    };

    const s_text TEXT_TITLE = {"Orion2 Redone - Launcher", "Orion2 Redone - запуск"};
    const s_text TEXT_LANGUAGE = {"Language of the game", "Язык игры"};
    const s_text TEXT_FOLDER = {"Original game data folder", "Папка с данными оригинальной игры"};
    const s_text TEXT_FOLDER_HINT = {"for example E:\\Games\\MOO2\\v1_31eng", "например E:\\Games\\MOO2\\v1_31rus"};
    const s_text TEXT_RULES = {"Rules", "Правила"};
    const s_text TEXT_PRESETS[launcher::PRESET_COUNT] = {
        {"Classic: the original game", "Классика: оригинальная игра"},
        {"1.50: the community patch", "1.50: патч сообщества"},
        {"Living rivals: computer empires that adapt", "Живые соперники: компьютерные империи, которые приспосабливаются"},
        {"Custom", "Своя настройка"},
    };
    const s_text TEXT_ADVISOR = {"Advisor for computer empires", "Советник компьютерных империй"};
    const s_text TEXT_ESPIONAGE = {"Espionage", "Шпионаж"};
    const s_text TEXT_ESPIONAGE_LEVELS[3] = {
        {"off", "выкл."}, {"spy missions", "задания шпионов"}, {"strong: spies cost 60 PP", "сильный: шпион за 60 PP"}};
    const s_text TEXT_EXPANSION = {"Expansion", "Экспансия"};
    const s_text TEXT_EXPANSION_LEVELS[3] = {
        {"off", "выкл."}, {"colony ships", "колонизаторы"}, {"also outposts (weaker in long games)", "и аванпосты (слабее в долгих партиях)"}};
    const s_text TEXT_ADAPT = {"Rivals adapt: objectives, range research, Lanchester, counter-design",
        "Соперники приспосабливаются: цели, дальность, Ланчестер, контр-дизайн"};
    const s_text TEXT_DIPLOMACY = {"Diplomacy: coalition against the leader, Byzantine doctrine",
        "Дипломатия: коалиция против лидера, византийская доктрина"};
    const s_text TEXT_PHASES = {"Play for victory: a leader conquers, everyone resists a would-be winner",
        "Игра на победу: лидер завоёвывает, все поднимаются против претендента"};
    const s_text TEXT_RESEARCH_FIX = {"Fix the original AI research estimate", "Исправить ошибку оригинала в выборе исследований ИИ"};
    const s_text TEXT_MEMORY = {"Diplomatic memory: trust heals, grievances fade, empires tire of costly wars",
        "Дипломатическая память: доверие восстанавливается, обиды забываются, дорогие войны утомляют"};
    const s_text TEXT_MODS = {"Mods", "Моды"};
    const s_text TEXT_PREVIEW = {"user.cfg that will be written", "Будущий user.cfg"};
    const s_text TEXT_START = {"Start", "Старт"};
    const s_text TEXT_SAVE = {"Save", "Сохранить"};
    const s_text TEXT_RESET = {"Reset to the main config", "Сбросить к основному конфигу"};
    const s_text TEXT_FOREIGN = {"data/user.cfg was written by hand; it is kept as user.cfg.bak when you save.",
        "data/user.cfg написан вручную; при сохранении он останется как user.cfg.bak."};
    const s_text TEXT_NO_GAME = {"The game executable is not next to the launcher.", "Рядом с лаунчером нет исполняемого файла игры."};
    const s_text TEXT_STARTED = {"The game has been started.", "Игра запущена."};
    const s_text TEXT_SAVED = {"Saved.", "Сохранено."};
    const s_text TEXT_RESET_DONE = {"user.cfg removed: the game uses its main config.", "user.cfg удалён: игра использует основной конфиг."};
    const s_text TEXT_WRITE_FAILED = {"Could not write data/user.cfg.", "Не удалось записать data/user.cfg."};

    struct s_app {
        launcher::s_settings settings;
        fs::path base;
        fs::path user_cfg;
        fs::path launcher_ini;
        fs::path game_exe;
        bool foreign_user_cfg = false;
        std::vector<std::string> available_mods;
        char folder[launcher::LANGUAGE_COUNT][1024] = {};
        std::string status;
    };

    // SDL hands out UTF-8; std::filesystem would read a plain char string in the ANSI code page.
    fs::path Path_From_Utf8_(const char* text) {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text != nullptr ? text : "")));
    }

    std::string Utf8_From_Path_(const fs::path& path) {
        const std::u8string text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    const char* T(const s_app& app, const s_text& text) {
        return app.settings.language == launcher::LANGUAGE_RUSSIAN ? text.ru : text.en;
    }

    std::string Read_File_(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    bool Write_File_(const fs::path& path, const std::string& text) {
        std::error_code code;
        fs::create_directories(path.parent_path(), code);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        return static_cast<bool>(out);
    }

    void Load_(s_app* app) {
        app->base = Path_From_Utf8_(SDL_GetBasePath());
        app->user_cfg = app->base / "data" / "user.cfg";
        app->launcher_ini = app->base / "launcher.ini";
#if defined(_WIN32)
        app->game_exe = app->base / "orion2re.exe";
#else
        app->game_exe = app->base / "orion2re";
#endif
        app->settings.mechanics = launcher::Preset_Mechanics(launcher::PRESET_CUSTOM);
        launcher::Read_Launcher_Ini(Read_File_(app->launcher_ini), &app->settings);
        const std::string user_cfg = Read_File_(app->user_cfg);
        if (!user_cfg.empty()) {
            if (launcher::Is_Launcher_User_Cfg(user_cfg)) {
                launcher::Read_User_Cfg(user_cfg, &app->settings);
            } else {
                app->foreign_user_cfg = true;
            }
        }
        for (int i = 0; i < launcher::LANGUAGE_COUNT; ++i) {
            std::strncpy(app->folder[i], app->settings.game_dir[i].c_str(), sizeof(app->folder[i]) - 1);
        }
        std::error_code code;
        for (const auto& entry : fs::directory_iterator(app->base / "data" / "mods", code)) {
            if (entry.path().extension() == ".zip") app->available_mods.push_back(Utf8_From_Path_(entry.path().stem()));
        }
    }

    bool Save_(s_app* app) {
        for (int i = 0; i < launcher::LANGUAGE_COUNT; ++i) app->settings.game_dir[i] = app->folder[i];
        if (app->foreign_user_cfg) {
            std::error_code code;
            fs::copy_file(app->user_cfg, app->base / "data" / "user.cfg.bak", fs::copy_options::overwrite_existing, code);
            app->foreign_user_cfg = false;
        }
        const bool cfg = Write_File_(app->user_cfg, launcher::Build_User_Cfg(app->settings));
        Write_File_(app->launcher_ini, launcher::Build_Launcher_Ini(app->settings));
        app->status = cfg ? T(*app, TEXT_SAVED) : T(*app, TEXT_WRITE_FAILED);
        return cfg;
    }

    bool Start_(s_app* app) {
        if (!Save_(app)) return false;
        std::error_code code;
        if (!fs::exists(app->game_exe, code)) {
            app->status = T(*app, TEXT_NO_GAME);
            return false;
        }
        const std::vector<std::string> args = launcher::Game_Arguments(Utf8_From_Path_(app->game_exe), app->settings);
        std::vector<const char*> argv;
        for (const std::string& arg : args) argv.push_back(arg.c_str());
        argv.push_back(nullptr);
        SDL_Process* process = SDL_CreateProcess(argv.data(), false);
        if (process == nullptr) {
            app->status = SDL_GetError();
            return false;
        }
        SDL_DestroyProcess(process);  // frees the handle; the game keeps running
        app->status = T(*app, TEXT_STARTED);
        return true;
    }

    void Load_Font_(ImGuiIO& io) {
        const char* candidates[] = {
#if defined(_WIN32)
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
#else
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
#endif
        };
        for (const char* path : candidates) {
            std::error_code code;
            if (fs::exists(path, code) && io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic()) != nullptr) {
                return;
            }
        }
        io.Fonts->AddFontDefault();  // no Cyrillic glyphs: Russian labels show as '?'
    }

    // Returns true when the launcher should close (the game was started).
    bool Draw_(s_app* app) {
        bool close = false;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextUnformatted(T(*app, TEXT_LANGUAGE));
        int language = app->settings.language;
        ImGui::RadioButton("English", &language, launcher::LANGUAGE_ENGLISH);
        ImGui::SameLine();
        ImGui::RadioButton("Русский", &language, launcher::LANGUAGE_RUSSIAN);
        app->settings.language = static_cast<e_language>(language);
        ImGui::TextUnformatted(T(*app, TEXT_FOLDER));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##folder", T(*app, TEXT_FOLDER_HINT), app->folder[language], sizeof(app->folder[language]));

        ImGui::Separator();
        ImGui::TextUnformatted(T(*app, TEXT_RULES));
        int preset = app->settings.preset;
        for (int i = 0; i < launcher::PRESET_COUNT; ++i) {
            ImGui::RadioButton(T(*app, TEXT_PRESETS[i]), &preset, i);
        }
        app->settings.preset = static_cast<e_preset>(preset);

        launcher::s_mechanics& m = app->settings.mechanics;
        if (app->settings.preset == launcher::PRESET_CUSTOM) {
            ImGui::Indent();
            ImGui::Checkbox(T(*app, TEXT_ADVISOR), &m.advisor);
            ImGui::BeginDisabled(!m.advisor);
            const float slider_width = ImGui::GetFontSize() * 16.0f;
            ImGui::SetNextItemWidth(slider_width);
            ImGui::SliderInt(T(*app, TEXT_ESPIONAGE), &m.espionage, 0, 2, T(*app, TEXT_ESPIONAGE_LEVELS[m.espionage]),
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
            ImGui::SetNextItemWidth(slider_width);
            ImGui::SliderInt(T(*app, TEXT_EXPANSION), &m.expansion, 0, 2, T(*app, TEXT_EXPANSION_LEVELS[m.expansion]),
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
            ImGui::Checkbox(T(*app, TEXT_ADAPT), &m.rivals_adapt);
            ImGui::Checkbox(T(*app, TEXT_DIPLOMACY), &m.diplomacy);
            ImGui::Checkbox(T(*app, TEXT_PHASES), &m.phases);
            ImGui::EndDisabled();
            ImGui::Checkbox(T(*app, TEXT_RESEARCH_FIX), &m.research_fix);
            ImGui::Checkbox(T(*app, TEXT_MEMORY), &m.memory);
            if (!app->available_mods.empty()) {
                ImGui::TextUnformatted(T(*app, TEXT_MODS));
                if (ImGui::BeginTable("##mods", 4)) {
                    for (const std::string& id : app->available_mods) {
                        ImGui::TableNextColumn();
                        auto it = std::find(app->settings.mods.begin(), app->settings.mods.end(), id);
                        bool on = it != app->settings.mods.end();
                        if (ImGui::Checkbox(id.c_str(), &on)) {
                            if (on) app->settings.mods.push_back(id);
                            else app->settings.mods.erase(it);
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::Unindent();
        }

        if (app->foreign_user_cfg) ImGui::TextWrapped("%s", T(*app, TEXT_FOREIGN));
        if (ImGui::CollapsingHeader(T(*app, TEXT_PREVIEW))) {
            const std::string preview = launcher::Build_User_Cfg(app->settings);
            ImGui::TextUnformatted(preview.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button(T(*app, TEXT_START), ImVec2(160, 0))) close = Start_(app);
        ImGui::SameLine();
        if (ImGui::Button(T(*app, TEXT_SAVE), ImVec2(160, 0))) Save_(app);
        ImGui::SameLine();
        if (ImGui::Button(T(*app, TEXT_RESET))) {
            std::error_code code;
            fs::remove(app->user_cfg, code);
            app->foreign_user_cfg = false;
            app->status = T(*app, TEXT_RESET_DONE);
        }
        if (!app->status.empty()) ImGui::TextWrapped("%s", app->status.c_str());
        ImGui::End();
        return close;
    }
} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "orion2re-launcher", SDL_GetError(), nullptr);
        return 1;
    }
    s_app app;
    Load_(&app);

    SDL_Window* window = SDL_CreateWindow(T(app, TEXT_TITLE), 760, 700, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (renderer == nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "orion2re-launcher", SDL_GetError(), nullptr);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    Load_Font_(io);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) running = false;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (Draw_(&app)) running = false;
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 24, 32, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
