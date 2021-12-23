#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "instructions.hpp"
#include "processor.hpp"
#include "processordata.hpp"
#include "registers.hpp"

// Uncomment this to allow very chatting logging of calls/returns
//#define TRACING

namespace
{
    const uint8_t OPCODE_LD_A_I = 0x57;
    const uint8_t OPCODE_LD_I_A = 0x47;
    const uint8_t OPCODE_LDI = 0xa0;
    const uint8_t OPCODE_LDIR = 0xb0;
    const uint8_t OPCODE_CPI = 0xa1;
    const uint8_t OPCODE_CPIR = 0xb1;
    const uint8_t OPCODE_RLD = 0x6f;
#if defined(Z80_CATCH_RETI) && defined(Z80_CATCH_RETN)
    const uint8_t OPCODE_RETI = 0x4d;
#endif
    const uint8_t OPCODE_INI = 0xa2;
    const uint8_t OPCODE_INIR = 0xb2;
    const uint8_t OPCODE_OUTI = 0xa3;
    const uint8_t OPCODE_OTIR = 0xb3;

    // Opcode decoding helpers.
    // Y() is bits 5-3 of the opcode, Z() is bits 2-0, P() bits 5-4, and Q() bits 4-3.
    uint8_t Y(uint8_t opcode)
    {
        return (opcode >> 3) & 0x07;
    }
    uint8_t Z(uint8_t opcode)
    {
        return opcode & 0x07;
    }
    uint8_t P(uint8_t opcode)
    {
        return (opcode >> 4) & 0x03;
    }
    uint8_t Q(uint8_t opcode)
    {
        return (opcode >> 3) & 0x03;
    }

    // Additional bitmasks for convenience
    const uint8_t SZC_FLAG_MASK =
        zcpm::Processor::S_FLAG_MASK | zcpm::Processor::Z_FLAG_MASK | zcpm::Processor::C_FLAG_MASK;
    const uint8_t YX_FLAG_MASK = zcpm::Processor::Y_FLAG_MASK | zcpm::Processor::X_FLAG_MASK;
    const uint8_t SZ_FLAG_MASK = zcpm::Processor::S_FLAG_MASK | zcpm::Processor::Z_FLAG_MASK;
    const uint8_t SZPV_FLAG_MASK =
        zcpm::Processor::S_FLAG_MASK | zcpm::Processor::Z_FLAG_MASK | zcpm::Processor::PV_FLAG_MASK;
    const uint8_t SYX_FLAG_MASK =
        zcpm::Processor::S_FLAG_MASK | zcpm::Processor::Y_FLAG_MASK | zcpm::Processor::X_FLAG_MASK;
    const uint8_t HC_FLAG_MASK = zcpm::Processor::H_FLAG_MASK | zcpm::Processor::C_FLAG_MASK;

    /* Indirect (HL) or prefixed indexed (IX + d) and (IY + d) memory operands are
     * encoded using the 3 bits "110" (0x06).
     */
    const uint8_t INDIRECT_HL = 0x06;

    /* Condition codes are encoded using 2 or 3 bits.  The xor table is needed for
     * negated conditions, it is used along with the and table.
     */

    const std::array<uint8_t, 8> XOR_CONDITION_TABLE{
        zcpm::Processor::Z_FLAG_MASK,  0, zcpm::Processor::C_FLAG_MASK, 0,
        zcpm::Processor::PV_FLAG_MASK, 0, zcpm::Processor::S_FLAG_MASK, 0,
    };

    const std::array<uint8_t, 8> AND_CONDITION_TABLE{
        zcpm::Processor::Z_FLAG_MASK, zcpm::Processor::Z_FLAG_MASK,  zcpm::Processor::C_FLAG_MASK,
        zcpm::Processor::C_FLAG_MASK, zcpm::Processor::PV_FLAG_MASK, zcpm::Processor::PV_FLAG_MASK,
        zcpm::Processor::S_FLAG_MASK, zcpm::Processor::S_FLAG_MASK,
    };

    /* RST instruction restart addresses, encoded by Y() bits of the opcode. */

    const std::array<uint8_t, 8> RST_TABLE{
        0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
    };

    /* There is an overflow if the xor of the carry out and the carry of the most
     * significant bit is not zero.
     */

    const std::array<uint8_t, 4> OVERFLOW_TABLE{
        0,
        zcpm::Processor::PV_FLAG_MASK,
        zcpm::Processor::PV_FLAG_MASK,
        0,
    };

    // Register definitions. These are used as indexes into an array; the first enum Reg8 indexes into one view of a
    // union to that array, the second enum Reg16 indexes into another view of that union into that array. For that
    // reason it's not an 'enum class'.

    // clang-format off
    enum Reg8
    {
        C, B, E, D, L, H, F, A,
        IXL, IXH,
        IYL, IYH,
        // No 8-bit access to SP
    };

    enum Reg16
    {
        BC, DE, HL, AF,
        IX, IY,
        SP
    };

    const std::set<uint8_t> DdFdPrefixable = {
        0x09,
        0x19,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26,             0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e,
        0x34, 0x35, 0x36,             0x39,
        0x44, 0x45, 0x46,                               0x4c, 0x4d, 0x4e,
        0x54, 0x55, 0x56,                               0x5c, 0x5d, 0x5e,
        0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75,       0x77,                         0x7c, 0x7d, 0x7e,
        0x84, 0x85, 0x86,                               0x8c, 0x8d, 0x8e,
        0x94, 0x95, 0x96,                               0x9c, 0x9d, 0x9e,
        0xa4, 0xa5, 0xa6,                               0xac, 0xad, 0xae,
        0xb4, 0xb5, 0xb6,                               0xbc, 0xbd, 0xbe,
        0xcb,
        0xe1,       0xe3,       0xe5,                   0xe9,
        0xf9
    };

    // clang-format on

} // namespace

namespace zcpm
{

    Processor::Processor(IMemory& memory, IProcessorObserver& processor_observer)
        : m_current_register_table(m_register_table), m_memory(memory), m_processor_observer(processor_observer)
    {
        ::memset(m_registers.byte, 0, sizeof(m_registers));
        ::memset(m_alternates, 0, sizeof(m_alternates));

        reset_state();

        /* Build register decoding tables for both 3-bit encoded 8-bit registers and 2-bit
         * encoded 16-bit registers. When an opcode is prefixed by 0xdd, HL is replaced by
         * IX. When 0xfd prefixed, HL is replaced by IY.
         */

        // 8-bit "R" registers

        m_register_table[0] = &m_registers.byte[Reg8::B];
        m_register_table[1] = &m_registers.byte[Reg8::C];
        m_register_table[2] = &m_registers.byte[Reg8::D];
        m_register_table[3] = &m_registers.byte[Reg8::E];
        m_register_table[4] = &m_registers.byte[Reg8::H];
        m_register_table[5] = &m_registers.byte[Reg8::L];

        // Encoding 0x06 is used for indexed memory operands and direct HL or IX/IY register access

        m_register_table[6] = &m_registers.word[Reg16::HL];
        m_register_table[7] = &m_registers.byte[Reg8::A];

        // "Regular" 16-bit "RR" registers

        m_register_table[8] = &m_registers.word[Reg16::BC];
        m_register_table[9] = &m_registers.word[Reg16::DE];
        m_register_table[10] = &m_registers.word[Reg16::HL];
        m_register_table[11] = &m_registers.word[Reg16::SP];

        // 16-bit "SS" registers for PUSH and POP instructions (note that SP is replaced by AF)

        m_register_table[12] = &m_registers.word[Reg16::BC];
        m_register_table[13] = &m_registers.word[Reg16::DE];
        m_register_table[14] = &m_registers.word[Reg16::HL];
        m_register_table[15] = &m_registers.word[Reg16::AF];

        // 0xdd and 0xfd prefixed register decoding tables

        for (int i = 0; i < 16; i++)
        {
            m_dd_register_table[i] = m_fd_register_table[i] = m_register_table[i];
        }

        m_dd_register_table[4] = &m_registers.byte[Reg8::IXH];
        m_dd_register_table[5] = &m_registers.byte[Reg8::IXL];
        m_dd_register_table[6] = &m_registers.word[Reg16::IX];
        m_dd_register_table[10] = &m_registers.word[Reg16::IX];
        m_dd_register_table[14] = &m_registers.word[Reg16::IX];

        m_fd_register_table[4] = &m_registers.byte[Reg8::IYH];
        m_fd_register_table[5] = &m_registers.byte[Reg8::IYL];
        m_fd_register_table[6] = &m_registers.word[Reg16::IY];
        m_fd_register_table[10] = &m_registers.word[Reg16::IY];
        m_fd_register_table[14] = &m_registers.word[Reg16::IY];
    }

    void Processor::reset_state()
    {
        reg_af() = 0xffff;
        reg_sp() = 0xffff;
        m_i = m_pc = m_iff1 = m_iff2 = m_effective_pc = 0;
        m_im = InterruptMode::IM0;
    }

    size_t Processor::interrupt(uint8_t data_on_bus)
    {
        if (m_iff1)
        {
            m_iff1 = m_iff2 = 0;
            m_r = (m_r & 0x80) | ((m_r + 1) & 0x7f);

            switch (m_im)
            {

            case InterruptMode::IM0:
            {
                /* Assuming the opcode in data_on_bus is an RST instruction, accepting the interrupt
                 * should take 2 + 11 = 13 cycles.
                 */
                return emulate(data_on_bus, false, 2, 4);
            }

            case InterruptMode::IM1:
            {
                size_t elapsed_cycles = 0;
                reg_sp() -= 2;
                m_memory.write_word(reg_sp(), m_pc);
                m_pc = 0x0038;
                return elapsed_cycles + 13;
            }

            case InterruptMode::IM2:
            {
                size_t elapsed_cycles = 0;
                reg_sp() -= 2;
                m_memory.write_word(reg_sp(), m_pc);
                const uint16_t vector = m_i << 8 | data_on_bus;
                m_pc = m_memory.read_word(vector);
                return elapsed_cycles + 19;
            }
            }
        }
        else
        {
            return 0;
        }

        return 0; // Should be unreachable, but needed to keep gcc happy
    }

    size_t Processor::non_maskable_interrupt()
    {
        m_iff2 = m_iff1;
        m_iff1 = 0;
        m_r = (m_r & 0x80) | ((m_r + 1) & 0x7f);

        size_t elapsed_cycles = 0;
        reg_sp() -= 2;
        m_memory.write_word(reg_sp(), m_pc);
        m_pc = 0x0066;

        return elapsed_cycles + 11;
    }

    size_t Processor::emulate()
    {
        m_effective_pc = m_pc;
        uint8_t opcode = m_memory.read_byte(m_pc++);

        return emulate(opcode, true, 0, 0);
    }

    size_t Processor::emulate_instruction()
    {
        m_effective_pc = m_pc;
        uint8_t opcode = m_memory.read_byte(m_pc++);

        return emulate(opcode, false, 0, 0);
    }

    uint8_t Processor::get_a() const
    {
        return m_registers.byte[Reg8::A];
    }

    uint8_t Processor::get_f() const
    {
        return m_registers.byte[Reg8::F];
    }

    uint8_t Processor::get_b() const
    {
        return m_registers.byte[Reg8::B];
    }

    uint8_t Processor::get_c() const
    {
        return m_registers.byte[Reg8::C];
    }

    uint8_t Processor::get_d() const
    {
        return m_registers.byte[Reg8::D];
    }

    uint8_t Processor::get_e() const
    {
        return m_registers.byte[Reg8::E];
    }

    uint8_t Processor::get_h() const
    {
        return m_registers.byte[Reg8::H];
    }

    uint8_t Processor::get_l() const
    {
        return m_registers.byte[Reg8::L];
    }

    uint16_t Processor::get_af() const
    {
        return m_registers.word[Reg16::AF];
    }

    uint16_t Processor::get_bc() const
    {
        return m_registers.word[Reg16::BC];
    }

    uint16_t Processor::get_de() const
    {
        return m_registers.word[Reg16::DE];
    }

    uint16_t Processor::get_hl() const
    {
        return m_registers.word[Reg16::HL];
    }

    uint16_t Processor::get_sp() const
    {
        return m_registers.word[Reg16::SP];
    }

    uint16_t Processor::get_pc() const
    {
        return m_effective_pc; // Note that we return *effective* PC, not m_pc
    }

    uint8_t& Processor::reg_a()
    {
        return m_registers.byte[Reg8::A];
    }

    uint8_t& Processor::reg_f()
    {
        return m_registers.byte[Reg8::F];
    }

    uint8_t& Processor::reg_b()
    {
        return m_registers.byte[Reg8::B];
    }

    uint8_t& Processor::reg_c()
    {
        return m_registers.byte[Reg8::C];
    }

    uint8_t& Processor::reg_d()
    {
        return m_registers.byte[Reg8::D];
    }

    uint8_t& Processor::reg_e()
    {
        return m_registers.byte[Reg8::E];
    }

    uint8_t& Processor::reg_h()
    {
        return m_registers.byte[Reg8::H];
    }

    uint8_t& Processor::reg_l()
    {
        return m_registers.byte[Reg8::L];
    }

    uint16_t& Processor::reg_af()
    {
        return m_registers.word[Reg16::AF];
    }

    uint16_t& Processor::reg_bc()
    {
        return m_registers.word[Reg16::BC];
    }

    uint16_t& Processor::reg_de()
    {
        return m_registers.word[Reg16::DE];
    }

    uint16_t& Processor::reg_hl()
    {
        return m_registers.word[Reg16::HL];
    }

    uint16_t& Processor::reg_sp()
    {
        return m_registers.word[Reg16::SP];
    }

    uint16_t& Processor::reg_pc()
    {
        return m_pc;
    }

    Registers Processor::get_registers() const
    {
        Registers r{};

        r.AF = get_af();
        r.BC = get_bc();
        r.DE = get_de();
        r.HL = get_hl();
        r.IX = m_registers.word[Reg16::IX];
        r.IY = m_registers.word[Reg16::IY];
        r.SP = m_registers.word[Reg16::SP];
        r.PC = m_pc;

        r.altAF = m_alternates[Reg16::AF];
        r.altBC = m_alternates[Reg16::BC];
        r.altDE = m_alternates[Reg16::DE];
        r.altHL = m_alternates[Reg16::HL];

        return r;
    }

    std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, std::vector<uint8_t>> Processor::get_opcodes_at(
        uint16_t pc,
        uint16_t offset) const
    {
        // Find the first non-prefix byte from the requested position (ie, byte other than DD/FD)
        std::vector<uint8_t> skipped;
        uint16_t skip_count = 0;
        uint8_t non_prefix_byte = 0;
        while (pc + offset + skip_count <= 0xFFFF)
        {
            const auto b = m_memory.read_byte(pc + offset + skip_count);
            if ((b == 0xDD) || (b == 0xFD))
            {
                ++skip_count;
            }
            else
            {
                non_prefix_byte = b;
                break;
            }
        }

        // Was there any DD/FD bytes at the start of the sequence?
        if (skip_count)
        {
            // This means that pc+offset points at one or more DD/FD bytes
            if (DdFdPrefixable.contains(non_prefix_byte))
            {
                // The use of DD/FD is valid, nothing to be ignored
                skip_count = 0;
            }
            else
            {
                // The DD/FD sequence is invalid (they precede an opcode that doesn't "need" a prefix).
                // Let the caller know about this DD/FD sequence.
                for (auto i = 0; i < skip_count; i++)
                {
                    skipped.push_back(m_memory.read_byte(pc + offset + i));
                }
            }
        }

        const auto op1 = m_memory.read_byte(pc + offset + skip_count + 0);
        const auto op2 = m_memory.read_byte(pc + offset + skip_count + 1);
        const auto op3 = m_memory.read_byte(pc + offset + skip_count + 2);
        const auto op4 = m_memory.read_byte(pc + offset + skip_count + 3);
        return { op1, op2, op3, op4, skipped };
    }

    void Processor::add_action(std::unique_ptr<DebugAction> p_action)
    {
        const auto a = p_action->get_address();
        m_debug_actions.insert({ a, std::move(p_action) });
    }

    void Processor::show_actions(std::ostream& os) const
    {
        os << m_debug_actions.size() << " action(s) are defined." << std::endl;
        int count = 0;
        for (const auto& a : m_debug_actions)
        {
            os << ++count << ": " << *(a.second) << std::endl;
        }
    }

    bool Processor::remove_action(size_t index)
    {
        if ((index == 0) || (index > m_debug_actions.size()))
        {
            return false;
        }

        auto it = m_debug_actions.begin();
        for (size_t i = 1; i < index; i++)
        {
            ++it;
        }
        m_debug_actions.erase(it);

        return true;
    }

    bool Processor::is_default_table() const
    {
        return m_current_register_table == m_register_table;
    }

    void Processor::set_default_table()
    {
        m_current_register_table = m_register_table;
    }

    void Processor::set_dd()
    {
        m_current_register_table = m_dd_register_table;
    }

    void Processor::set_fd()
    {
        m_current_register_table = m_fd_register_table;
    }

    uint8_t& Processor::R(int r) const
    {
        return *(static_cast<uint8_t*>(m_current_register_table[r]));
    }

    uint8_t& Processor::S(int s) const
    {
        return *(static_cast<uint8_t*>(m_register_table[s]));
    }

    uint16_t& Processor::RR(int rr) const
    {
        return *(static_cast<uint16_t*>(m_current_register_table[rr + 8]));
    }

    uint16_t& Processor::SS(int ss) const
    {
        return *(static_cast<uint16_t*>(m_current_register_table[ss + 12]));
    }

    uint16_t& Processor::HL_IX_IY() const
    {
        return *(static_cast<uint16_t*>(m_current_register_table[6]));
    }

    size_t Processor::emulate(uint8_t opcode, bool unbounded, size_t elapsed_cycles, size_t max_cycles)
    {
        uint16_t pc = m_pc;
        uint8_t r = m_r & 0x7f;

        goto start_emulation; // NOLINT: imported 3rd-party code

        for (;;)
        {
            uint8_t instruction;

            m_effective_pc = pc; // In case the following method causes a debugger hook to fire
            opcode = m_memory.read_byte(pc);
            pc++;

        start_emulation:

            set_default_table();

        emulate_next_opcode:

            instruction = INSTRUCTION_TABLE[opcode];

        emulate_next_instruction:

            // CP/M programs terminate by reaching address 0008, e.g. via a RET or a RST0.  We
            // manually treat this as a termination condition.
            if ((m_effective_pc == 0x0008) || !(m_processor_observer.running()))
            {
                BOOST_LOG_TRIVIAL(trace) << boost::format("Stopping execution at PC=%04X") % m_effective_pc;
                m_processor_observer.set_finished(true);
                goto stop_emulation; // NOLINT: imported 3rd-party code
            }

            // Have we hit a BIOS address that needs to be intercepted?
            m_processor_observer.check_and_handle_bdos_and_bios(m_effective_pc);

            elapsed_cycles += 4;
            r++;
            switch (instruction)
            {

                // 8-bit load group

            case LD_R_R:
            {
                R(Y(opcode)) = R(Z(opcode));

                break;
            }

            case LD_R_N:
            {
                R(Y(opcode)) = m_memory.read_byte_step(pc, elapsed_cycles);

                break;
            }

            case LD_R_INDIRECT_HL:
            {
                if (is_default_table())
                {
                    R(Y(opcode)) = m_memory.read_byte(reg_hl(), elapsed_cycles);
                }
                else
                {
                    auto d = static_cast<int>(m_memory.read_byte_step(pc, elapsed_cycles));
                    d += HL_IX_IY();
                    S(Y(opcode)) = m_memory.read_byte(d, elapsed_cycles);

                    elapsed_cycles += 5;
                }

                break;
            }

            case LD_INDIRECT_HL_R:
            {
                if (is_default_table())
                {
                    m_memory.write_byte(reg_hl(), R(Z(opcode)), elapsed_cycles);
                }
                else
                {
                    auto d = static_cast<int>(m_memory.read_byte_step(pc, elapsed_cycles));
                    d += HL_IX_IY();
                    m_memory.write_byte(d, S(Z(opcode)), elapsed_cycles);

                    elapsed_cycles += 5;
                }

                break;
            }

            case LD_INDIRECT_HL_N:
            {
                uint8_t n;

                if (is_default_table())
                {
                    n = m_memory.read_byte_step(pc, elapsed_cycles);
                    m_memory.write_byte(reg_hl(), n, elapsed_cycles);
                }
                else
                {
                    auto d = static_cast<int>(m_memory.read_byte_step(pc, elapsed_cycles));
                    d += HL_IX_IY();
                    n = m_memory.read_byte_step(pc, elapsed_cycles);
                    m_memory.write_byte(d, n, elapsed_cycles);

                    elapsed_cycles += 2;
                }

                break;
            }

            case LD_A_INDIRECT_BC:
            {
                reg_a() = m_memory.read_byte(reg_bc(), elapsed_cycles);

                break;
            }

            case LD_A_INDIRECT_DE:
            {
                reg_a() = m_memory.read_byte(reg_de(), elapsed_cycles);

                break;
            }

            case LD_A_INDIRECT_NN:
            {
                const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                reg_a() = m_memory.read_byte(nn, elapsed_cycles);

                break;
            }

            case LD_INDIRECT_BC_A:
            {
                m_memory.write_byte(reg_bc(), reg_a(), elapsed_cycles);

                break;
            }

            case LD_INDIRECT_DE_A:
            {
                m_memory.write_byte(reg_de(), reg_a(), elapsed_cycles);

                break;
            }

            case LD_INDIRECT_NN_A:
            {
                const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                m_memory.write_byte(nn, reg_a(), elapsed_cycles);

                break;
            }

            case LD_A_I_LD_A_R:
            {
                const uint8_t a = opcode == OPCODE_LD_A_I ? m_i : (r & 0x80) | (r & 0x7f);
                uint8_t f = SZYX_FLAGS_TABLE[a];

                /* Note: On a real processor, if an interrupt occurs during the execution of either
                 * "LD A, I" or "LD A, R", the parity flag is reset. That can never happen here.
                 */

                f |= m_iff2 << PV_FLAG_BIT;
                f |= reg_f() & C_FLAG_MASK;

                reg_af() = (a << 8) | f;

                elapsed_cycles++;

                break;
            }

            case LD_I_A_LD_R_A:
            {
                if (opcode == OPCODE_LD_I_A)
                {
                    m_i = reg_a();
                }
                else
                {
                    r = reg_a() & 0x7f;
                }

                elapsed_cycles++;

                break;
            }

                // 16-bit load group

            case LD_RR_NN:
            {
                RR(P(opcode)) = m_memory.read_word_step(pc, elapsed_cycles);

                break;
            }

            case LD_HL_INDIRECT_NN:
            {
                const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                HL_IX_IY() = m_memory.read_word(nn, elapsed_cycles);

                break;
            }

            case LD_RR_INDIRECT_NN:
            {
                const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                RR(P(opcode)) = m_memory.read_word(nn, elapsed_cycles);

                break;
            }

            case LD_INDIRECT_NN_HL:
            {
                const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                m_memory.write_word(nn, HL_IX_IY(), elapsed_cycles);

                break;
            }

            case LD_INDIRECT_NN_RR:
            {
                const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                m_memory.write_word(nn, RR(P(opcode)), elapsed_cycles);

                break;
            }

            case LD_SP_HL:
            {
                reg_sp() = HL_IX_IY();
                elapsed_cycles += 2;

                break;
            }

            case PUSH_SS:
            {
                m_memory.push(SS(P(opcode)), elapsed_cycles);
                elapsed_cycles++;

                break;
            }

            case POP_SS:
            {
                SS(P(opcode)) = m_memory.pop(elapsed_cycles);

                break;
            }

                // Exchange, block transfer and search group

            case EX_DE_HL:
            {
                std::swap(reg_de(), reg_hl());

                break;
            }

            case EX_AF_AF_PRIME:
            {
                std::swap(reg_af(), m_alternates[Reg16::AF]);

                break;
            }

            case EXX:
            {
                std::swap(reg_bc(), m_alternates[Reg16::BC]);
                std::swap(reg_de(), m_alternates[Reg16::DE]);
                std::swap(reg_hl(), m_alternates[Reg16::HL]);

                break;
            }

            case EX_INDIRECT_SP_HL:
            {
                const uint16_t t = m_memory.read_word(reg_sp(), elapsed_cycles);
                m_memory.write_word(reg_sp(), HL_IX_IY(), elapsed_cycles);
                HL_IX_IY() = t;

                elapsed_cycles += 3;

                break;
            }

            case LDI_LDD:
            {
                uint8_t n = m_memory.read_byte(reg_hl(), elapsed_cycles);
                m_memory.write_byte(reg_de(), n, elapsed_cycles);

                uint8_t f = reg_f() & SZC_FLAG_MASK;
                f |= --reg_bc() ? PV_FLAG_MASK : 0;

                n += reg_a();
                f |= n & X_FLAG_MASK;
                f |= (n << (Y_FLAG_BIT - 1)) & Y_FLAG_MASK;

                reg_f() = f;

                const int d = opcode == OPCODE_LDI ? +1 : -1;
                reg_de() += d;
                reg_hl() += d;

                elapsed_cycles += 2;

                break;
            }

            case LDIR_LDDR:
            {
                const int d = opcode == OPCODE_LDIR ? +1 : -1;

                uint8_t f = reg_f() & SZC_FLAG_MASK;
                uint16_t bc = reg_bc();
                uint16_t de = reg_de();
                uint16_t hl = reg_hl();

                uint8_t n;

                r -= 2;
                elapsed_cycles -= 8;
                for (;;)
                {
                    r += 2;

                    n = m_memory.read_byte(hl);
                    m_memory.write_byte(de, n);

                    hl += d;
                    de += d;

                    if (--bc)
                    {
                        elapsed_cycles += 21;
                    }
                    else
                    {
                        elapsed_cycles += 16;
                        break;
                    }

                    if (unbounded || (elapsed_cycles < max_cycles) || (max_cycles == 0))
                    {
                        continue;
                    }
                    else
                    {
                        f |= PV_FLAG_MASK;
                        pc -= 2;
                        break;
                    }
                }

                reg_hl() = hl;
                reg_de() = de;
                reg_bc() = bc;

                n += reg_a();
                f |= n & X_FLAG_MASK;
                f |= (n << (Y_FLAG_BIT - 1)) & Y_FLAG_MASK;

                reg_f() = f;

                break;
            }

            case CPI_CPD:
            {
                uint8_t a = reg_a();
                uint8_t n = m_memory.read_byte(reg_hl(), elapsed_cycles);
                uint8_t z = a - n;

                reg_hl() += opcode == OPCODE_CPI ? +1 : -1;

                uint8_t f = (a ^ n ^ z) & H_FLAG_MASK;

                n = z - (f >> H_FLAG_BIT);
                f |= (n << (Y_FLAG_BIT - 1)) & Y_FLAG_MASK;
                f |= n & X_FLAG_MASK;

                f |= SZYX_FLAGS_TABLE[z & 0xff] & SZ_FLAG_MASK;
                f |= --reg_bc() ? PV_FLAG_MASK : 0;
                reg_f() = f | N_FLAG_MASK | (reg_f() & C_FLAG_MASK);

                elapsed_cycles += 5;

                break;
            }

            case CPIR_CPDR:
            {
                const int d = opcode == OPCODE_CPIR ? +1 : -1;

                uint8_t a = reg_a();
                uint16_t bc = reg_bc();
                uint16_t hl = reg_hl();

                uint8_t n, z;

                r -= 2;
                elapsed_cycles -= 8;
                for (;;)
                {
                    r += 2;

                    n = m_memory.read_byte(hl);
                    z = a - n;

                    hl += d;
                    if (--bc && z)
                    {
                        elapsed_cycles += 21;
                    }
                    else
                    {
                        elapsed_cycles += 16;
                        break;
                    }

                    if (unbounded || (elapsed_cycles < max_cycles) || (max_cycles == 0))
                    {
                        continue;
                    }
                    else
                    {
                        pc -= 2;
                        break;
                    }
                }

                reg_hl() = hl;
                reg_bc() = bc;

                uint8_t f = (a ^ n ^ z) & H_FLAG_MASK;

                n = z - (f >> H_FLAG_BIT);
                f |= (n << (Y_FLAG_BIT - 1)) & Y_FLAG_MASK;
                f |= n & X_FLAG_MASK;

                f |= SZYX_FLAGS_TABLE[z & 0xff] & SZ_FLAG_MASK;
                f |= bc ? PV_FLAG_MASK : 0;
                reg_f() = f | N_FLAG_MASK | (reg_f() & C_FLAG_MASK);

                break;
            }

                // 8-bit arithmetic and logical group

            case ADD_R:
            {
                op_add(R(Z(opcode)));

                break;
            }

            case ADD_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_add(n);

                break;
            }

            case ADD_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);
                op_add(x);

                break;
            }

            case ADC_R:
            {
                op_adc(R(Z(opcode)));

                break;
            }

            case ADC_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_adc(n);

                break;
            }

            case ADC_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);

                op_adc(x);
                break;
            }

            case SUB_R:
            {
                op_sub(R(Z(opcode)));

                break;
            }

            case SUB_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_sub(n);

                break;
            }

            case SUB_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);
                op_sub(x);

                break;
            }

            case SBC_R:
            {
                op_sbc(R(Z(opcode)));

                break;
            }

            case SBC_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_sbc(n);

                break;
            }

            case SBC_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);
                op_sbc(x);

                break;
            }

            case AND_R:
            {
                op_and(R(Z(opcode)));

                break;
            }

            case AND_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_and(n);

                break;
            }

            case AND_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);
                op_and(x);

                break;
            }

            case OR_R:
            {
                op_or(R(Z(opcode)));

                break;
            }

            case OR_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_or(n);
                break;
            }

            case OR_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);
                op_or(x);

                break;
            }

            case XOR_R:
            {
                op_xor(R(Z(opcode)));

                break;
            }

            case XOR_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_xor(n);

                break;
            }

            case XOR_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);
                op_xor(x);

                break;
            }

            case CP_R:
            {
                op_cp(R(Z(opcode)));

                break;
            }

            case CP_N:
            {
                auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                op_cp(n);

                break;
            }

            case CP_INDIRECT_HL:
            {
                uint8_t x = read_indirect_hl(pc, elapsed_cycles);
                op_cp(x);

                break;
            }

            case INC_R:
            {
                const auto y = Y(opcode);
                auto x = R(y);
                op_inc(x);
                R(y) = x;

                break;
            }

            case INC_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_inc(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    auto d = static_cast<int>(m_memory.read_byte_step(pc, elapsed_cycles));
                    d += HL_IX_IY();
                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_inc(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    elapsed_cycles += 6;
                }

                break;
            }

            case DEC_R:
            {
                const auto y = Y(opcode);
                auto x = R(y);
                op_dec(x);
                R(y) = x;

                break;
            }

            case DEC_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_dec(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    auto d = static_cast<int>(m_memory.read_byte_step(pc, elapsed_cycles));
                    d += HL_IX_IY();
                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_dec(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    elapsed_cycles += 6;
                }

                break;
            }

                // General-purpose arithmetic and CPU control group

            case DAA:
            {
                // The following algorithm is from comp.sys.sinclair's FAQ.

                const uint8_t a = reg_a();
                uint8_t c = 0;
                uint8_t d = 0;
                if (a > 0x99 || (reg_f() & C_FLAG_MASK))
                {
                    c = C_FLAG_MASK;
                    d = 0x60;
                }
                if ((a & 0x0f) > 0x09 || (reg_f() & H_FLAG_MASK))
                {
                    d += 0x06;
                }
                reg_a() += reg_f() & N_FLAG_MASK ? -d : +d;
                reg_f() = SZYXP_FLAGS_TABLE[reg_a()] | ((reg_a() ^ a) & H_FLAG_MASK) | (reg_f() & N_FLAG_MASK) | c;

                break;
            }

            case CPL:
            {
                reg_a() = ~reg_a();
                reg_f() =
                    (reg_f() & (SZPV_FLAG_MASK | C_FLAG_MASK)) | (reg_a() & YX_FLAG_MASK) | H_FLAG_MASK | N_FLAG_MASK;

                break;
            }

            case NEG:
            {
                const uint8_t a = reg_a();
                int z = -a;

                int c = a ^ z;
                uint8_t f = N_FLAG_MASK | (c & H_FLAG_MASK);
                f |= SZYX_FLAGS_TABLE[z &= 0xff];
                c &= 0x0180;
                f |= OVERFLOW_TABLE[c >> 7];
                f |= c >> (8 - C_FLAG_BIT);

                reg_a() = z;
                reg_f() = f;

                break;
            }

            case CCF:
            {
                const uint8_t c = reg_f() & C_FLAG_MASK;
                reg_f() = (reg_f() & (SZPV_FLAG_MASK | YX_FLAG_MASK)) | (c << H_FLAG_BIT) | (reg_a() & YX_FLAG_MASK) |
                          (c ^ C_FLAG_MASK);

                break;
            }

            case SCF:
            {
                reg_f() = (reg_f() & (SZPV_FLAG_MASK | YX_FLAG_MASK)) | (reg_a() & YX_FLAG_MASK) | C_FLAG_MASK;

                break;
            }

            case NOP:
            {
                break;
            }

            case HALT:
            {
                /* If an HALT instruction is executed, the Z80 keeps executing NOPs until an interrupt
                 * is generated. Basically nothing happens for the remaining number of cycles.
                 */

                if (elapsed_cycles < max_cycles)
                {
                    elapsed_cycles = max_cycles;
                }

                goto stop_emulation; // NOLINT: imported 3rd-party code
            }

            case DI:
            {
                m_iff1 = m_iff2 = 0;

                /* No interrupt can be accepted right after a DI or EI instruction on an actual Z80
                 * processor. By adding 4 cycles to max_cycles, at least one more instruction
                 * will be executed. However, this will fail if the next instruction has multiple
                 * 0xdd or 0xfd prefixes and Z80_PREFIX_FAILSAFE is defined, but that is an
                 * unlikely pathological case.
                 */

                max_cycles += 4;
                break;
            }

            case EI:
            {
                m_iff1 = m_iff2 = 1;

                /* See comment for DI. */

                max_cycles += 4;
                break;
            }

            case IM_N:
            {
                // "IM 0/1" (0xed prefixed opcodes 0x4e and 0x6e) is treated like a "IM 0"

                if ((Y(opcode) & 0x03) <= 0x01)
                {
                    m_im = InterruptMode::IM0;
                }
                else if (!(Y(opcode) & 1))
                {
                    m_im = InterruptMode::IM1;
                }
                else
                {
                    m_im = InterruptMode::IM2;
                }

                break;
            }

                // 16-bit arithmetic group

            case ADD_HL_RR:
            {
                const uint16_t x = HL_IX_IY();
                const uint16_t y = RR(P(opcode));
                const int z = x + y;

                const int c = x ^ y ^ z;
                uint8_t f = reg_f() & SZPV_FLAG_MASK;
                f |= (z >> 8) & YX_FLAG_MASK;
                f |= (c >> 8) & H_FLAG_MASK;
                f |= c >> (16 - C_FLAG_BIT);

                HL_IX_IY() = z;
                reg_f() = f;

                elapsed_cycles += 7;

                break;
            }

            case ADC_HL_RR:
            {
                const uint16_t x = reg_hl();
                const uint16_t y = RR(P(opcode));
                const int z = x + y + (reg_f() & C_FLAG_MASK);

                const int c = x ^ y ^ z;
                uint8_t f = z & 0xffff ? (z >> 8) & SYX_FLAG_MASK : Z_FLAG_MASK;
                f |= (c >> 8) & H_FLAG_MASK;
                f |= OVERFLOW_TABLE[c >> 15];
                f |= z >> (16 - C_FLAG_BIT);

                reg_hl() = z;
                reg_f() = f;

                elapsed_cycles += 7;

                break;
            }

            case SBC_HL_RR:
            {
                const uint16_t x = reg_hl();
                const uint16_t y = RR(P(opcode));
                const int z = x - y - (reg_f() & C_FLAG_MASK);

                int c = x ^ y ^ z;
                uint8_t f = N_FLAG_MASK;
                f |= z & 0xffff ? (z >> 8) & SYX_FLAG_MASK : Z_FLAG_MASK;
                f |= (c >> 8) & H_FLAG_MASK;
                c &= 0x018000;
                f |= OVERFLOW_TABLE[c >> 15];
                f |= c >> (16 - C_FLAG_BIT);

                reg_hl() = z;
                reg_f() = f;

                elapsed_cycles += 7;

                break;
            }

            case INC_RR:
            {
                uint16_t x = RR(P(opcode));
                x++;
                RR(P(opcode)) = x;

                elapsed_cycles += 2;

                break;
            }

            case DEC_RR:
            {
                uint16_t x = RR(P(opcode));
                x--;
                RR(P(opcode)) = x;

                elapsed_cycles += 2;

                break;
            }

                // Rotate and shift group

            case RLCA:
            {
                reg_a() = (reg_a() << 1) | (reg_a() >> 7);
                reg_f() = (reg_f() & SZPV_FLAG_MASK) | (reg_a() & (YX_FLAG_MASK | C_FLAG_MASK));

                break;
            }

            case RLA:
            {
                uint8_t a = reg_a() << 1;
                uint8_t f = (reg_f() & SZPV_FLAG_MASK) | (a & YX_FLAG_MASK) | (reg_a() >> 7);
                reg_a() = a | (reg_f() & C_FLAG_MASK);
                reg_f() = f;

                break;
            }

            case RRCA:
            {
                const uint8_t c = reg_a() & 0x01;
                reg_a() = (reg_a() >> 1) | (reg_a() << 7);
                reg_f() = (reg_f() & SZPV_FLAG_MASK) | (reg_a() & YX_FLAG_MASK) | c;

                break;
            }

            case RRA:
            {
                const uint8_t c = reg_a() & 0x01;
                reg_a() = (reg_a() >> 1) | ((reg_f() & C_FLAG_MASK) << 7);
                reg_f() = (reg_f() & SZPV_FLAG_MASK) | (reg_a() & YX_FLAG_MASK) | c;

                break;
            }

            case RLC_R:
            {
                uint8_t x = R(Z(opcode));
                op_rlc(x);
                R(Z(opcode)) = x;

                break;
            }

            case RLC_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_rlc(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_rlc(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case RL_R:
            {
                uint8_t x = R(Z(opcode));
                op_rl(x);
                R(Z(opcode)) = x;

                break;
            }

            case RL_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_rl(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_rl(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case RRC_R:
            {
                uint8_t x = R(Z(opcode));
                op_rrc(x);
                R(Z(opcode)) = x;

                break;
            }

            case RRC_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_rrc(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_rrc(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case RR_R:
            {
                uint8_t x = R(Z(opcode));
                op_rr_instruction(x);
                R(Z(opcode)) = x;

                break;
            }

            case RR_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_rr_instruction(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_rr_instruction(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case SLA_R:
            {
                uint8_t x = R(Z(opcode));
                op_sla(x);
                R(Z(opcode)) = x;

                break;
            }

            case SLA_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_sla(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_sla(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case SLL_R:
            {
                uint8_t x = R(Z(opcode));
                op_sll(x);
                R(Z(opcode)) = x;

                break;
            }

            case SLL_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_sll(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_sll(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case SRA_R:
            {
                uint8_t x = R(Z(opcode));
                op_sra(x);
                R(Z(opcode)) = x;

                break;
            }

            case SRA_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_sra(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_sra(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case SRL_R:
            {
                uint8_t x = R(Z(opcode));
                op_srl(x);
                R(Z(opcode)) = x;
                break;
            }

            case SRL_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    op_srl(x);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    op_srl(x);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case RLD_RRD:
            {
                uint8_t x = m_memory.read_byte(reg_hl(), elapsed_cycles);

                uint16_t y = (reg_a() & 0xf0) << 8;
                y |= opcode == OPCODE_RLD ? (x << 4) | (reg_a() & 0x0f)
                                          : ((x & 0x0f) << 8) | ((reg_a() & 0x0f) << 4) | (x >> 4);
                m_memory.write_byte(reg_hl(), y, elapsed_cycles);
                y >>= 8;

                reg_a() = y;
                reg_f() = SZYXP_FLAGS_TABLE[y] | (reg_f() & C_FLAG_MASK);

                elapsed_cycles += 4;

                break;
            }

                // Bit set, reset, and test group

            case BIT_B_R:
            {
                int x = R(Z(opcode)) & (1 << Y(opcode));
                reg_f() = (x ? 0 : Z_FLAG_MASK | PV_FLAG_MASK) | (x & S_FLAG_MASK) | (R(Z(opcode)) & YX_FLAG_MASK) |
                          H_FLAG_MASK | (reg_f() & C_FLAG_MASK);

                break;
            }

            case BIT_B_INDIRECT_HL:
            {
                uint16_t d;

                if (is_default_table())
                {
                    d = reg_hl();

                    elapsed_cycles++;
                }
                else
                {
                    d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    pc += 2;

                    elapsed_cycles += 5;
                }

                uint8_t x = m_memory.read_byte(d, elapsed_cycles);
                x &= 1 << Y(opcode);
                reg_f() = (x ? 0 : Z_FLAG_MASK | PV_FLAG_MASK) | (x & S_FLAG_MASK) | (d & YX_FLAG_MASK) | H_FLAG_MASK |
                          (reg_f() & C_FLAG_MASK);

                break;
            }

            case SET_B_R:
            {
                R(Z(opcode)) |= 1 << Y(opcode);

                break;
            }

            case SET_B_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    x |= 1 << Y(opcode);
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    x |= 1 << Y(opcode);
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }

                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

            case RES_B_R:
            {
                R(Z(opcode)) &= ~(1 << Y(opcode));

                break;
            }

            case RES_B_INDIRECT_HL:
            {
                uint8_t x;

                if (is_default_table())
                {
                    x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                    x &= ~(1 << Y(opcode));
                    m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                    elapsed_cycles++;
                }
                else
                {
                    int d = m_memory.read_byte(pc);
                    d = static_cast<signed char>(d) + HL_IX_IY();

                    x = m_memory.read_byte(d, elapsed_cycles);
                    x &= ~(1 << Y(opcode));
                    m_memory.write_byte(d, x, elapsed_cycles);

                    if (Z(opcode) != INDIRECT_HL)
                    {
                        R(Z(opcode)) = x;
                    }
                    pc += 2;

                    elapsed_cycles += 5;
                }

                break;
            }

                // Jump group

            case JP_NN:
            {
                const uint16_t nn = m_memory.read_word(pc);
                pc = nn;

                elapsed_cycles += 6;

                break;
            }

            case JP_CC_NN:
            {
                if (test_cc(Y(opcode)))
                {
                    const uint16_t nn = m_memory.read_word(pc);

                    pc = nn;
                }
                else
                {
                    pc += 2;
                }

                elapsed_cycles += 6;

                break;
            }

            case JR_E:
            {
                const int e = m_memory.read_byte(pc);
                pc += static_cast<signed char>(e) + 1;

                elapsed_cycles += 8;

                break;
            }

            case JR_DD_E:
            {
                if (test_dd(Q(opcode)))
                {
                    const int e = m_memory.read_byte(pc);
                    pc += static_cast<signed char>(e) + 1;

                    elapsed_cycles += 8;
                }
                else
                {
                    pc++;

                    elapsed_cycles += 3;
                }

                break;
            }

            case JP_HL:
            {
                pc = HL_IX_IY();

                break;
            }

            case DJNZ_E:
            {
                if (--reg_b())
                {
                    const int e = m_memory.read_byte(pc);
                    pc += static_cast<signed char>(e) + 1;

                    elapsed_cycles += 9;
                }
                else
                {
                    pc++;

                    elapsed_cycles += 4;
                }

                break;
            }

                /* Call and return group. */

            case CALL_NN:
            {
                const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                m_memory.push(pc, elapsed_cycles);
#ifdef TRACING
                BOOST_LOG_TRIVIAL(trace) << boost::format("TRACE: Calling %04X from PC=%04X") % nn % (pc - 3);
#endif
                pc = nn;

                elapsed_cycles++;

                break;
            }

            case CALL_CC_NN:
            {
                if (test_cc(Y(opcode)))
                {
                    const auto nn = m_memory.read_word_step(pc, elapsed_cycles);
                    m_memory.push(pc, elapsed_cycles);
#ifdef TRACING
                    BOOST_LOG_TRIVIAL(trace)
                        << boost::format("TRACE: Calling %04X from PC=%04X (cond)") % nn % (pc - 3);
#endif
                    pc = nn;

                    elapsed_cycles++;
                }
                else
                {
                    pc += 2;

                    elapsed_cycles += 6;
                }

                break;
            }

            case RET:
            {
#ifdef TRACING
                BOOST_LOG_TRIVIAL(trace) << boost::format("TRACE: Returning from PC=%04X") % (pc - 1);
#endif
                pc = m_memory.pop(elapsed_cycles);
#ifdef TRACING
                BOOST_LOG_TRIVIAL(trace) << boost::format("TRACE: Returning to PC=%04X") % pc;
#endif

                break;
            }

            case RET_CC:
            {
                if (test_cc(Y(opcode)))
                {
#ifdef TRACING
                    BOOST_LOG_TRIVIAL(trace) << boost::format("TRACE: Returning from PC=%04X (cond)") % (pc - 1);
#endif
                    pc = m_memory.pop(elapsed_cycles);
#ifdef TRACING
                    BOOST_LOG_TRIVIAL(trace) << boost::format("TRACE: Returning to PC=%04X") % pc;
#endif
                }
                elapsed_cycles++;

                break;
            }

            case RETI_RETN:
            {
                m_iff1 = m_iff2;
                pc = m_memory.pop(elapsed_cycles);

                break;
            }

            case RST_P:
            {
                m_memory.push(pc, elapsed_cycles);
                pc = RST_TABLE[Y(opcode)];
                elapsed_cycles++;

                break;
            }

                /* Input and output group. */

            case IN_A_N:
            {
                const auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                reg_a() = m_memory.input_byte(n);

                elapsed_cycles += 4;

                break;
            }

            case IN_R_C:
            {
                uint8_t x = m_memory.input_byte(reg_c());
                if (Y(opcode) != INDIRECT_HL)
                {
                    R(Y(opcode)) = x;
                }

                reg_f() = SZYXP_FLAGS_TABLE[x] | (reg_f() & C_FLAG_MASK);

                elapsed_cycles += 4;

                break;
            }

                /* Some of the undocumented flags for "INI", "IND",
                 * "INIR", "INDR",  "OUTI", "OUTD", "OTIR", and
                 * "OTDR" are really really strange. The emulator
                 * implements the specifications described in "The
                 * Undocumented Z80 Documented Version 0.91".
                 */

            case INI_IND:
            {
                int x = m_memory.input_byte(reg_c());
                m_memory.write_byte(reg_hl(), x, elapsed_cycles);

                int f = SZYX_FLAGS_TABLE[--reg_b() & 0xff] | (x >> (7 - N_FLAG_BIT));
                if (opcode == OPCODE_INI)
                {
                    reg_hl()++;
                    x += (reg_c() + 1) & 0xff;
                }
                else
                {
                    reg_hl()--;
                    x += (reg_c() - 1) & 0xff;
                }
                f |= x & 0x0100 ? HC_FLAG_MASK : 0;
                f |= SZYXP_FLAGS_TABLE[(x & 0x07) ^ reg_b()] & PV_FLAG_MASK;
                reg_f() = f;

                elapsed_cycles += 5;

                break;
            }

            case INIR_INDR:
            {
                int d = opcode == OPCODE_INIR ? +1 : -1;

                int b = reg_b();
                int hl = reg_hl();

                int x, f;

                r -= 2;
                elapsed_cycles -= 8;
                for (;;)
                {
                    r += 2;

                    x = m_memory.input_byte(reg_c());
                    m_memory.write_byte(hl, x);

                    hl += d;

                    if (--b)
                    {
                        elapsed_cycles += 21;
                    }
                    else
                    {
                        f = Z_FLAG_MASK;
                        elapsed_cycles += 16;
                        break;
                    }

                    if (unbounded || (elapsed_cycles < max_cycles) || (max_cycles == 0))
                    {
                        continue;
                    }
                    else
                    {
                        f = SZYX_FLAGS_TABLE[b];
                        pc -= 2;
                        break;
                    }
                }

                reg_hl() = hl;
                reg_b() = b;

                f |= x >> (7 - N_FLAG_BIT);
                x += (reg_c() + d) & 0xff;
                f |= x & 0x0100 ? HC_FLAG_MASK : 0;
                f |= SZYXP_FLAGS_TABLE[(x & 0x07) ^ b] & PV_FLAG_MASK;
                reg_f() = f;

                break;
            }

            case OUT_N_A:
            {
                const auto n = m_memory.read_byte_step(pc, elapsed_cycles);
                m_memory.output_byte(n, reg_a());

                elapsed_cycles += 4;

                break;
            }

            case OUT_C_R:
            {
                const uint8_t x = Y(opcode) != INDIRECT_HL ? R(Y(opcode)) : 0;
                m_memory.output_byte(reg_c(), x);

                elapsed_cycles += 4;

                break;
            }

            case OUTI_OUTD:
            {
                uint8_t x = m_memory.read_byte(reg_hl(), elapsed_cycles);
                m_memory.output_byte(reg_c(), x);

                reg_hl() += opcode == OPCODE_OUTI ? +1 : -1;

                uint8_t f = SZYX_FLAGS_TABLE[--reg_b() & 0xff] | (x >> (7 - N_FLAG_BIT));
                x += reg_hl() & 0xff;
                f |= x & 0x0100 ? HC_FLAG_MASK : 0;
                f |= SZYXP_FLAGS_TABLE[(x & 0x07) ^ reg_b()] & PV_FLAG_MASK;
                reg_f() = f;

                break;
            }

            case OTIR_OTDR:
            {
                const int d = opcode == OPCODE_OTIR ? +1 : -1;

                int b = reg_b();
                int hl = reg_hl();

                uint8_t x, f;

                r -= 2;
                elapsed_cycles -= 8;
                for (;;)
                {
                    r += 2;

                    x = m_memory.read_byte(hl);
                    m_memory.output_byte(reg_c(), x);

                    hl += d;
                    if (--b)
                    {
                        elapsed_cycles += 21;
                    }
                    else
                    {
                        f = Z_FLAG_MASK;
                        elapsed_cycles += 16;
                        break;
                    }

                    if (unbounded || (elapsed_cycles < max_cycles) || (max_cycles == 0))
                    {
                        continue;
                    }
                    else
                    {
                        f = SZYX_FLAGS_TABLE[b];
                        pc -= 2;
                        break;
                    }
                }

                reg_hl() = hl;
                reg_b() = b;

                f |= x >> (7 - N_FLAG_BIT);
                x += hl & 0xff;
                f |= x & 0x0100 ? HC_FLAG_MASK : 0;
                f |= SZYXP_FLAGS_TABLE[(x & 0x07) ^ b] & PV_FLAG_MASK;
                reg_f() = f;

                break;
            }

                // Prefix group

            case CB_PREFIX:
            {
                // Special handling if the 0xcb prefix is prefixed by a 0xdd or 0xfd prefix

                if (!(is_default_table()))
                {
                    r--;
                    // Indexed memory access routine will correctly update pc
                    opcode = m_memory.read_byte(pc + 1);
                }
                else
                {
                    opcode = m_memory.read_byte(pc);
                    pc++;
                }
                instruction = CB_INSTRUCTION_TABLE[opcode];

                goto emulate_next_instruction; // NOLINT: imported 3rd-party code
            }

            case DD_PREFIX:
            {
                set_dd();

                opcode = m_memory.read_byte(pc);
                pc++;
                goto emulate_next_opcode; // NOLINT: imported 3rd-party code
            }

            case FD_PREFIX:
            {
                set_fd();

                opcode = m_memory.read_byte(pc);
                pc++;
                goto emulate_next_opcode; // NOLINT: imported 3rd-party code
            }

            case ED_PREFIX:
            {
                set_default_table();

                opcode = m_memory.read_byte(pc);
                pc++;
                instruction = ED_INSTRUCTION_TABLE[opcode];

                goto emulate_next_instruction; // NOLINT: imported 3rd-party code
            }

                // Special/pseudo instruction group

            case ED_UNDEFINED:
            {
                break;
            }
            }

            // Have we reached the specified maximum cycle count?
            if (!unbounded && (elapsed_cycles >= max_cycles))
            {
                goto stop_emulation; // NOLINT: imported 3rd-party code
            }

            // Do we have any debug actions for this address?
            if (m_debug_actions.count(pc) > 0)
            {
                // Yes, we have one or more, find them and evaluate them.  If any evaluate to false,
                // then we stop the emulation (typically to return to the debugger).
                const auto [start, stop] = m_debug_actions.equal_range(pc);
                if (std::any_of(start, stop, [pc](const auto& a) { return a.second->evaluate(pc) == false; }))
                {
                    m_processor_observer.set_finished(true);
                    goto stop_emulation; // NOLINT: imported 3rd-party code
                }
            }
        }

    stop_emulation:

        m_r = (m_r & 0x80) | (r & 0x7f);
        m_pc = m_effective_pc = pc & 0xffff;

        return elapsed_cycles;
    }

    bool Processor::test_cc(uint8_t cc)
    {
        return (reg_f() ^ XOR_CONDITION_TABLE[cc]) & AND_CONDITION_TABLE[cc];
    }

    bool Processor::test_dd(uint8_t dd)
    {
        return test_cc(dd);
    }

    uint8_t Processor::read_indirect_hl(uint16_t& pc, size_t& elapsed_cycles)
    {
        uint8_t x;
        if (is_default_table())
        {
            x = m_memory.read_byte(reg_hl(), elapsed_cycles);
        }
        else
        {
            auto d = static_cast<int>(m_memory.read_byte_step(pc, elapsed_cycles));
            d += HL_IX_IY();
            x = m_memory.read_byte(d, elapsed_cycles);
            elapsed_cycles += 5;
        }
        return x;
    }

    void Processor::write_indirect_hl(uint8_t x, uint16_t& pc, size_t& elapsed_cycles)
    {
        if (is_default_table())
        {
            m_memory.write_byte(reg_hl(), x, elapsed_cycles);
        }
        else
        {
            auto d = static_cast<int>(m_memory.read_byte_step(pc, elapsed_cycles));
            d += HL_IX_IY();
            x = m_memory.read_byte(d, elapsed_cycles);
            elapsed_cycles += 5;
        }
    }

    void Processor::op_add(uint8_t x)
    {
        const uint8_t a = reg_a();
        int z = a + x;
        int c = a ^ x ^ z;
        int f = c & H_FLAG_MASK;
        f |= SZYX_FLAGS_TABLE[z & 0xff];
        f |= OVERFLOW_TABLE[c >> 7];
        f |= z >> (8 - C_FLAG_BIT);
        reg_a() = z;
        reg_f() = f;
    }

    void Processor::op_adc(uint8_t x)
    {
        const uint8_t a = reg_a();
        int z = a + x + (reg_f() & C_FLAG_MASK);
        int c = a ^ x ^ z;
        int f = c & H_FLAG_MASK;
        f |= SZYX_FLAGS_TABLE[z & 0xff];
        f |= OVERFLOW_TABLE[c >> 7];
        f |= z >> (8 - C_FLAG_BIT);
        reg_a() = z;
        reg_f() = f;
    }

    void Processor::op_sub(uint8_t x)
    {
        const uint8_t a = reg_a();
        int z = a - x;
        int c = a ^ x ^ z;
        int f = N_FLAG_MASK | (c & H_FLAG_MASK);
        f |= SZYX_FLAGS_TABLE[z & 0xff];
        c &= 0x0180;
        f |= OVERFLOW_TABLE[c >> 7];
        f |= c >> (8 - C_FLAG_BIT);
        reg_a() = z;
        reg_f() = f;
    }

    void Processor::op_sbc(uint8_t x)
    {
        const uint8_t a = reg_a();
        int z = a - x - (reg_f() & C_FLAG_MASK);
        int c = a ^ x ^ z;
        int f = N_FLAG_MASK | (c & H_FLAG_MASK);
        f |= SZYX_FLAGS_TABLE[z & 0xff];
        c &= 0x0180;
        f |= OVERFLOW_TABLE[c >> 7];
        f |= c >> (8 - C_FLAG_BIT);
        reg_a() = z;
        reg_f() = f;
    }

    void Processor::op_and(uint8_t x)
    {
        reg_f() = SZYXP_FLAGS_TABLE[reg_a() &= x] | H_FLAG_MASK;
    }

    void Processor::op_or(uint8_t x)
    {
        reg_f() = SZYXP_FLAGS_TABLE[reg_a() |= x];
    }

    void Processor::op_xor(uint8_t x)
    {
        reg_f() = SZYXP_FLAGS_TABLE[reg_a() ^= x];
    }

    void Processor::op_cp(uint8_t x)
    {
        const uint8_t a = reg_a();
        int z = a - x;

        int c = a ^ x ^ z;
        int f = N_FLAG_MASK | (c & H_FLAG_MASK);
        f |= SZYX_FLAGS_TABLE[z & 0xff] & SZ_FLAG_MASK;
        f |= x & YX_FLAG_MASK;
        c &= 0x0180;
        f |= OVERFLOW_TABLE[c >> 7];
        f |= c >> (8 - C_FLAG_BIT);

        reg_f() = f;
    }

    void Processor::op_inc(uint8_t& x)
    {
        int z = x + 1;
        int c = x ^ z;

        int f = reg_f() & C_FLAG_MASK;
        f |= c & H_FLAG_MASK;
        f |= SZYX_FLAGS_TABLE[z & 0xff];
        f |= OVERFLOW_TABLE[(c >> 7) & 0x03];

        x = z;
        reg_f() = f;
    }

    void Processor::op_dec(uint8_t& x)
    {
        int z = x - 1;
        int c = x ^ z;

        int f = N_FLAG_MASK | (reg_f() & C_FLAG_MASK);
        f |= c & H_FLAG_MASK;
        f |= SZYX_FLAGS_TABLE[z & 0xff];
        f |= OVERFLOW_TABLE[(c >> 7) & 0x03];

        x = z;
        reg_f() = f;
    }

    void Processor::op_rlc(uint8_t& x)
    {
        const uint8_t c = x >> 7;
        x = (x << 1) | c;
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

    void Processor::op_rl(uint8_t& x)
    {
        const uint8_t c = x >> 7;
        x = (x << 1) | (reg_f() & C_FLAG_MASK);
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

    void Processor::op_rrc(uint8_t& x)
    {
        const uint8_t c = x & 0x01;
        x = (x >> 1) | (c << 7);
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

    void Processor::op_rr_instruction(uint8_t& x)
    {
        const uint8_t c = x & 0x01;
        x = (x >> 1) | ((reg_f() & C_FLAG_MASK) << 7);
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

    void Processor::op_sla(uint8_t& x)
    {
        const uint8_t c = x >> 7;
        x <<= 1;
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

    void Processor::op_sll(uint8_t& x)
    {
        const uint8_t c = x >> 7;
        x = (x << 1) | 0x01;
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

    void Processor::op_sra(uint8_t& x)
    {
        const uint8_t c = x & 0x01;
        x = static_cast<signed char>(x) >> 1;
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

    void Processor::op_srl(uint8_t& x)
    {
        const uint8_t c = x & 0x01;
        x >>= 1;
        reg_f() = SZYXP_FLAGS_TABLE[x & 0xff] | c;
    }

} // namespace zcpm
