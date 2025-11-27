#pragma once

namespace rayol::ui {

struct UiState {
    int gfx_quality = 2;       // Graphics quality selection (0=Low...3=Ultra)
    float master_volume = 0.8f;  // Master volume slider value (0..1)
    bool vsync = true;         // VSync toggle
};

struct MenuIntents {
    bool start = false;        // Request to enter the running mode
    bool exit = false;         // Request to quit the app
    bool open_options = false; // Reserved for future options menu
};

struct GameIntents {
    bool exit_to_menu = false; // Request to return to main menu
    bool quit = false;         // Request to quit entirely
};

}  // namespace rayol::ui
