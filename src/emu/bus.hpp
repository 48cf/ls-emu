#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>

enum BusSize : uint8_t {
  BUS_BYTE,
  BUS_INT,
  BUS_LONG,
};

class Area : public std::enable_shared_from_this<Area> {
public:
  constexpr static uint32_t area_size = 128 * 1024 * 1024;

  virtual void reset() {
  }

  virtual bool mem_read(uint32_t addr, BusSize size, uint32_t &value) {
    return false;
  }

  virtual bool mem_write(uint32_t addr, BusSize size, uint32_t value) {
    return false;
  }
};

class Bus {
public:
  constexpr static uint32_t areas = 0x100000000 / Area::area_size;
  constexpr static uint32_t slot_start = 24;

  void map(uint32_t num, std::shared_ptr<Area> area) {
    if (m_areas[num] != nullptr)
      throw std::runtime_error("Area already mapped");

    m_areas[num] = area;
  }

  void unmap(uint32_t num) {
    m_areas[num] = nullptr;
  }

  void reset() {
    for (auto area : m_areas) {
      if (area)
        area->reset();
    }
  }

  bool mem_read(uint32_t addr, BusSize size, uint32_t &value) {
    auto area_num = addr >> 27;

    if (auto area = m_areas[area_num]) {
      return area->mem_read(addr & 0x7ffffff, size, value);
    } else if (area_num >= slot_start) {
      value = 0;
      return true;
    } else {
      return false;
    }
  }

  bool mem_write(uint32_t addr, BusSize size, uint32_t value) {
    auto area_num = addr >> 27;

    if (auto area = m_areas[area_num])
      return area->mem_write(addr & 0x7ffffff, size, value);
    else
      return false;
  }

private:
  std::shared_ptr<Area> m_areas[areas] = {nullptr};
};
