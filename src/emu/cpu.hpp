#pragma once

#include <cstdint>

#include "bus.hpp"
#include "lsic.hpp"
#include "platform.hpp"

inline uint32_t sign_ext(uint32_t value, uint32_t bits) {
  return (int32_t)(value << bits) >> bits;
}

inline uint32_t sign_ext_23(uint32_t value) {
  return sign_ext(value, 9);
}

inline uint32_t sign_ext_18(uint32_t value) {
  return sign_ext(value, 14);
}

inline uint32_t sign_ext_5(uint32_t value) {
  return sign_ext(value, 27);
}

inline uint32_t sign_ext_16(uint32_t value) {
  return sign_ext(value, 16);
}

inline uint32_t rotate_right(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

inline uint32_t less_than(uint32_t lhs, uint32_t rhs, bool is_signed) {
  if (is_signed)
    return (int32_t)lhs < (int32_t)rhs ? 1 : 0;
  else
    return lhs < rhs ? 1 : 0;
}

inline uint32_t shift(uint32_t lhs, uint32_t rhs, uint32_t shift_type) {
  switch (shift_type) {
  case 0b00: return lhs << rhs;             // Shift Left
  case 0b01: return lhs >> rhs;             // Shift Right
  case 0b10: return (int32_t)lhs >> rhs;    // Arithmetic Shift
  case 0b11: return rotate_right(lhs, rhs); // Rotate Right
  }

  throw std::runtime_error("Unknown shift type");
}

enum StatusRegister : uint32_t {
  RS_USER = 1,
  RS_INT = 2,
  RS_MMU = 4,
};

enum Registers : uint32_t {
  REG_LR = 31,
};

enum ControlRegisters : uint32_t {
  CTL_RS = 0,
  CTL_ECAUSE = 1,
  CTL_ERS = 2,
  CTL_EPC = 3,
  CTL_EVEC = 4,
  CTL_PGTB = 5,
  CTL_ASID = 6,
  CTL_EBADADDR = 7,
  CTL_CPUID = 8,
  CTL_FWVEC = 9,
};

enum ExceptionType : uint32_t {
  EXC_INTERRUPT = 1,
  EXC_SYSCALL = 2,
  EXC_FWCALL = 3,
  EXC_BUSERROR = 4,
  EXC_BRKPOINT = 6,
  EXC_INVINST = 7,
  EXC_INVPRVG = 8,
  EXC_UNALIGNED = 9,
  EXC_PAGEFAULT = 12,
  EXC_PAGEWRITE = 13,
};

static const char *exception_names[] = {
    [EXC_INTERRUPT] = "EXC_INTERRUPT", [EXC_SYSCALL] = "EXC_SYSCALL",     [EXC_FWCALL] = "EXC_FWCALL",   [EXC_BUSERROR] = "EXC_BUSERROR",
    [EXC_BRKPOINT] = "EXC_BRKPOINT",   [EXC_INVINST] = "EXC_INVINST",     [EXC_INVPRVG] = "EXC_INVPRVG", [EXC_UNALIGNED] = "EXC_UNALIGNED",
    [EXC_PAGEFAULT] = "EXC_PAGEFAULT", [EXC_PAGEWRITE] = "EXC_PAGEWRITE",
};

class Cpu {
public:
  Cpu(Bus &bus, InterruptController &int_ctl) : m_bus(bus), m_int_ctl(int_ctl) {
    reset();
  }

  void reset() {
    m_pc = 0xFFFE0000;
    m_ctl_regs[CTL_RS] = 0;
    m_ctl_regs[CTL_EVEC] = 0;
    m_ctl_regs[CTL_CPUID] = 0x80060000;
    m_exc = 0;
  }

  bool execute() {
    if (m_exc || (m_ctl_regs[CTL_RS] & RS_INT && m_int_ctl.interrupt_pending())) {
      auto exc_vector = 0;
      auto new_state = m_ctl_regs[CTL_RS] & 0xfffffffc;

      if (m_exc == EXC_FWCALL) {
        exc_vector = m_ctl_regs[CTL_FWVEC];
        new_state &= 0xfffffff8;
      } else {
        if (new_state & 128)
          new_state &= 0xfffffff8;

        exc_vector = m_ctl_regs[CTL_EVEC];
      }

      if (!exc_vector) {
        reset();
      } else {
        if (!m_exc)
          m_exc = EXC_INTERRUPT;

        m_ctl_regs[CTL_EPC] = m_pc;
        m_ctl_regs[CTL_ECAUSE] = m_exc;
        m_ctl_regs[CTL_ERS] = m_ctl_regs[CTL_RS];
        m_ctl_regs[CTL_RS] = new_state;
        m_pc = exc_vector;
      }

      m_exc = 0;
    }

    auto instruction = 0u;
    auto current_pc = m_pc;

    m_pc += 4;

    if (!mem_read(current_pc, BUS_LONG, instruction))
      return false;

    auto major = instruction & 0b111;
    auto major_op = instruction & 0b111111;

    if (major == 0b111) { // JAL
      m_regs[REG_LR] = m_pc;
      m_pc = (current_pc & 0x80000000) | ((instruction >> 3) << 2);
      return true;
    } else if (major == 0b110) { // J
      m_pc = (current_pc & 0x80000000) | ((instruction >> 3) << 2);
      return true;
    } else if (major_op == 0b111001) {
      return handle_opcode_111001(instruction);
    } else if (major_op == 0b110001) {
      return handle_opcode_110001(instruction);
    } else if (major_op == 0b101001) {
      return handle_opcode_101001(instruction);
    } else {
      return handle_opcode_major(major_op, instruction, current_pc);
    }

    raise_exception(EXC_INVINST);
    return false;
  }

private:
  void raise_exception(uint32_t exception) {
    auto nested = m_exc != 0;

    m_exc = exception;

    if ((exception == EXC_INTERRUPT || exception == EXC_SYSCALL || exception == EXC_FWCALL || exception == EXC_BRKPOINT) && !nested)
      return;

    printf("CPU raised exception %d (%s)\n", exception, exception_names[exception]);
    printf("Register dump:\n");

    for (auto i = 0; i < 8; i++) {
      printf("  %08x %08x %08x %08x\n", m_regs[i * 8], m_regs[i * 8 + 1], m_regs[i * 8 + 2], m_regs[i * 8 + 3]);
    }

    printf("Control registers dump: \n");
    printf("  CTL_RS = %08x\n", m_ctl_regs[CTL_RS]);
    printf("  CTL_ECAUSE = %08x\n", m_ctl_regs[CTL_ECAUSE]);
    printf("  CTL_ERS = %08x\n", m_ctl_regs[CTL_ERS]);
    printf("  CTL_EPC = %08x\n", m_ctl_regs[CTL_EPC]);
    printf("  CTL_EVEC = %08x\n", m_ctl_regs[CTL_EVEC]);
    printf("  CTL_PGTB = %08x\n", m_ctl_regs[CTL_PGTB]);
    printf("  CTL_ASID = %08x\n", m_ctl_regs[CTL_ASID]);
    printf("  CTL_EBADADDR = %08x\n", m_ctl_regs[CTL_EBADADDR]);
    printf("  CTL_CPUID = %08x\n", m_ctl_regs[CTL_CPUID]);
    printf("  CTL_FWVEC = %08x\n", m_ctl_regs[CTL_FWVEC]);

    if (nested) {
      printf("CPU raised an exception while another one is being handled!\n");
      exit(1);
    }
  }

  // TODO: Implement TLB
  bool translate_va(uint32_t addr, uint32_t &phys, bool is_writing) {
    auto virt_page_num = addr >> 12;
    auto virt_page_off = addr & 0xfff;

    uint32_t pde;
    uint32_t tlb_high;

    if (!m_bus.mem_read(m_ctl_regs[CTL_PGTB] + ((addr >> 22) << 2), BUS_LONG, pde)) {
      m_ctl_regs[CTL_EBADADDR] = m_ctl_regs[CTL_PGTB] + ((addr >> 22) << 2);
      raise_exception(EXC_BUSERROR);
      return false;
    }

    if (!(pde & 0x1)) {
      m_ctl_regs[CTL_EBADADDR] = addr;
      raise_exception(is_writing ? EXC_PAGEWRITE : EXC_PAGEFAULT);
      return false;
    }

    if (!m_bus.mem_read(((pde >> 5) << 12) + ((virt_page_num & 0x3ff) << 2), BUS_LONG, tlb_high)) {
      m_ctl_regs[CTL_EBADADDR] = ((pde >> 5) << 12) + ((virt_page_num & 0x3ff) << 2);
      raise_exception(EXC_BUSERROR);
      return false;
    }

    if (!(tlb_high & 0x1)) {
      m_ctl_regs[CTL_EBADADDR] = addr;
      raise_exception(is_writing ? EXC_PAGEWRITE : EXC_PAGEFAULT);
      return false;
    }

    auto phys_page_num = ((tlb_high >> 5) & 0xfffff) << 12;

    phys = phys_page_num + virt_page_off;

    return true;
  }

  bool mem_read(uint32_t addr, BusSize size, uint32_t &value) {
    if (addr < 0x1000 || addr >= 0xfffff000) {
      m_ctl_regs[CTL_EBADADDR] = addr;
      raise_exception(EXC_PAGEFAULT);
      return false;
    }

    if (m_ctl_regs[CTL_RS] & RS_MMU && !translate_va(addr, addr, false))
      return false; // Exception already raised inside `traslate_va`

    if (!m_bus.mem_read(addr, size, value)) {
      m_ctl_regs[CTL_EBADADDR] = addr;
      raise_exception(EXC_BUSERROR);
      return false;
    }

    return true;
  }

  bool mem_write(uint32_t addr, BusSize size, uint32_t value) {
    if (addr < 0x1000 || addr >= 0xfffff000) {
      m_ctl_regs[CTL_EBADADDR] = addr;
      raise_exception(EXC_PAGEWRITE);
      return false;
    }

    if (m_ctl_regs[CTL_RS] & RS_MMU && !translate_va(addr, addr, true))
      return false; // Exception already raised inside `traslate_va`

    if (!m_bus.mem_write(addr, size, value)) {
      m_ctl_regs[CTL_EBADADDR] = addr;
      raise_exception(EXC_BUSERROR);
      return false;
    }

    return true;
  }

  bool handle_opcode_111001(uint32_t instruction) {
    auto function = instruction >> 28;
    auto shift_type = (instruction >> 26) & 0b11;
    auto shift_count = (instruction >> 21) & 0b11111;

    auto reg_d = (instruction >> 6) & 0b11111;
    auto reg_a = (instruction >> 11) & 0b11111;
    auto reg_b = (instruction >> 16) & 0b11111;
    auto value = shift_count ? shift(m_regs[reg_b], shift_count, shift_type) : m_regs[reg_b];

    if (reg_d || (function >= 9 && function <= 11)) {
      switch (function) {
      case 0: m_regs[reg_d] = ~(m_regs[reg_a] | value); return true;                        // NOR
      case 1: m_regs[reg_d] = m_regs[reg_a] | value; return true;                           // OR
      case 2: m_regs[reg_d] = m_regs[reg_a] ^ value; return true;                           // XOR
      case 3: m_regs[reg_d] = m_regs[reg_a] & value; return true;                           // AND
      case 4: m_regs[reg_d] = less_than(m_regs[reg_a], value, true); return true;           // SLT signed
      case 5: m_regs[reg_d] = less_than(m_regs[reg_a], value, false); return true;          // SLT
      case 6: m_regs[reg_d] = m_regs[reg_a] - value; return true;                           // SUB
      case 7: m_regs[reg_d] = m_regs[reg_a] + value; return true;                           // ADD
      case 8: m_regs[reg_d] = shift(m_regs[reg_b], m_regs[reg_a], shift_type); return true; // Shift
      case 9: return mem_write(m_regs[reg_a] + value, BUS_LONG, m_regs[reg_d]);             // Move reg_d to long[reg_a + reg_b]
      case 10: return mem_write(m_regs[reg_a] + value, BUS_INT, m_regs[reg_d] & 0xffff);    // Move reg_d to int[reg_a + reg_b]
      case 11: return mem_write(m_regs[reg_a] + value, BUS_BYTE, m_regs[reg_d] & 0xff);     // Move reg_d to byte[reg_a + reg_b]
      case 13: return mem_read(m_regs[reg_a] + value, BUS_LONG, m_regs[reg_d]);             // Move long[reg_a + reg_b] to reg_d
      case 14: return mem_read(m_regs[reg_a] + value, BUS_INT, m_regs[reg_d]);              // Move int[reg_a + reg_b] to reg_d
      case 15: return mem_read(m_regs[reg_a] + value, BUS_BYTE, m_regs[reg_d]);             // Move byte[reg_a + reg_b] to reg_d
      }
    }

    raise_exception(EXC_INVINST);
    return false;
  }

  bool handle_opcode_110001(uint32_t instruction) {
    auto function = instruction >> 28;

    auto reg_d = (instruction >> 6) & 0b11111;
    auto reg_a = (instruction >> 11) & 0b11111;
    auto reg_b = (instruction >> 16) & 0b11111;

    switch (function) {
    case 0: // SYS
      raise_exception(EXC_SYSCALL);
      return true;
    case 1: // BRK
      raise_exception(EXC_BRKPOINT);
      return true;
    case 8: // SC
      if (m_locked && !mem_write(m_regs[reg_a], BUS_LONG, m_regs[reg_b]))
        return false;
      if (reg_d != 0)
        m_regs[reg_d] = m_locked;
      return true;
    case 9: // LL
      m_locked = true;
      if (reg_d != 0 && !mem_read(m_regs[reg_a], BUS_LONG, m_regs[reg_d]))
        return false;
      return true;
    case 11: // MOD
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_b] ? m_regs[reg_a] % m_regs[reg_b] : 0;
      return true;
    case 12: // DIV signed
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_b] ? (int32_t)m_regs[reg_a] / (int32_t)m_regs[reg_b] : 0;
      return true;
    case 13: // DIV
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_b] ? m_regs[reg_a] / m_regs[reg_b] : 0;
      return true;
    case 15: // MUL
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_a] * m_regs[reg_b];
      return true;
    }

    raise_exception(EXC_INVINST);
    return false;
  }

  bool handle_opcode_101001(uint32_t instruction) {
    if (m_ctl_regs[CTL_RS] & RS_USER) {
      raise_exception(EXC_INVPRVG);
      return false;
    }

    auto function = instruction >> 28;

    auto reg_d = (instruction >> 6) & 0b11111;
    auto reg_a = (instruction >> 11) & 0b11111;
    auto reg_b = (instruction >> 16) & 0b11111;

    switch (function) {
    case 10: // FWC
      raise_exception(EXC_FWCALL);
      return true;
    case 11: // RFE
      m_locked = false;
      m_pc = m_ctl_regs[CTL_EPC];
      m_ctl_regs[CTL_RS] = m_ctl_regs[CTL_ERS];
      return true;
    case 12: // HLT
      m_halt = true;
      return true;
    case 13: // FTLB
      // TODO: Implement flushing TLB
      return true;
    case 14: // MTCR
      m_ctl_regs[reg_b] = m_regs[reg_a];
      return true;
    case 15: // MFCR
      if (reg_d != 0)
        m_regs[reg_d] = m_ctl_regs[reg_b];
      return true;
    }

    raise_exception(EXC_INVINST);
    return false;
  }

  bool handle_opcode_major(uint32_t major_op, uint32_t instruction, uint32_t current_pc) {
    auto imm = instruction >> 16;
    auto reg_d = (instruction >> 6) & 0b11111;
    auto reg_a = (instruction >> 11) & 0b11111;

    switch (major_op) {
    case 61: // BEQ
      if (m_regs[reg_d] == 0)
        m_pc = current_pc + sign_ext_23((instruction >> 11) << 2);
      return true;
    case 53: // BNE
      if (m_regs[reg_d] != 0)
        m_pc = current_pc + sign_ext_23((instruction >> 11) << 2);
      return true;
    case 45: // BLT
      if ((int32_t)m_regs[reg_d] < 0)
        m_pc = current_pc + sign_ext_23((instruction >> 11) << 2);
      return true;
    case 60: // ADDI
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_a] + imm;
      return true;
    case 52: // SUBI
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_a] - imm;
      return true;
    case 44: // SLTI
      if (reg_d != 0)
        m_regs[reg_d] = less_than(m_regs[reg_a], imm, false);
      return true;
    case 36: // SLTI signed
      if (reg_d != 0)
        m_regs[reg_d] = less_than(m_regs[reg_a], sign_ext_16(imm), true);
      return true;
    case 28: // ANDI
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_a] & imm;
      return true;
    case 20: // XORI
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_a] ^ imm;
      return true;
    case 12: // ORI
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_a] | imm;
      return true;
    case 4: // LUI
      if (reg_d != 0)
        m_regs[reg_d] = m_regs[reg_a] | imm << 16;
      return true;
    case 56: // JALR
      if (reg_d != 0)
        m_regs[reg_d] = m_pc;
      m_pc = m_regs[reg_a] + sign_ext_18(imm << 2);
      return true;
    case 59: return reg_d ? mem_read(m_regs[reg_a] + imm, BUS_BYTE, m_regs[reg_d]) : true;        // Move byte[reg_a + imm] into reg_d
    case 51: return reg_d ? mem_read(m_regs[reg_a] + (imm << 1), BUS_INT, m_regs[reg_d]) : true;  // Move int[reg_a + imm] into reg_d
    case 43: return reg_d ? mem_read(m_regs[reg_a] + (imm << 2), BUS_LONG, m_regs[reg_d]) : true; // Move long[reg_a + imm] into reg_d
    case 58: return mem_write(m_regs[reg_d] + imm, BUS_BYTE, m_regs[reg_a]);                      // Move reg_a into byte[reg_d + imm]
    case 50: return mem_write(m_regs[reg_d] + (imm << 1), BUS_INT, m_regs[reg_a]);                // Move reg_a into int[reg_d + imm]
    case 42: return mem_write(m_regs[reg_d] + (imm << 2), BUS_LONG, m_regs[reg_a]);               // Move reg_a into long[reg_d + imm]
    case 26: return mem_write(m_regs[reg_d] + imm, BUS_BYTE, sign_ext_5(reg_a));                  // Move reg_a into byte[reg_d + imm5]
    case 18: return mem_write(m_regs[reg_d] + (imm << 1), BUS_INT, sign_ext_5(reg_a));            // Move reg_a into int[reg_d + imm5]
    case 10: return mem_write(m_regs[reg_d] + (imm << 2), BUS_LONG, sign_ext_5(reg_a));           // Move reg_a into long[reg_d + imm5]
    }

    raise_exception(EXC_INVINST);
    return false;
  }

private:
  Bus &m_bus;
  InterruptController &m_int_ctl;

  uint32_t m_pc = 0;
  uint32_t m_exc = 0;
  uint32_t m_regs[32] = {0};
  uint32_t m_ctl_regs[32] = {0};

  bool m_halt = false;
  bool m_locked = false;
};
