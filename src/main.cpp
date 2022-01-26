#include <SDL2/SDL.h>

#include "emu/bus.hpp"
#include "emu/cpu.hpp"
#include "emu/kinnowfb.hpp"
#include "emu/platform.hpp"
#include "emu/ram.hpp"
#include "emu/serial.hpp"

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    printf("Unable to initialize SDL: %s", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("ls-emu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, 0);
  if (!window) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  if (!renderer) {
    printf("Could not create renderer: %s", SDL_GetError());
    return 1;
  }

  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1024, 768);
  if (!texture) {
    printf("Could not create texture: %s", SDL_GetError());
    return 1;
  }

  Bus bus;

  Ram ram(bus, 64 * 1024 * 1024);
  Platform board(bus, "boot.bin");
  KinnowFb kinnow(bus, 1024, 768);
  SerialPort serial1(board, 0);
  SerialPort serial2(board, 1);
  Cpu cpu(bus);

  SDL_ShowWindow(window);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);

  auto done = false;

  while (!done) {
    for (auto i = 0; i < 5000; i++)
      cpu.execute();

    kinnow.draw(texture);

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT: done = true; break;
      case SDL_KEYDOWN:
        if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
          done = true;
        break;
      }
    }

    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
  }

  return 0;
}
