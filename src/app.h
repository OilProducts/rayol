#pragma once

namespace rayol {

class App {
public:
    // Entry point: initialize SDL/Vulkan/ImGui and drive the app loop.
    int run();

private:
    enum class Mode {
        MainMenu,
        Running,
    };
};

}  // namespace rayol
