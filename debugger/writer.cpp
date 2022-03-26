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
    const std::array<const char*, 8> DDByteRegMask{ "B", "C", "D", "E", "IXH", "IXL", "(HL)", "A" };
    const std::array<const char*, 8> FDByteRegMask{ "B", "C", "D", "E", "IYH", "IYL", "(HL)", "A" };
    const std::map<uint8_t, const char*> DdFdCbLogicals = {
        { 0x06, "RLC" }, { 0x0E, "RRC" }, { 0x16, "RL" },  { 0x1E, "RR" },
        { 0x26, "SLA" }, { 0x2E, "SRA" }, { 0x36, "SLL" }, { 0x3E, "SRL" },
    };

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

    std::string byte_array_to_string(const std::vector<uint8_t>& bytes)
    {
        std::string result;
        for (const auto& b : bytes)
        {
            if (!result.empty())
            {
                result += " ";
            }
            result += byte(b);
        }
        return "[" + result + "]";
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
        if ((op2 & 0xC0) == 0x40)
        {
            const uint16_t b = (op2 >> 3) & 0x07;
            const uint8_t r = op2 & 0x07;
            return { 2, "BIT", (boost::format("%d,") % b).str() + ByteRegMask[r] };
        }
        if ((op2 & 0xC0) == 0x80)
        {
            const uint16_t b = (op2 >> 3) & 0x07;
            const uint8_t r = op2 & 0x07;
            return { 2, "RES", (boost::format("%d,") % b).str() + ByteRegMask[r] };
        }
        if ((op2 & 0xC0) == 0xC0)
        {
            const uint16_t b = (op2 >> 3) & 0x07;
            const uint8_t r = op2 & 0x07;
            return { 2, "SET", (boost::format("%d,") % b).str() + ByteRegMask[r] };
        }
        if ((op2 & 0xC7) == 0x46)
        {
            const uint16_t b = (op2 >> 3) & 0x07;
            return { 2, "BIT", (boost::format("%d,(HL)") % b).str() };
        }
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
        if ((op2 & 0xF8) == 0x10)
        {
            const uint8_t r = op2 & 0x07;
            return { 2, "RL", ByteRegMask[r] };
        }
        if ((op2 & 0xF8) == 0x18)
        {
            const uint8_t r = op2 & 0x07;
            return { 2, "RR", ByteRegMask[r] };
        }
        if ((op2 & 0xF8) == 0x20)
        {
            const uint8_t r = op2 & 0x07;
            return { 2, "SLA", ByteRegMask[r] };
        }
        if ((op2 & 0xF8) == 0x28)
        {
            const uint8_t r = op2 & 0x07;
            return { 2, "SRA", ByteRegMask[r] };
        }
        if ((op2 & 0xF8) == 0x30)
        {
            const uint8_t r = op2 & 0x07;
            return { 2, "SLL", ByteRegMask[r] };
        }
        if ((op2 & 0xF8) == 0x38)
        {
            const uint8_t r = op2 & 0x07;
            return { 2, "SRL", ByteRegMask[r] };
        }

        // No match
        return { 0, (boost::format("?? CB %02X") % static_cast<unsigned short>(op2)).str(), "" };
    }

    std::tuple<size_t, std::string, std::string> disassemble_ddfd(const std::string& xy,
                                                                  uint8_t op1,
                                                                  uint8_t op2,
                                                                  uint8_t op3,
                                                                  uint8_t op4)
    {
        // First check for specific opcodes
        switch (op2)
        {
        case 0x09: return { 2, "ADD", xy + ",BC" };
        case 0x19: return { 2, "ADD", xy + ",DE" };
        case 0x21: return { 4, "LD", (boost::format("%S,%04X") % xy % ((op4 << 8) | op3)).str() };
        case 0x22: return { 4, "LD", (boost::format("(%04X),%S") % ((op4 << 8) | op3) % xy).str() };
        case 0x23: return { 2, "INC", xy };
        case 0x24: return { 2, "INC", xy + "H" };
        case 0x25: return { 2, "DEC", xy + "H" };
        case 0x26: return { 3, "LD", (boost::format("%SH,%02X") % xy % static_cast<uint16_t>(op3)).str() };
        case 0x29: return { 2, "ADD", xy + "," + xy };
        case 0x2A: return { 4, "LD", (boost::format("%S,(%04X)") % xy % ((op4 << 8) | op3)).str() };
        case 0x2B: return { 2, "DEC", xy };
        case 0x2C: return { 2, "INC", xy + "L" };
        case 0x2D: return { 2, "DEC", xy + "L" };
        case 0x2E: return { 3, "LD", (boost::format("%SL,%02X") % xy % static_cast<uint16_t>(op3)).str() };
        case 0x34: return { 3, "INC", (boost::format("(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0x35: return { 3, "DEC", (boost::format("(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0x36:
            return {
                4,
                "LD",
                (boost::format("(%S+%02X),%02X") % xy % static_cast<uint16_t>(op3) % static_cast<uint16_t>(op4)).str()
            };
        case 0x39: return { 2, "ADD", xy + ",SP" };
        case 0x86: return { 3, "ADD", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0x8E: return { 3, "ADC", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0x94: return { 2, "SUB", xy + "H" };
        case 0x95: return { 2, "SUB", xy + "L" };
        case 0x96: return { 3, "SUB", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0x9E: return { 3, "SBC", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0xA6: return { 3, "AND", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0xAE: return { 3, "XOR", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0xB6: return { 3, "OR", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0xBE: return { 3, "CP", (boost::format("A,(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
        case 0xCB:
        {
            // TODO: move to a new method of its own? We'll see how well this scales...
            if (DdFdCbLogicals.contains(op4))
            {
                return { 4,
                         DdFdCbLogicals.at(op4),
                         (boost::format("(%S+%02X)") % xy % static_cast<uint16_t>(op3)).str() };
            }
            else if ((op4 & 0xC0) == 0x80)
            {
                const auto b = (op4 >> 3) & 0x07;
                return { 4, "RES", (boost::format("%d,(%S+%02X)") % b % xy % static_cast<uint16_t>(op3)).str() };
            }
            else if ((op4 & 0xC0) == 0x40)
            {
                const auto b = (op4 >> 3) & 0x07;
                return { 4, "BIT", (boost::format("%d,(%S+%02X)") % b % xy % static_cast<uint16_t>(op3)).str() };
            }
            else if ((op4 & 0xC0) == 0xC0)
            {
                const auto b = (op4 >> 3) & 0x07;
                return { 4, "SET", (boost::format("%d,(%S+%02X)") % b % xy % static_cast<uint16_t>(op3)).str() };
            }
            else
            {
                // Unimplemented sequence
                const auto message = boost::format("Unimplemented %02X %02X %02X %02X") % static_cast<uint16_t>(op1) %
                                     static_cast<uint16_t>(op2) % static_cast<uint16_t>(op3) %
                                     static_cast<uint16_t>(op4);
                throw std::logic_error(message.str());
            }
        }
        case 0xE1: return { 2, "POP", xy };
        case 0xE5: return { 2, "PUSH", xy };
        default:
            // Fall through to bytefield checks below
            break;
        }

        // Now check for bytefields
        if ((op2 & 0xC0) == 0x40)
        {
            // Refer 'Undocumented Z80', page 24
            const auto p1 = (op2 >> 3) & 0x07;
            const auto p2 = op2 & 0x07;
            const auto table = (op1 == 0xDD) ? DDByteRegMask : FDByteRegMask;
            if (p1 == 0x06)
            {
                const auto lhs = boost::format("(%S+%02X)") % xy % static_cast<uint16_t>(op3);
                return { 3, "LD", lhs.str() + "," + std::string(ByteRegMask[p2]) };
            }
            else if (p2 == 0x06)
            {
                const auto rhs = boost::format("(%S+%02X)") % xy % static_cast<uint16_t>(op3);
                return { 3, "LD", std::string(ByteRegMask[p1]) + "," + rhs.str() };
            }
            else
            {
                return { 2, "LD", std::string(table[p1]) + "," + std::string(table[p2]) };
            }
        }
        if ((op4 & 0xC0) == 0x40)
        {
            const auto b = (op4 >> 3) & 0x07;
            return { 4, "BIT", (boost::format("%X,(%S+%02X)") % xy % b % static_cast<uint16_t>(op3)).str() };
        }
        if ((op2 & 0xC7) == 0x46)
        {
            const uint8_t r = (op2 >> 3) & 0x07;
            return { 3, "LD", r_ind_offset(r, xy, op3) };
        }
        if ((op2 & 0xF8) == 0x70)
        {
            const uint8_t r = op2 & 0x07;
            return { 3, "LD", ind_offset_r(r, xy, op3) };
        }

        // Unimplemented sequence
        const auto message = boost::format("Unimplemented %02X %02X %02X %02X") % static_cast<uint16_t>(op1) %
                             static_cast<uint16_t>(op2) % static_cast<uint16_t>(op3) % static_cast<uint16_t>(op4);
        throw std::logic_error(message.str());
    }

    std::tuple<size_t, std::string, std::string> disassemble_ed(uint8_t op2, uint8_t op3, uint8_t op4)
    {
        // First check for specific opcodes
        switch (op2)
        {
        case 0x44: return { 2, "NEG", "" };
        case 0x67: return { 2, "RRD", "" };
        case 0x6F: return { 2, "RLD", "" };
        case 0xA0: return { 2, "LDI", "" };
        case 0xA1: return { 2, "CPI", "" };
        case 0xA8: return { 2, "LDD", "" };
        case 0xA9: return { 2, "CPD", "" };
        case 0xB0: return { 2, "LDIR", "" };
        case 0xB1: return { 2, "CPIR", "" };
        case 0xB8: return { 2, "LDDR", "" };
        case 0xB9: return { 2, "CPDR", "" };
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
        case 0x27: return { 1, "DAA", "" };
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
        case 0xDD: return disassemble_ddfd("IX", op1, op2, op3, op4);
        case 0xD6: return { 2, "SUB", byte(op2) };
        case 0xD9: return { 1, "EXX", "" };
        case 0xDE: return { 2, "SBC", "A," + byte(op2) };
        case 0xE3: return { 1, "EX", "(SP),HL" };
        case 0xE6: return { 2, "AND", byte(op2) };
        case 0xEE: return { 2, "XOR", byte(op2) };
        case 0xF3: return { 1, "DI", "" };
        case 0xF6: return { 2, "OR", byte(op2) };
        case 0xFB: return { 1, "EI", "" };
        case 0xE9: return { 1, "JP", "(HL)" };
        case 0xEB: return { 1, "EX", "DE,HL" };
        case 0xED: return disassemble_ed(op2, op3, op4);
        case 0xF9: return { 1, "LD", "SP,HL" };
        case 0xFD: return disassemble_ddfd("IY", op1, op2, op3, op4);
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

Writer::Writer(const zcpm::IDebuggable* p_debuggable, zcpm::IMemory& memory, std::ostream& os)
    : m_pdebuggable(p_debuggable), m_memory(memory), m_os(os)
{
    BOOST_ASSERT(p_debuggable);
}

void Writer::examine() const
{
    m_memory.check_memory_accesses(false);

    const auto registers = m_pdebuggable->get_registers();
    const auto [op1, op2, op3, op4, skipped] = m_pdebuggable->get_opcodes_at(registers.PC, 0);
    if (!skipped.empty())
    {
        display(registers, byte_array_to_string(skipped) + " SKIPPED", "");
    }
    const auto num_skipped = skipped.size();

    try
    {
        const auto [_, s1, s2] = disassemble(op1, op2, op3, op4, registers.PC + num_skipped);

        display(registers, s1, s2, num_skipped);

        m_memory.check_memory_accesses(true);
    }
    catch (const std::logic_error& e)
    {
        // A failure to parse; display enough information to help the maintainer:
        // Memory content around the offending area, aligned on 16's
        dump((registers.PC - 16) & 0xFFF0, 64);
        throw;
    }
}

void Writer::list(int start, size_t instructions) const
{
    const auto registers = m_pdebuggable->get_registers();
    const uint16_t base = (start < 0) ? registers.PC : start;

    size_t offset = 0;
    for (size_t i = 0; i < instructions; ++i)
    {
        const auto [op1, op2, op3, op4, skipped] = m_pdebuggable->get_opcodes_at(base, offset);
        if (!skipped.empty())
        {
            display(registers, byte_array_to_string(skipped) + " SKIPPED", "");
            offset += skipped.size();
        }
        const auto num_skipped = skipped.size();
        const auto [nbytes, s1, s2] = disassemble(op1, op2, op3, op4, registers.PC + num_skipped);
        display(base + offset + num_skipped, s1, s2);
        offset += nbytes + num_skipped;
    }
}

void Writer::dump(int start, size_t bytes) const
{
    if (bytes == 0)
    {
        return;
    }

    const auto registers = m_pdebuggable->get_registers();
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

void Writer::display(const zcpm::Registers& registers,
                     const std::string& s1,
                     const std::string& s2,
                     const uint16_t offset) const
{
    m_os << boost::format("%s A=%02X B=%04X D=%04X H=%04X S=%04X P=%04X  %-5s%s") %
                flags_to_string(registers.AF & 0xFF) % static_cast<unsigned short>(registers.AF >> 8) % registers.BC %
                registers.DE % registers.HL % registers.SP % ((registers.PC + offset) & 0xFFFF) % s1 % s2;

    m_os << std::endl;
    m_os << boost::format("%s '=%02X '=%04X '=%04X '=%04X X=%04X Y=%04X") % flags_to_string(registers.altAF & 0xFF) %
                static_cast<unsigned short>(registers.altAF >> 8) % registers.altBC % registers.altDE %
                registers.altHL % registers.IX % registers.IY
         << std::endl;
}

std::string Writer::flags_to_string(uint8_t f) const
{
    std::string result("------");

    if (f & zcpm::Processor::C_FLAG_MASK)
    {
        result[0] = 'C';
    }
    if (f & zcpm::Processor::N_FLAG_MASK)
    {
        result[1] = 'S';
    }
    if (f & zcpm::Processor::PV_FLAG_MASK)
    {
        result[2] = 'E';
    }
    if (f & zcpm::Processor::H_FLAG_MASK)
    {
        result[3] = 'F';
    }
    if (f & zcpm::Processor::Z_FLAG_MASK)
    {
        result[4] = 'Z';
    }
    if (f & zcpm::Processor::S_FLAG_MASK)
    {
        result[5] = 'M';
    }

    return result;
}
