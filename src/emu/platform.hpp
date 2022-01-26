#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <vector>

#include "bus.hpp"
#include "lsic.hpp"

enum PlatformMemoryArea : uint8_t {
  PBOARD_CITRON,
  PBOARD_REGS,
  PBOARD_NVRAM,
  PBOARD_LSIC,
  PBOARD_BOOT_ROM,
  PBOARD_RESET,
  PBOARD_NONE,
};

struct PlatformArea {
  PlatformMemoryArea area;
  uint32_t address;
};

class CitronPort : public std::enable_shared_from_this<CitronPort> {
public:
  virtual void reset() {
  }

  virtual bool read(uint32_t port, BusSize size, uint32_t &value) {
    return false;
  }

  virtual bool write(uint32_t port, BusSize size, uint32_t value) {
    return false;
  }
};

class Platform : public Area {
public:
  Platform(Bus &bus, InterruptController &int_ctl, std::filesystem::path boot_rom) : m_int_ctl(int_ctl) {
    auto self = std::shared_ptr<Platform>(this, [](auto) {});

    m_regs[0] = 0x00030001;       // Board version
    m_nvram.resize(64 * 1024, 0); // 64KiB of NVRAM

    if (auto stream = std::ifstream(boot_rom, std::ios::binary); stream.good()) {
      stream.seekg(0, std::ios::end);

      auto length = stream.tellg();

      m_boot_rom.resize(length, 0);

      stream.seekg(0, std::ios::beg);
      stream.read((char *)m_boot_rom.data(), length);
    }

    bus.map(31, self);
  }

  void set_port(uint32_t num, std::shared_ptr<CitronPort> port) {
    if (m_ports[num] != nullptr)
      throw std::runtime_error("Port already in use");

    m_ports[num] = port;
  }

  void reset() override {
    m_int_ctl.reset();

    for (auto port : m_ports) {
      if (port)
        port->reset();
    }
  }

  bool mem_read(uint32_t addr, BusSize size, uint32_t &value) override {
    auto [area, address] = area_from_addr(addr);

    switch (area) {
    case PBOARD_CITRON: {
      auto port_num = address / 4;
      if (auto port = m_ports[port_num])
        return port->read(port_num, size, value);

      value = 0;
      return true;
    }
    case PBOARD_REGS: {
      auto reg_num = address / 4;
      if (size == BUS_LONG)
        value = m_regs[reg_num];

      return true;
    }
    case PBOARD_NVRAM: {
      auto nvram = m_nvram.data() + address;
      if (size == BUS_BYTE)
        value = *(uint8_t *)nvram;
      else if (size == BUS_INT)
        value = *(uint16_t *)nvram;
      else if (size == BUS_LONG)
        value = *(uint32_t *)nvram;

      return true;
    }
    case PBOARD_LSIC:
      if (size == BUS_LONG)
        return m_int_ctl.mem_read(address, size, value);
      return false;
    case PBOARD_BOOT_ROM: {
      if (address > m_boot_rom.size())
        return false;

      auto boot_rom = m_boot_rom.data() + address;
      if (size == BUS_BYTE)
        value = *(uint8_t *)boot_rom;
      else if (size == BUS_INT)
        value = *(uint16_t *)boot_rom;
      else if (size == BUS_LONG)
        value = *(uint32_t *)boot_rom;

      return true;
    }
    case PBOARD_RESET:
    case PBOARD_NONE: return false;
    }
  }

  bool mem_write(uint32_t addr, BusSize size, uint32_t value) override {
    auto [area, address] = area_from_addr(addr);

    switch (area) {
    case PBOARD_CITRON: {
      auto port_num = address / 4;
      if (auto port = m_ports[port_num])
        return port->write(port_num, size, value);
      return true;
    }
    case PBOARD_REGS: {
      auto reg_num = address / 4;
      if (size == BUS_LONG && reg_num != 0)
        m_regs[reg_num] = value;

      return true;
    }
    case PBOARD_NVRAM: {
      auto nvram = m_nvram.data() + address;
      if (size == BUS_BYTE)
        *(uint8_t *)nvram = value;
      else if (size == BUS_INT)
        *(uint16_t *)nvram = value;
      else if (size == BUS_LONG)
        *(uint32_t *)nvram = value;

      return true;
    }
    case PBOARD_LSIC:
      if (size == BUS_LONG)
        return m_int_ctl.mem_write(address, size, value);
      return false;
    case PBOARD_BOOT_ROM: return false;
    case PBOARD_RESET:
      if (size == BUS_LONG && value == 0xaabbccdd) {
        reset();
        return true;
      }
      return false;
    case PBOARD_NONE: return false;
    }
  }

private:
  PlatformArea area_from_addr(uint32_t addr) const {
    if (addr < 0x400)
      return {PBOARD_CITRON, addr};
    else if (addr >= 0x800 && addr < 0x880)
      return {PBOARD_REGS, addr - 0x800};
    else if (addr >= 0x1000 && addr < 0x11000)
      return {PBOARD_NVRAM, addr - 0x1000};
    else if (addr >= 0x30000 && addr < 0x30100)
      return {PBOARD_LSIC, addr - 0x30000};
    else if (addr >= 0x7fe0000)
      return {PBOARD_BOOT_ROM, addr - 0x7fe0000};
    else if (addr == 0x800000)
      return {PBOARD_RESET, 0};
    else
      return {PBOARD_NONE, 0};
  }

  InterruptController &m_int_ctl;

  std::shared_ptr<CitronPort> m_ports[256] = {nullptr};
  std::vector<uint8_t> m_nvram;
  std::vector<uint8_t> m_boot_rom;

  uint32_t m_regs[32];
};
