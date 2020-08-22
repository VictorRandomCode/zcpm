#include <array>
#include <cstdint>
#include <ostream>
#include <string>

#include <boost/format.hpp>

#include <zcpm/core/processor.hpp>
#include <zcpm/core/registers.hpp>

#include "writer.hpp"

namespace
{

  const std::array<const char*, 8> ByteRegMask{ "B", "C", "D", "E", "H", "L", "(HL)", "A" };
  const std::array<const char*, 4> WordRegMask{ "BC", "DE", "HL", "SP" };
  const std::array<const char*, 4> WordRegMaskQq{ "BC", "DE", "HL", "AF" };
  const std::array<const char*, 8> CondMask{ "NZ", "Z", "NC", "C", "PO", "PE", "P", "M" };

  // A 8-bit literal as a 2 digit hex string
  auto byte(uint8_t x)
  {
    return (boost::format("%02X") % static_cast<unsigned int>(x)).str();
  }

  // A 16-bit literal as a 4 digit hex string
  auto word(uint8_t low, uint8_t high)
  {
    return (boost::format("%04X") % (high << 8 | low)).str();
  }

  // Dereference NN from (string)
  auto nn_string(uint8_t low, uint8_t high, const std::string& s)
  {
    return (boost::format("(%04X),%s") % (high << 8 | low) % s).str();
  }

  // Dereference (string) from NN
  auto string_nn(uint8_t low, uint8_t high, const std::string& s)
  {
    return (boost::format("%s,(%04X)") % s % (high << 8 | low)).str();
  }

  // Dereference N from (string)
  auto n_string(uint8_t n, const std::string& s)
  {
    return (boost::format("(%02X),%s") % static_cast<unsigned int>(n) % s).str();
  }

  // Dereference (string) from N
  // auto string_n(uint8_t n, const std::string& s)
  //{
  //  return (boost::format("%s,(%02X)") % s % static_cast<unsigned int>(n)).str();
  //}

  auto hl_ss(uint8_t ss)
  {
    return (boost::format("HL,%s") % WordRegMask[ss]).str();
  }

  auto byte_register(uint8_t r)
  {
    return ByteRegMask[r];
  }

  auto qq_word_register(uint8_t qq)
  {
    return WordRegMaskQq[qq];
  }

  // r,n where r is a byte register and n is a 8-bit literal
  auto r_n(uint8_t r, uint8_t n)
  {
    return (boost::format("%s,%02X") % ByteRegMask[r] % static_cast<unsigned int>(n)).str();
  }

  // r,r where r1 and r2 are both byte registers
  auto r_r(uint8_t r1, uint8_t r2)
  {
    return (boost::format("%s,%s") % ByteRegMask[r1] % ByteRegMask[r2]).str();
  }

  // dd,nn where dd is a word register and nn is a 16-bit literal
  auto dd_nn(uint8_t dd, uint8_t nn_low, uint8_t nn_high)
  {
    const uint16_t nn = (nn_high << 8) | nn_low;
    return (boost::format("%s,%04X") % WordRegMask[dd] % nn).str();
  }

  // (nn),dd where dd is a word register and nn is a 16-bit literal
  auto inn_dd(uint8_t dd, uint8_t nn_low, uint8_t nn_high)
  {
    const uint16_t nn = (nn_high << 8) | nn_low;
    return (boost::format("(%04X),%s") % nn % WordRegMask[dd]).str();
  }

  // dd,(nn) where dd is a word register and nn is a 16-bit literal
  auto dd_inn(uint8_t dd, uint8_t nn_low, uint8_t nn_high)
  {
    const uint16_t nn = (nn_high << 8) | nn_low;
    return (boost::format("%s,(%04X)") % WordRegMask[dd] % nn).str();
  }

  // cc,pq where cc is a 3-bit condition and pq is a 16-bit literal
  auto cc_pq(uint8_t cc, uint8_t pq_low, uint8_t pq_high)
  {
    const uint16_t pq = (pq_high << 8) | pq_low;
    return (boost::format("%s,%04X") % CondMask[cc] % pq).str();
  }

  // Relative target as a 4 digit hex value (PC+e)
  auto offset(uint16_t pc, uint8_t e)
  {
    const int8_t ee = e;
    const uint16_t dest = pc + 2 + ee;
    return (boost::format("%04X") % dest).str();
  }

  // cc,e where cc is a 2-bit condition and e is a relative jump, which
  // we combine with pc for display purposes for compatibility with DebugZ
  auto cc_offset(uint8_t cc, uint8_t e, uint16_t pc)
  {
    const int8_t ee = e;
    const uint16_t dest = pc + 2 + ee;
    return (boost::format("%s,%04X") % CondMask[cc] % dest).str();
  }

  // "r,(reg+d)"  where r is a 8-bit register index and reg is an index register name and d is an offset
  auto r_ind_offset(uint8_t r, const std::string& reg, uint8_t offset)
  {
    const int8_t o = offset;
    return (boost::format("%s,(%s+%02X)") % ByteRegMask[r] % reg % static_cast<short>(o)).str();
  }

  // "(reg+d),r"  where r is a 8-bit register index and reg is an index register name and d is an offset
  auto ind_offset_r(uint8_t r, const std::string& reg, uint8_t offset)
  {
    const int8_t o = offset;
    return (boost::format("(%s+%02X),%s") % reg % static_cast<short>(o) % ByteRegMask[r]).str();
  }

  // TODO: Collate string constants

  std::tuple<size_t, std::string, std::string> disassemble_cb(uint8_t op2, uint8_t /*op3*/, uint8_t /*op4*/)
  {
    // First check for specific opcodes
    switch (op2)
    {
    // TODO: none handled here yet
    default:
      // Fall through to bytefield checks below
      break;
    }

    // Now check for bytefields
    if ((op2 & 0xF8) == 0x00)
    {
      const uint8_t r = op2 & 0x07;
      return { 2, "RLC", ByteRegMask[r] };
    }
    if ((op2 & 0xF8) == 0x08)
    {
      const uint8_t r = op2 & 0x07;
      return { 2, "RRC", ByteRegMask[r] };
    }
    if ((op2 & 0xF8) == 0x18)
    {
      const uint8_t r = op2 & 0x07;
      return { 2, "RR", ByteRegMask[r] };
    }
    if ((op2 & 0xF8) == 0x38)
    {
      const uint8_t r = op2 & 0x07;
      return { 2, "SRL", ByteRegMask[r] };
    }

    // No match
    return { 0, (boost::format("?? CB %02X") % static_cast<unsigned short>(op2)).str(), "" };
  }

  std::tuple<size_t, std::string, std::string> disassemble_dd(uint8_t op2, uint8_t op3, uint8_t /*op4*/)
  {
    // First check for specific opcodes
    switch (op2)
    {
    case 0xE1: return { 2, "POP", "IX" };
    case 0xE5: return { 2, "PUSH", "IX" };
    default:
      // Fall through to bytefield checks below
      break;
    }

    // Now check for bytefields
    if ((op2 & 0xC7) == 0x46)
    {
      const uint8_t r = (op2 >> 3) & 0x07;
      return { 3, "LD", r_ind_offset(r, "IX", op3) };
    }
    if ((op2 & 0xF8) == 0x70)
    {
      const uint8_t r = op2 & 0x07;
      return { 3, "LD", ind_offset_r(r, "IX", op3) };
    }

    // No match
    return { 0, (boost::format("?? DD %02X") % static_cast<unsigned short>(op2)).str(), "" };
  }

  std::tuple<size_t, std::string, std::string> disassemble_ed(uint8_t op2, uint8_t op3, uint8_t op4)
  {
    // First check for specific opcodes
    switch (op2)
    {
    case 0x44: return { 2, "NEG", "" };
    case 0xA0: return { 2, "LDI", "" };
    case 0xA8: return { 2, "LDD", "" };
    case 0xB0: return { 2, "LDIR", "" };
    case 0xB8: return { 2, "LDDR", "" };
    default:
      // Fall through to bytefield checks below
      break;
    }

    // Now check for bytefields
    if ((op2 & 0xCF) == 0x42)
    {
      const uint8_t ss = (op2 >> 4) & 0x03;
      return { 2, "SBC", "HL," + std::string(WordRegMask[ss]) };
    }
    if ((op2 & 0xCF) == 0x43)
    {
      const uint8_t dd = (op2 >> 4) & 0x03;
      return { 4, "LD", inn_dd(dd, op3, op4) };
    }
    if ((op2 & 0xCF) == 0x4A)
    {
      const uint8_t ss = (op2 >> 4) & 0x03;
      return { 2, "ADC", "HL," + std::string(WordRegMask[ss]) };
    }
    if ((op2 & 0xCF) == 0x4B)
    {
      const uint8_t dd = (op2 >> 4) & 0x03;
      return { 4, "LD", dd_inn(dd, op3, op4) };
    }

    // No match
    return { 0, (boost::format("?? ED %02X") % static_cast<unsigned short>(op2)).str(), "" };
  }

  std::tuple<size_t, std::string, std::string> disassemble_fd(uint8_t op2, uint8_t /*op3*/, uint8_t /*op4*/)
  {
    // First check for specific opcodes
    switch (op2)
    {
    case 0xE1: return { 2, "POP", "IY" };
    case 0xE5: return { 2, "PUSH", "IY" };
    default:
      // Fall through to bytefield checks below
      break;
    }

    // Now check for bytefields
    // TODO

    // No match
    return { 0, (boost::format("?? FD %02X") % static_cast<unsigned short>(op2)).str(), "" };
  }

  // Given 4 bytes at the current PC (plus the PC), returns two human-readable "words" of disassembly,
  // plus the count of bytes actually used (as each disassembled instruction can be 1-4 bytes)
  std::tuple<size_t, std::string, std::string> disassemble(uint8_t op1,
                                                           uint8_t op2,
                                                           uint8_t op3,
                                                           uint8_t op4,
                                                           uint16_t pc)
  {
    // First check for specific opcodes
    switch (op1)
    {
    case 0x00: return { 1, "NOP", "" };
    case 0x02: return { 1, "LD", "(BC),A" }; // Special case, can't use the usual lookups
    case 0x07: return { 1, "RLCA", "" };
    case 0x0A: return { 1, "LD", "A,(BC)" }; // Special case, can't use the usual lookups
    case 0x0E:
    {
      const uint8_t r = (op1 >> 3) & 0x07;
      const uint8_t n = op2;
      return { 2, "LD", r_n(r, n) };
    }
    case 0x0F: return { 1, "RRCA", "" };
    case 0x10: return { 2, "DJNZ", offset(pc, op2) };
    case 0x12: return { 1, "LD", "(DE),A" }; // Special case, can't use the usual lookups
    case 0x17: return { 1, "RLA", "" };
    case 0x18: return { 2, "JR", offset(pc, op2) };
    case 0x1A: return { 1, "LD", "A,(DE)" }; // Special case, can't use the usual lookups
    case 0x1F: return { 1, "RRA", "" };
    case 0x22: return { 3, "LD", nn_string(op2, op3, "HL") };
    case 0x2A: return { 3, "LD", string_nn(op2, op3, "HL") };
    case 0x2F: return { 1, "CPL", "" };
    case 0x32: return { 3, "LD", nn_string(op2, op3, "A") };
    case 0x37: return { 1, "SCF", "" };
    case 0x3A: return { 3, "LD", string_nn(op2, op3, "A") };
    case 0x3F: return { 1, "CCF", "" };
    case 0xC3: return { 3, "JP", word(op2, op3) };
    case 0xC6: return { 2, "ADD", "A," + byte(op2) };
    case 0x08: return { 1, "EX", "AF,AF'" };
    case 0xC9: return { 1, "RET", "" };
    case 0xCB: return disassemble_cb(op2, op3, op4);
    case 0xCD: return { 3, "CALL", word(op2, op3) };
    case 0xCE: return { 2, "ADC", "A," + byte(op2) };
    case 0xD3: return { 2, "OUT", n_string(op2, "A") };
    case 0xDD: return disassemble_dd(op2, op3, op4);
    case 0xD6: return { 2, "SUB", byte(op2) };
    case 0xD9: return { 1, "EXX", "" };
    case 0xE3: return { 1, "EX", "(SP),HL" };
    case 0xE6: return { 2, "AND", byte(op2) };
    case 0xF3: return { 1, "DI", "" };
    case 0xF6: return { 2, "OR", byte(op2) };
    case 0xFB: return { 1, "EI", "" };
    case 0xE9: return { 1, "JP", "(HL)" };
    case 0xEB: return { 1, "EX", "DE,HL" };
    case 0xED: return disassemble_ed(op2, op3, op4);
    case 0xF9: return { 1, "LD", "SP,HL" };
    case 0xFD: return disassemble_fd(op2, op3, op4);
    case 0xFE: return { 2, "CP", byte(op2) };
    default:
      // Fall through to bytefield checks below
      break;
    }

    // Byte field checks

    if ((op1 & 0xC0) == 0x40)
    {
      const uint8_t r1 = (op1 >> 3) & 0x07;
      const uint8_t r2 = op1 & 0x07;
      return { 1, "LD", r_r(r1, r2) };
    }

    if ((op1 & 0xC7) == 0x04)
    {
      const uint8_t r = (op1 >> 3) & 0x07;
      return { 1, "INC", ByteRegMask[r] };
    }
    if ((op1 & 0xC7) == 0x05)
    {
      const uint8_t r = (op1 >> 3) & 0x07;
      return { 1, "DEC", ByteRegMask[r] };
    }
    if ((op1 & 0xC7) == 0x06)
    {
      const uint8_t r = (op1 >> 3) & 0x07;
      return { 2, "LD", r_n(r, op2) };
    }
    if ((op1 & 0xC7) == 0xC0)
    {
      const uint8_t cc = (op1 >> 3) & 0x07;
      return { 1, "RET", CondMask[cc] };
    }
    if ((op1 & 0xC7) == 0xC2)
    {
      const uint8_t cc = (op1 >> 3) & 0x07;
      return { 3, "JP", cc_pq(cc, op2, op3) };
    }
    if ((op1 & 0xC7) == 0xC4)
    {
      const uint8_t cc = (op1 >> 3) & 0x07;
      return { 3, "CALL", cc_pq(cc, op2, op3) };
    }
    if ((op1 & 0xC7) == 0xC7)
    {
      const uint8_t p = (op1 >> 3) & 0x07;
      return { 1, "RST", byte(p << 3) };
    }

    if ((op1 & 0xCF) == 0x01)
    {
      const uint8_t dd = (op1 >> 4) & 0x03;
      return { 3, "LD", dd_nn(dd, op2, op3) };
    }
    if ((op1 & 0xCF) == 0x03)
    {
      const uint8_t rr = (op1 >> 4) & 0x03;
      return { 1, "INC", WordRegMask[rr] };
    }
    if ((op1 & 0xCF) == 0x09)
    {
      const uint8_t ss = (op1 >> 4) & 0x03;
      return { 1, "ADD", hl_ss(ss) };
    }
    if ((op1 & 0xCF) == 0x0B)
    {
      const uint8_t rr = (op1 >> 4) & 0x03;
      return { 1, "DEC", WordRegMask[rr] };
    }
    if ((op1 & 0xCF) == 0xC1)
    {
      const uint8_t qq = (op1 >> 4) & 0x03;
      return { 1, "POP", qq_word_register(qq) };
    }
    if ((op1 & 0xCF) == 0xC5)
    {
      const uint8_t qq = (op1 >> 4) & 0x03;
      return { 1, "PUSH", qq_word_register(qq) };
    }

    if ((op1 & 0xE7) == 0x20)
    {
      const uint8_t cc = (op1 >> 3) & 0x03;
      return { 2, "JR", cc_offset(cc, op2, pc) };
    }

    if ((op1 & 0xF8) == 0x80)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "ADD", "A," + std::string(ByteRegMask[r]) };
    }
    if ((op1 & 0xF8) == 0x88)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "ADC", ByteRegMask[r] };
    }
    if ((op1 & 0xF8) == 0x90)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "SUB", ByteRegMask[r] };
    }
    if ((op1 & 0xF8) == 0x98)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "SBC", ByteRegMask[r] };
    }
    if ((op1 & 0xF8) == 0xA0)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "AND", ByteRegMask[r] };
    }
    if ((op1 & 0xF8) == 0xA8)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "XOR", byte_register(r) };
    }
    if ((op1 & 0xF8) == 0xB0)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "OR", byte_register(r) };
    }
    if ((op1 & 0xF8) == 0xB8)
    {
      const uint8_t r = op1 & 0x07;
      return { 1, "CP", ByteRegMask[r] };
    }

    // Unhandled instruction
    return { 0,
             (boost::format("?TODO(%02X,%02X,%02X)") % static_cast<unsigned short>(op1) %
              static_cast<unsigned short>(op2) % static_cast<unsigned short>(op3))
               .str(),
             "" };
  }
} // namespace

Writer::Writer(const ZCPM::IDebuggable* p_debuggable, ZCPM::IMemory& memory, std::ostream& os)
  : m_pdebuggable(p_debuggable), m_memory(memory), m_os(os)
{
  BOOST_ASSERT(p_debuggable);
}

void Writer::examine() const
{
  m_memory.check_memory_accesses(false);

  ZCPM::Registers registers{};
  m_pdebuggable->get_registers(registers);
  const auto [op1, op2, op3, op4] = m_pdebuggable->get_opcodes_at(registers.PC, 0);
  auto [_, s1, s2] = disassemble(op1, op2, op3, op4, registers.PC);

  display(registers, s1, s2);

  m_memory.check_memory_accesses(true);
}

void Writer::list(int start, size_t instructions) const
{
  ZCPM::Registers registers{};
  m_pdebuggable->get_registers(registers);
  const uint16_t base = (start < 0) ? registers.PC : start;

  size_t offset = 0;
  for (size_t i = 0; i < instructions; ++i)
  {
    const auto [op1, op2, op3, op4] = m_pdebuggable->get_opcodes_at(base, offset);
    const auto [nbytes, s1, s2] = disassemble(op1, op2, op3, op4, registers.PC);
    display(base + offset, s1, s2);
    offset += nbytes;
  }
}

void Writer::dump(int start, size_t bytes) const
{
  if (bytes == 0)
  {
    return;
  }

  ZCPM::Registers registers{};
  m_pdebuggable->get_registers(registers);
  const uint16_t base = (start < 0) ? registers.PC : start;

  std::string hex_bytes, ascii_bytes;
  for (size_t offset = 0; offset < bytes; ++offset)
  {
    if ((offset % 16) == 0)
    {
      m_os << boost::format("%04X:") % (base + offset);
    }
    const auto b = m_memory.read_byte(base + offset);
    hex_bytes += (boost::format(" %02X") % static_cast<unsigned short>(b)).str();
    ascii_bytes += std::string(1, ((b < 0x20) || (b > 0x7f)) ? '.' : b);
    if (((offset + 1) % 16) == 0)
    {
      m_os << hex_bytes << ' ' << ascii_bytes << std::endl;
      hex_bytes.clear();
      ascii_bytes.clear();
    }
  }
  if (!hex_bytes.empty() || !ascii_bytes.empty())
  {
    const auto nbytes = ascii_bytes.length();
    const auto padding = std::string((16 - nbytes) * 3, ' ');
    m_os << hex_bytes << padding << ' ' << ascii_bytes << std::endl;
  }
}

void Writer::display(uint16_t address, const std::string& s1, const std::string& s2) const
{
  m_os << boost::format("%04X     %-5s%s") % address % s1 % s2 << std::endl;
}

void Writer::display(const ZCPM::Registers& registers, const std::string& s1, const std::string& s2) const
{
  m_os << boost::format("%s A=%02X B=%04X D=%04X H=%04X S=%04X P=%04X  %-5s%s") % flags_to_string(registers.AF & 0xFF) %
            static_cast<unsigned short>(registers.AF >> 8) % registers.BC % registers.DE % registers.HL % registers.SP %
            registers.PC % s1 % s2;

  m_os << std::endl;
  m_os << boost::format("%s '=%02X '=%04X '=%04X '=%04X X=%04X Y=%04X") % flags_to_string(registers.altAF & 0xFF) %
            static_cast<unsigned short>(registers.altAF >> 8) % registers.altBC % registers.altDE % registers.altHL %
            registers.IX % registers.IY
       << std::endl;
}

std::string Writer::flags_to_string(uint8_t f) const
{
  std::string result("------");

  if (f & ZCPM::Processor::C_FLAG_MASK)
  {
    result[0] = 'C';
  }
  if (f & ZCPM::Processor::N_FLAG_MASK)
  {
    result[1] = 'S';
  }
  if (f & ZCPM::Processor::PV_FLAG_MASK)
  {
    result[2] = 'E';
  }
  if (f & ZCPM::Processor::H_FLAG_MASK)
  {
    result[3] = 'F';
  }
  if (f & ZCPM::Processor::Z_FLAG_MASK)
  {
    result[4] = 'Z';
  }
  if (f & ZCPM::Processor::S_FLAG_MASK)
  {
    result[5] = 'M';
  }

  return result;
}
