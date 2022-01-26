#pragma once

#include <cstdint>

#include "platform.hpp"

enum SerialPortCommand : uint8_t {
  SERIAL_CMD_WRITE = 1,
  SERIAL_CMD_READ,
  SERIAL_CMD_SET_INTERRUPTS,
  SERIAL_CMD_CLEAR_INTERRUPTS,
};

class SerialPort : public CitronPort {
public:
  SerialPort(Platform &platform, int num) : m_base(0x10 + num * 2) {
    auto self = std::shared_ptr<SerialPort>(this, [](auto) {});

    platform.set_port(m_base, self);
    platform.set_port(m_base + 1, self);

    setbuf(stdout, nullptr);
  }

  void reset() override {
    m_interrupts = false;
  }

  bool read(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t &value) override {
    if (port == m_base) {
      value = 0;
    } else if (port == m_base + 1) {
      if (size == BUS_BYTE)
        value = m_data & 0xff;
      else if (size == BUS_INT)
        value = m_data & 0xffff;
      else if (size == BUS_LONG)
        value = m_data;
    }

    return true;
  }

  bool write(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t value) override {
    if (port == m_base) {
      switch (value) {
      case SERIAL_CMD_WRITE: putchar(m_data); return true;
      case SERIAL_CMD_READ:
        m_data = m_last_data;
        m_last_data = 0xffff;
        return true;
      case SERIAL_CMD_SET_INTERRUPTS: m_interrupts = true; return true;
      case SERIAL_CMD_CLEAR_INTERRUPTS: m_interrupts = false; return true;
      }
    } else if (port == m_base + 1) {
      if (size == BUS_BYTE)
        m_data = value & 0xff;
      else if (size == BUS_INT)
        m_data = value & 0xffff;
      else if (size == BUS_LONG)
        m_data = value;
      return true;
    }

    return false;
  }

private:
  uint32_t m_base;
  uint32_t m_data = 0x0;
  uint32_t m_last_data = 0xffff;

  bool m_interrupts = false;
};
