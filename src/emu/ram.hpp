#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "bus.hpp"

class Ram : public std::enable_shared_from_this<Ram> {
  class RamArea : public Area {
  public:
    RamArea(const std::shared_ptr<Ram> &ram, uint32_t page) : m_ram(ram), m_page(page) {
    }

    bool mem_read(uint32_t addr, BusSize size, uint32_t &value) override {
      auto offset = m_page * Area::area_size + addr;
      if (offset >= m_ram->m_memory.size())
        return false;

      auto ram = m_ram->m_memory.data() + offset;
      if (size == BUS_BYTE)
        value = *(uint8_t *)ram;
      else if (size == BUS_BYTE)
        value = *(uint16_t *)ram;
      else if (size == BUS_BYTE)
        value = *(uint32_t *)ram;

      return true;
    }

    bool mem_write(uint32_t addr, BusSize size, uint32_t value) override {
      auto offset = m_page * Area::area_size + addr;
      if (offset >= m_ram->m_memory.size())
        return false;

      auto ram = m_ram->m_memory.data() + offset;
      if (size == BUS_BYTE)
        *(uint8_t *)ram = value;
      else if (size == BUS_BYTE)
        *(uint16_t *)ram = value;
      else if (size == BUS_BYTE)
        *(uint32_t *)ram = value;

      return true;
    }

  private:
    std::shared_ptr<Ram> m_ram;

    uint32_t m_page;
  };

  class RamDescriptor : public Area {
  public:
    RamDescriptor(const std::shared_ptr<Ram> &ram) : m_ram(ram) {
    }

    virtual bool mem_read(uint32_t addr, BusSize size, uint32_t &value) {
      if (size != BUS_LONG)
        return false;

      if (addr == 0) {
        value = Ram::slot_count;
      } else if (auto reg_num = addr / 4; reg_num - 1 < Ram::slot_count) {
        value = m_ram->m_slot_sizes[reg_num - 1];
      } else {
        return false;
      }

      return true;
    }

    virtual bool mem_write(uint32_t addr, BusSize size, uint32_t value) {
      return false;
    }

  private:
    std::shared_ptr<Ram> m_ram;
  };

public:
  constexpr static uint32_t slot_size = 32 * 1024 * 1024; // 32MiB
  constexpr static uint32_t slot_count = 8;
  constexpr static uint32_t max_size = slot_size * slot_count;

  Ram(Bus &bus, uint32_t size) {
    auto self = std::shared_ptr<Ram>(this, [](auto) {});

    m_memory.resize(size, 0);

    bus.map(0, std::make_shared<RamArea>(self, 0));
    bus.map(2, std::make_shared<RamDescriptor>(self));

    if (size > Area::area_size)
      bus.map(1, std::make_shared<RamArea>(self, 1));

    auto full_slots = size / slot_size;
    auto count = 0;

    for (; count < full_slots; count++)
      m_slot_sizes[count] = slot_size;

    if (auto leftover = size - (full_slots * slot_size))
      m_slot_sizes[count] = leftover;
  }

private:
  friend class RamArea;
  friend class RamDescriptor;

  std::vector<uint8_t> m_memory;

  uint32_t m_slot_sizes[slot_count];
};
