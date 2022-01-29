#pragma once

#include <chrono>

#include "platform.hpp"

class Rtc : public CitronPort {
public:
  Rtc(Platform &platform) {
    auto self = std::shared_ptr<Rtc>(this, [](auto) {});

    platform.set_port(0x20, self);
    platform.set_port(0x21, self);
  }

  void reset() override {
    m_interval_ms = 0;
    m_interval_count = 0;
    m_port_a = 0;
  }

  bool read(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t &value) override {
    if (port == 0x20) {
      value = 0;
      return true;
    } else if (port == 0x21) {
      value = m_port_a;
      return true;
    }

    return false;
  }

  bool write(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t value) override {
    if (port == 0x20) {
      switch (value) {
      case 1: // Set interval
        m_interval_ms = m_port_a;
        m_interval_count = 0;
        return true;
      case 2: // Get epoch time
        if (m_modified)
          m_port_a = m_current_time_sec;
        else
          m_port_a = std::chrono::duration_cast<std::chrono::seconds>(m_time.time_since_epoch()).count();
        return true;
      case 3: // Get epoch ms
        if (m_modified)
          m_port_a = m_current_time_ms;
        else
          m_port_a = std::chrono::duration_cast<std::chrono::milliseconds>(m_time.time_since_epoch()).count();
        return true;
      case 4: // Set epoch time
        m_current_time_sec = m_port_a;
        m_modified = true;
        return true;
      case 5: // Set epoch ms
        m_current_time_ms = m_port_a;
        m_modified = true;
        return true;
      }
    } else if (port == 0x21) {
      m_port_a = value;
      return true;
    }

    return false;
  }

  void tick(InterruptController &int_ctl, int ms) {
    if (!m_modified) {
      m_time = m_clock.now();
    } else {
      m_current_time_ms += ms;

      if (m_current_time_ms >= 1000) {
        m_current_time_ms -= 1000;
        m_current_time_sec += 1;
      }
    }

    m_interval_count += ms;

    if (m_interval_count >= m_interval_ms) {
      int_ctl.raise(1);

      m_interval_count -= m_interval_ms;
    }
  }

private:
  using clock = std::chrono::high_resolution_clock;

  bool m_modified = false;

  uint32_t m_current_time_sec = 0;
  uint32_t m_current_time_ms = 0;
  uint32_t m_interval_ms = 0;
  uint32_t m_interval_count = 0;
  uint32_t m_port_a = 0;

  clock m_clock;
  clock::time_point m_time;
};
