#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "bus.hpp"

// Snagged from https://github.com/limnarch/limnemu/blob/main/src/lsic.c and rewritten
// to match the style of the codebase :^)
class InterruptController : public Area {
public:
  void raise(int vector) {
    if (vector == 0 || vector >= 64)
      throw std::runtime_error("Bad interrupt vector");

    int bitmap = vector / 32;
    int bitmap_offset = vector & 0b11111;

    m_regs[bitmap + 2] |= (1 << bitmap_offset);

    if (!((m_regs[bitmap] >> bitmap_offset) & 1))
      m_pending = true;
  }

  bool interrupt_pending() const {
    return m_pending;
  }

  virtual void reset() {
    memset(m_regs, 0, sizeof(m_regs));

    m_pending = false;
  }

  virtual bool mem_read(uint32_t addr, BusSize size, uint32_t &value) {
    switch (addr / 4) {
    case 0:
    case 1:
    case 2:
    case 3: // Register reads
      value = m_regs[addr / 4];
      return true;
    case 4: // Claim
      for (int i = 1; i < 64; i++) {
        int bitmap = i / 32;
        int bitmap_offset = i & 0b11111;

        if (((~m_regs[bitmap] & m_regs[bitmap + 2]) >> bitmap_offset) & 1) {
          value = i;
          return true;
        }
      }

      value = 0;
      return true;
    }

    return false;
  }

  virtual bool mem_write(uint32_t addr, BusSize size, uint32_t value) {
    switch (addr / 4) {
    case 0:
    case 1:
    case 2:
    case 3: // Register write
      m_regs[addr / 4] = value;
      break;
    case 4: // Complete
      if (value >= 64)
        return false;
      m_regs[(value / 32) + 2] &= ~(1 << (value & 31));
      break;
    }

    m_pending = (~m_regs[0] & m_regs[2]) || (~m_regs[1] & m_regs[3]);
    return true;
  }

private:
  uint32_t m_regs[5];

  bool m_pending = false;
};
