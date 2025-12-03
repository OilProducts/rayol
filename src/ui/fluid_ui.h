#pragma once

#include "ui/ui_models.h"
#include "experiments/fluid/fluid_experiment.h"

namespace rayol::ui {

struct FluidUiIntents {
    bool reset = false;  // User requested a reset/reseed.
};

// Render fluid control panel and return intents.
FluidUiIntents render_fluid_ui(UiState& state, const fluid::FluidStats& stats);

}  // namespace rayol::ui
