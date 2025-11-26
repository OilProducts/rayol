#include <SDL3/SDL.h>
#include <iostream>

namespace rayol {

class App {
public:
    int run() {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
            return 1;
        }

        SDL_Window* window = SDL_CreateWindow("Rayol Prototype", 960, 540, SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "SDL window creation failed: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }

        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
            }
            SDL_Delay(16);
        }

        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
};

}  // namespace rayol

int main() {
    rayol::App app;
    return app.run();
}
