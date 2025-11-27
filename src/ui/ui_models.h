#pragma once

namespace rayol::ui {

enum class AppMode {
    MainMenu,
    Running,
};

struct UiState {
    int gfx_quality = 2;       // 0=Low,1=Med,2=High,3=Ultra
    float master_volume = 0.8f;
    bool vsync = true;
};

struct MenuIntents {
    bool start = false;
    bool exit = false;
    bool open_options = false;
};

struct GameIntents {
    bool exit_to_menu = false;
    bool quit = false;
};

}  // namespace rayol::ui
