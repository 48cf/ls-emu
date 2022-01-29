#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <memory>

#include "lsic.hpp"
#include "platform.hpp"

class AmanatsuDevice : public std::enable_shared_from_this<AmanatsuDevice> {
public:
  int interrupt_line = 0;

  uint32_t magic = 0;
  uint32_t port_a = 0;
  uint32_t port_b = 0;

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

class Amanatsu : public CitronPort {
  friend class AmanatsuController;

public:
  Amanatsu(Platform &platform) {
    auto self = std::shared_ptr<Amanatsu>(this, [](auto) {});

    for (auto i = 0; i < 5; i++)
      platform.set_port(0x30 + i, self);

    set_device(0, std::make_shared<AmanatsuController>(self));
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

  bool read(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t &value) override {
    if (port == 0x30) { // Get current device
      value = m_selected;
      return true;
    } else if (port == 0x31) { // Get device magic
      if (auto device = m_devices[m_selected])
        value = device->magic;
      else
        value = 0;

      return true;
    } else if (port == 0x32) { // ???
      if (m_devices[m_selected]) {
        value = 0;
        return true;
      }
    } else if (port == 0x33) { // Get port A
      if (auto device = m_devices[m_selected]) {
        value = device->port_a;
        return true;
      }
    } else if (port == 0x34) { // Get port B
      if (auto device = m_devices[m_selected]) {
        value = device->port_b;
        return true;
      }
    }

    return false;
  }

  bool write(InterruptController &int_ctl, uint32_t port, BusSize size, uint32_t value) override {
    if (port == 0x30 && value < 16) { // Set current device
      m_selected = value;
      return true;
    } else if (port == 0x31) { // Set device magic
      return false;
    } else if (port == 0x32) { // Send command
      if (auto device = m_devices[m_selected])
        return device->action(value);
    } else if (port == 0x33) { // Set port A
      if (auto device = m_devices[m_selected]) {
        device->port_a = value;
        return true;
      }
    } else if (port == 0x34) { // Set port B
      if (auto device = m_devices[m_selected]) {
        device->port_b = value;
        return true;
      }
    }

    return false;
  }

private:
  std::shared_ptr<AmanatsuDevice> m_devices[16];

  uint32_t m_selected = 0;
};

// TODO: Rewrite this at some point zzz
// 99% of code is copied from https://github.com/limnarch/limnemu/blob/main/src/keybd.c lmao
class AmanatsuKeyboard : public AmanatsuDevice {
  constexpr static int key_map[SDL_NUM_SCANCODES] = {
      [SDL_SCANCODE_A] = 0x01,
      [SDL_SCANCODE_B] = 0x02,
      [SDL_SCANCODE_C] = 0x03,
      [SDL_SCANCODE_D] = 0x04,
      [SDL_SCANCODE_E] = 0x05,
      [SDL_SCANCODE_F] = 0x06,
      [SDL_SCANCODE_G] = 0x07,
      [SDL_SCANCODE_H] = 0x08,
      [SDL_SCANCODE_I] = 0x09,
      [SDL_SCANCODE_J] = 0x0A,
      [SDL_SCANCODE_K] = 0x0B,
      [SDL_SCANCODE_L] = 0x0C,
      [SDL_SCANCODE_M] = 0x0D,
      [SDL_SCANCODE_N] = 0x0E,
      [SDL_SCANCODE_O] = 0x0F,
      [SDL_SCANCODE_P] = 0x10,
      [SDL_SCANCODE_Q] = 0x11,
      [SDL_SCANCODE_R] = 0x12,
      [SDL_SCANCODE_S] = 0x13,
      [SDL_SCANCODE_T] = 0x14,
      [SDL_SCANCODE_U] = 0x15,
      [SDL_SCANCODE_V] = 0x16,
      [SDL_SCANCODE_W] = 0x17,
      [SDL_SCANCODE_X] = 0x18,
      [SDL_SCANCODE_Y] = 0x19,
      [SDL_SCANCODE_Z] = 0x1A,
      [SDL_SCANCODE_0] = 0x1B,
      [SDL_SCANCODE_1] = 0x1C,
      [SDL_SCANCODE_2] = 0x1D,
      [SDL_SCANCODE_3] = 0x1E,
      [SDL_SCANCODE_4] = 0x1F,
      [SDL_SCANCODE_5] = 0x20,
      [SDL_SCANCODE_6] = 0x21,
      [SDL_SCANCODE_7] = 0x22,
      [SDL_SCANCODE_8] = 0x23,
      [SDL_SCANCODE_9] = 0x24,
      [SDL_SCANCODE_SEMICOLON] = 0x25,
      [SDL_SCANCODE_SPACE] = 0x26,
      [SDL_SCANCODE_TAB] = 0x27,
      [SDL_SCANCODE_MINUS] = 0x28,
      [SDL_SCANCODE_EQUALS] = 0x29,
      [SDL_SCANCODE_LEFTBRACKET] = 0x2A,
      [SDL_SCANCODE_RIGHTBRACKET] = 0x2B,
      [SDL_SCANCODE_BACKSLASH] = 0x2C,
      [SDL_SCANCODE_NONUSHASH] = 0x2C,
      [SDL_SCANCODE_SLASH] = 0x2E,
      [SDL_SCANCODE_PERIOD] = 0x2F,
      [SDL_SCANCODE_APOSTROPHE] = 0x30,
      [SDL_SCANCODE_COMMA] = 0x31,
      [SDL_SCANCODE_GRAVE] = 0x32,
      [SDL_SCANCODE_RETURN] = 0x33,
      [SDL_SCANCODE_BACKSPACE] = 0x34,
      [SDL_SCANCODE_CAPSLOCK] = 0x35,
      [SDL_SCANCODE_ESCAPE] = 0x36,
      [SDL_SCANCODE_LEFT] = 0x37,
      [SDL_SCANCODE_RIGHT] = 0x38,
      [SDL_SCANCODE_DOWN] = 0x39,
      [SDL_SCANCODE_UP] = 0x3A,
      [SDL_SCANCODE_LCTRL] = 0x51,
      [SDL_SCANCODE_RCTRL] = 0x52,
      [SDL_SCANCODE_LSHIFT] = 0x53,
      [SDL_SCANCODE_RSHIFT] = 0x54,
      [SDL_SCANCODE_LALT] = 0x55,
      [SDL_SCANCODE_RALT] = 0x56,
      [SDL_SCANCODE_KP_DIVIDE] = 0x2E,
      [SDL_SCANCODE_KP_MINUS] = 0x28,
      [SDL_SCANCODE_KP_ENTER] = 0x33,
      [SDL_SCANCODE_KP_0] = 0x1B,
      [SDL_SCANCODE_KP_1] = 0x1C,
      [SDL_SCANCODE_KP_2] = 0x1D,
      [SDL_SCANCODE_KP_3] = 0x1E,
      [SDL_SCANCODE_KP_4] = 0x1F,
      [SDL_SCANCODE_KP_5] = 0x20,
      [SDL_SCANCODE_KP_6] = 0x21,
      [SDL_SCANCODE_KP_7] = 0x22,
      [SDL_SCANCODE_KP_8] = 0x23,
      [SDL_SCANCODE_KP_9] = 0x24,
      [SDL_SCANCODE_KP_PERIOD] = 0x2F,
  };

public:
  AmanatsuKeyboard(Amanatsu &amanatsu) {
    magic = 0x8fc48fc4;

    amanatsu.set_device(1, std::shared_ptr<AmanatsuKeyboard>(this, [](auto) {}));
  }

  void reset() override {
    port_a = 0xffff;

    memset(m_is_pressed, false, sizeof(m_is_pressed));
    memset(m_outstanding_press, false, sizeof(m_outstanding_press));
    memset(m_outstanding_release, false, sizeof(m_outstanding_release));
  }

  void handle_key_event(const SDL_KeyboardEvent &ev) {
    if (auto ch = key_map[ev.keysym.scancode]) {
      m_is_pressed[ch - 1] = ev.type == SDL_KEYDOWN;

      if (ev.type == SDL_KEYDOWN)
        m_outstanding_press[ch - 1] = true;
      else if (ev.type == SDL_KEYUP)
        m_outstanding_release[ch - 1] = true;
    }
  }

  bool action(uint32_t value) override {
    if (value == 1) {
      for (int i = 0; i <= 85; i++) {
        if (m_outstanding_release[i]) {
          port_a = i | 0x8000;
          m_outstanding_release[i] = false;
          m_outstanding_press[i] = false;
          return true;
        } else if (m_outstanding_press[i]) {
          port_a = i;
          m_outstanding_press[i] = false;
          return true;
        }
      }

      port_a = 0xffff;
    } else if (value == 2) {
      reset();
    } else if (value == 3) {
      if (port_a <= 85)
        port_a = m_is_pressed[port_a] ? 1 : 0;
    }

    return true;
  }

private:
  bool m_is_pressed[85];
  bool m_outstanding_press[85];
  bool m_outstanding_release[85];
};

// TODO: Implement the mouse at some point :^)
class AmanatsuMouse : public AmanatsuDevice {
public:
  AmanatsuMouse(Amanatsu &amanatsu) {
    magic = 0x4d4f5553;

    amanatsu.set_device(2, std::shared_ptr<AmanatsuMouse>(this, [](auto) {}));
  }

  bool action(uint32_t value) override {
    port_a = 0;
    return true;
  }
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
