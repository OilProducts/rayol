#pragma once

#include "ui/ui_models.h"

namespace rayol::ui {

// Render the main menu UI; updates UiState and returns one-frame intents.
MenuIntents render_menu_ui(UiState& state);

}  // namespace rayol::ui
