#pragma once

namespace rayol {

class App {
public:
    int run();

private:
    enum class Mode {
        MainMenu,
        Running,
    };
};

}  // namespace rayol
