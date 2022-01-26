#include "emu/bus.hpp"
#include "emu/cpu.hpp"
#include "emu/kinnowfb.hpp"
#include "emu/platform.hpp"
#include "emu/ram.hpp"
#include "emu/serial.hpp"

int main() {
  Bus bus;

  Ram ram(bus, 64 * 1024 * 1024);
  Platform board(bus, "boot.bin");
  KinnowFb kinnow(bus, 1280, 1024);
  SerialPort serial1(board, 0);
  SerialPort serial2(board, 1);
  Cpu cpu(bus);

  for (auto i = 0;; i++) {
    cpu.execute();
  }

  return 0;
}
