#pragma once

#include <cstdint>
#include <memory>

#include "platform.hpp"

class AmanatsuDevice : public std::enable_shared_from_this<AmanatsuDevice> {
public:
  int interrupt_line;

  uint32_t magic;
  uint32_t port_a;
  uint32_t port_b;

  virtual void reset() {
  }

  virtual bool action(uint32_t value) {
    return false;
  }
};

class Amanatsu;

class AmanatsuController : public AmanatsuDevice {
public:
  AmanatsuController(std::shared_ptr<Amanatsu> amanatsu) : m_amanatsu(amanatsu) {
  }

  bool action(uint32_t value) override;

private:
  std::shared_ptr<Amanatsu> m_amanatsu;
};

class AmanatsuKeyboard : public AmanatsuDevice {
public:
  AmanatsuKeyboard() {
    magic = 0x8fc48fc4;
  }

  bool action(uint32_t value) override {
    port_a = 0;
    port_b = 0;
    return true;
  }
};

class AmanatsuMouse : public AmanatsuDevice {
public:
  AmanatsuMouse() {
    magic = 0x4d4f5553;
  }

  bool action(uint32_t value) override {
    port_a = 0;
    port_b = 0;
    return true;
  }
};

class Amanatsu : public CitronPort {
  friend class AmanatsuController;

public:
  Amanatsu(Platform &platform) {
    auto self = std::shared_ptr<Amanatsu>(this, [](auto) {});

    for (auto i = 0; i < 5; i++)
      platform.set_port(0x30 + i, self);

    set_device(0, std::make_shared<AmanatsuController>(self));
    set_device(1, std::make_shared<AmanatsuKeyboard>());
    set_device(2, std::make_shared<AmanatsuMouse>());
  }

  void set_device(int num, std::shared_ptr<AmanatsuDevice> device) {
    if (m_devices[num] != nullptr)
      throw std::runtime_error("Device slot already in use");

    m_devices[num] = device;
  }

  void reset() override {
    for (auto device : m_devices) {
      if (device)
        device->reset();
    }
  }

  bool read(uint32_t port, BusSize size, uint32_t &value) override {
    if (port == 0x30) { // Get current device
      value = m_current;
      return true;
    } else if (port == 0x31) { // Get device magic
      if (auto device = m_devices[m_current])
        value = device->magic;
      else
        value = 0;

      return true;
    } else if (port == 0x32) { // ???
      if (m_devices[m_current]) {
        value = 0;
        return true;
      }
    } else if (port == 0x33) { // Get port A
      if (auto device = m_devices[m_current]) {
        value = device->port_a;
        return true;
      }
    } else if (port == 0x34) { // Get port B
      if (auto device = m_devices[m_current]) {
        value = device->port_b;
        return true;
      }
    }

    return false;
  }

  bool write(uint32_t port, BusSize size, uint32_t value) override {
    if (port == 0x30 && value < 16) { // Set current device
      m_current = value;
      return true;
    } else if (port == 0x31) { // Set device magic
      return false;
    } else if (port == 0x32) { // Send command
      if (auto device = m_devices[m_current])
        return device->action(value);
    } else if (port == 0x33) { // Set port A
      if (auto device = m_devices[m_current]) {
        device->port_a = value;
        return true;
      }
    } else if (port == 0x34) { // Set port B
      if (auto device = m_devices[m_current]) {
        device->port_b = value;
        return true;
      }
    }

    return false;
  }

private:
  std::shared_ptr<AmanatsuDevice> m_devices[16];

  uint32_t m_current = 0;
};

inline bool AmanatsuController::action(uint32_t value) {
  switch (value) {
  case 1: // Enable interrupts on device
    if (port_b < 16 && m_amanatsu->m_devices[port_b]) {
      m_amanatsu->m_devices[port_b]->interrupt_line = 48 + port_b;
      return true;
    }
    break;
  case 2: // Reset
    m_amanatsu->reset();
    return true;
  case 3: // Disable interrupts on device
    if (port_b < 16 && m_amanatsu->m_devices[port_b]) {
      m_amanatsu->m_devices[port_b]->interrupt_line = 0;
      return true;
    }
    break;
  }

  return false;
}
