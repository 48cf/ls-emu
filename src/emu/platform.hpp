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
  PBOARD_DISK_BUFFER,
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

  virtual bool read(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t &value) {
    return false;
  }

  virtual bool write(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t value) {
    return false;
  }
};

class DiskController : public CitronPort {
  struct AttachedDisk {
    AttachedDisk(std::filesystem::path disk_path) : stream(disk_path, std::ios::binary | std::ios::in | std::ios::out | std::ios::app) {
      if (!stream.good())
        throw std::runtime_error("Failed to open disk image");

      stream.seekg(0, std::ios::end);

      block_count = stream.tellg() / 512;
    }

    std::fstream stream;

    uint32_t block_count;
  };

  friend class Platform;

public:
  DiskController() {
    m_disk_buffer.resize(512, 0);
    m_disks.clear();
  }

  void attach(std::filesystem::path disk_path) {
    if (m_disks.size() >= 8)
      throw std::runtime_error("Reached the maximum amount of disks attached");

    m_disks.emplace_back(disk_path);
  }

  void reset() override {
    m_interrupts = false;
    m_port_a = 0;
    m_port_b = 0;
    m_selected = 0;
    m_info_what = 0;
    m_info_details = 0;
    m_operation = 0;
  }

  bool read(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t &value) override {
    if (port == 0x19) { // Command
      value = m_operation;
      return true;
    } else if (port == 0x1a) {
      value = m_port_a;
      return true;
    } else if (port == 0x1b) {
      value = m_port_b;
      return true;
    }

    return false;
  }

  bool write(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t value) override {
    if (port == 0x19) {
      switch (value) {
      case 1: // Select drive
        if (m_port_a < m_disks.size())
          m_selected = m_port_a;
        else
          m_selected = -1;
        return true;
      case 2: { // Read block
        if (m_selected == -1)
          return false;

        auto &disk = m_disks[m_selected];
        if (m_port_a >= disk.block_count)
          return false;

        disk.stream.seekg(m_port_a * 512, std::ios::beg);
        disk.stream.read((char *)m_disk_buffer.data(), m_disk_buffer.size());

        write_info(int_ctl, 0, m_port_a);
        return true;
      }
      case 3: { // Write block
        if (m_selected == -1)
          return false;

        auto &disk = m_disks[m_selected];
        if (m_port_a >= disk.block_count)
          return false;

        disk.stream.seekp(m_port_a * 512, std::ios::beg);
        disk.stream.write((const char *)m_disk_buffer.data(), m_disk_buffer.size());

        write_info(int_ctl, 0, m_port_a);
        return true;
      }
      case 4: // Read info
        m_port_a = m_info_what;
        m_port_b = m_info_details;
        return true;
      case 5: // Get drive block count
        if (m_port_a < m_disks.size()) {
          m_port_b = m_disks[m_port_a].block_count;
          m_port_a = 1;
        } else {
          m_port_a = 0;
          m_port_b = 0;
        }
        return true;
      case 6: // Enable interrupts
        m_interrupts = true;
        return true;
      case 7: // Disable interrupts
        m_interrupts = false;
        return true;
      }

      return false;
    } else if (port == 0x1a) {
      m_port_a = value;
      return true;
    } else if (port == 0x1b) {
      m_port_b = value;
      return true;
    }

    return false;
  }

private:
  void write_info(InterruptController &int_ctl, uint32_t what, uint32_t details) {
    m_info_what = what;
    m_info_details = details;

    if (m_interrupts)
      int_ctl.raise(0x3);
  }

  std::vector<AttachedDisk> m_disks;
  std::vector<uint8_t> m_disk_buffer;

  uint32_t m_selected;
  uint32_t m_info_what;
  uint32_t m_info_details;
  uint32_t m_operation;
  uint32_t m_port_a;
  uint32_t m_port_b;

  bool m_interrupts;
};

class Platform : public Area {
public:
  Platform(Bus &bus, InterruptController &int_ctl, DiskController &disk_ctl, std::filesystem::path boot_rom)
      : m_int_ctl(int_ctl), m_disk_ctl(disk_ctl) // sorry, OCD.
  {
    auto self = std::shared_ptr<Platform>(this, [](auto) {});

    m_regs[0] = 0x00030001;       // Board version
    m_nvram.resize(64 * 1024, 0); // 64KiB of NVRAM

    if (auto stream = std::ifstream(boot_rom, std::ios::binary); stream.good()) {
      stream.seekg(0, std::ios::end);

      auto length = stream.tellg();

      m_boot_rom.resize(length, 0);

      stream.seekg(0, std::ios::beg);
      stream.read((char *)m_boot_rom.data(), length);
    } else {
      throw std::runtime_error("Failed to open boot ROM image");
    }

    set_port(0x19, disk_ctl.shared_from_this());
    set_port(0x1a, disk_ctl.shared_from_this());
    set_port(0x1b, disk_ctl.shared_from_this());

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
        return port->read(m_int_ctl, port_num, size, value);

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
    case PBOARD_DISK_BUFFER: {
      if (address > m_disk_ctl.m_disk_buffer.size())
        return false;

      auto disk_buf = m_disk_ctl.m_disk_buffer.data() + address;
      if (size == BUS_BYTE)
        value = *(uint8_t *)disk_buf;
      else if (size == BUS_INT)
        value = *(uint16_t *)disk_buf;
      else if (size == BUS_LONG)
        value = *(uint32_t *)disk_buf;

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
        return port->write(m_int_ctl, port_num, size, value);
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
    case PBOARD_DISK_BUFFER: {
      if (address > m_disk_ctl.m_disk_buffer.size())
        return false;

      auto disk_buf = m_disk_ctl.m_disk_buffer.data() + address;
      if (size == BUS_BYTE)
        *(uint8_t *)disk_buf = value;
      else if (size == BUS_INT)
        *(uint16_t *)disk_buf = value;
      else if (size == BUS_LONG)
        *(uint32_t *)disk_buf = value;

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
    else if (addr >= 0x20000 && addr < 0x20200)
      return {PBOARD_DISK_BUFFER, addr - 0x20000};
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
  DiskController &m_disk_ctl;

  std::shared_ptr<CitronPort> m_ports[256] = {nullptr};
  std::vector<uint8_t> m_nvram;
  std::vector<uint8_t> m_boot_rom;

  uint32_t m_regs[32];
};
