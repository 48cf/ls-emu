#include <SDL2/SDL.h>

#include "emu/amanatsu.hpp"
#include "emu/bus.hpp"
#include "emu/cpu.hpp"
#include "emu/kinnowfb.hpp"
#include "emu/lsic.hpp"
#include "emu/platform.hpp"
#include "emu/ram.hpp"
#include "emu/rtc.hpp"
#include "emu/serial.hpp"

constexpr static auto instructions_per_sec = 25'000'000;
constexpr static auto ticks_per_second = 60;

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

  Ram ram(bus, 32 * 1024 * 1024);
  KinnowFb kinnow(bus, 1024, 768);

  InterruptController lsic;
  DiskController disk_ctl;

  // ?????
  auto _ = std::shared_ptr<DiskController>(&disk_ctl, [](auto) {});

  disk_ctl.attach("mintia-dist.img");
  disk_ctl.attach("aisix-dist.img");

  Platform board(bus, lsic, disk_ctl, "boot.bin");
  SerialPort serial1(board, 0);
  SerialPort serial2(board, 1);
  Rtc rtc(board);

  Amanatsu amanatsu(board);
  AmanatsuKeyboard keyboard(amanatsu);
  AmanatsuMouse mouse(amanatsu);

  Cpu cpu(bus, lsic);

  SDL_ShowWindow(window);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);

  auto done = false;
  auto tick_start = SDL_GetTicks();
  auto tick_end = SDL_GetTicks();
  auto ticks = 0;

  while (!done) {
    auto ms = std::max(SDL_GetTicks() - tick_start, 1);
    auto instr_to_run = instructions_per_sec / ticks_per_second / ms;

    tick_start = SDL_GetTicks();

    for (auto i = 0; i < ms; i++) {
      for (auto j = 0; j < instr_to_run; j++) {
        cpu.execute();

        if (cpu.is_halted())
          break;
      }

      rtc.tick(lsic, 1);
    }

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT: // Handle native app exit
        done = true;
        break;
      case SDL_KEYDOWN:
      case SDL_KEYUP:
        keyboard.handle_key_event(event.key);
        if (keyboard.interrupt_line)
          lsic.raise(keyboard.interrupt_line);
        break;
      }
    }

    if (ticks % ticks_per_second == 0) {
      kinnow.draw(texture);

      SDL_RenderCopy(renderer, texture, nullptr, nullptr);
      SDL_RenderPresent(renderer);
    }

    tick_end = SDL_GetTicks();

    auto time_left = 1000 / ticks_per_second - (int)(tick_end - tick_start);

    if (time_left > 0)
      SDL_Delay(time_left);
    else if (time_left < 0)
      printf("Time overrun: %dms\n", -time_left);
  }

  return 0;
}
