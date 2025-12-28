#pragma once

#include "debugaction.hpp"
#include "idebuggable.hpp"
#include "imemory.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <vector>

namespace zcpm
{

class Registers;

// TODO: Not sure about the best name for this; it's not a true Observer?
class IProcessorObserver
{
public:
    virtual ~IProcessorObserver() = default;

    IProcessorObserver() = default;
    IProcessorObserver(const IProcessorObserver& other) = delete;
    IProcessorObserver(IProcessorObserver&& other) = delete;
    IProcessorObserver& operator=(const IProcessorObserver& other) = delete;
    IProcessorObserver& operator=(IProcessorObserver&& other) = delete;

    // Can be used from user-supplied handlers to stop execution
    virtual void set_finished(bool finished) = 0;

    // Are we still meant to be running? (i.e., stop() hasn't yet been called)
    [[nodiscard]] virtual bool running() const = 0;

    // Check if the specified address is within our custom BIOS implementation (and hence should be intercepted). If
    // so, works out what the intercepted BIOS call is trying to do and does whatever is needed, and then allows the
    // caller to return to normal processing. Returns true if BIOS was intercepted.
    virtual bool check_and_handle_bdos_and_bios(std::uint16_t address) const = 0;
};

class Processor final : public IDebuggable
{
public:
    Processor(IMemory& memory, IProcessorObserver& processor_observer);

    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;
    Processor(Processor&&) = delete;
    Processor& operator=(Processor&&) = delete;

    ~Processor() override = default;

    // Initialise processor's state to power-on default
    void reset_state();

    // Trigger an interrupt according to the current interrupt mode and return the number of cycles elapsed to
    // accept it. If maskable interrupts are disabled, this will return zero. In interrupt mode 0, data_on_bus must
    // be a single byte opcode
    size_t interrupt(std::uint8_t data_on_bus);

    // Trigger a non-maskable interrupt, then return the number of cycles elapsed to accept it
    size_t non_maskable_interrupt();

    // Execute instructions until completion or a breakpoint
    // Returns the number of cycles consumed
    size_t emulate();

    // Execute a single instruction
    // Returns the number of cycles consumed
    size_t emulate_instruction();

    // Individual read-only getters for registers
    [[nodiscard]] std::uint8_t get_a() const;
    [[nodiscard]] std::uint8_t get_f() const;
    [[nodiscard]] std::uint8_t get_b() const;
    [[nodiscard]] std::uint8_t get_c() const;
    [[nodiscard]] std::uint8_t get_d() const;
    [[nodiscard]] std::uint8_t get_e() const;
    [[nodiscard]] std::uint8_t get_h() const;
    [[nodiscard]] std::uint8_t get_l() const;
    [[nodiscard]] std::uint16_t get_af() const;
    [[nodiscard]] std::uint16_t get_bc() const;
    [[nodiscard]] std::uint16_t get_de() const;
    [[nodiscard]] std::uint16_t get_hl() const;
    [[nodiscard]] std::uint16_t get_sp() const;
    [[nodiscard]] std::uint16_t get_pc() const; // This returns m_effective_pc, can be a trap

    // These return a writable reference to the current content of the specified register. These need to be public
    // so that (for example) the BIOS can set registers. Use these with care!
    std::uint8_t& reg_a();
    std::uint8_t& reg_f();
    std::uint8_t& reg_b();
    std::uint8_t& reg_c();
    std::uint8_t& reg_d();
    std::uint8_t& reg_e();
    std::uint8_t& reg_h();
    std::uint8_t& reg_l();
    std::uint16_t& reg_af();
    std::uint16_t& reg_bc();
    std::uint16_t& reg_de();
    std::uint16_t& reg_hl();
    std::uint16_t& reg_sp();
    std::uint16_t& reg_pc();

    // Implementation of IDebuggable

    Registers get_registers() const override;
    std::tuple<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::vector<std::uint8_t>> get_opcodes_at(
        std::uint16_t pc,
        std::uint16_t offset) const override;
    void add_action(std::unique_ptr<DebugAction> p_action) override;
    void show_actions(std::ostream& os) const override;
    bool remove_action(size_t index) override;

    // Z80 flag positions in the F register
    inline static const std::uint8_t S_FLAG_BIT{ 7 };
    inline static const std::uint8_t Z_FLAG_BIT{ 6 };
    inline static const std::uint8_t Y_FLAG_BIT{ 5 };
    inline static const std::uint8_t H_FLAG_BIT{ 4 };
    inline static const std::uint8_t X_FLAG_BIT{ 3 };
    inline static const std::uint8_t PV_FLAG_BIT{ 2 };
    inline static const std::uint8_t N_FLAG_BIT{ 1 };
    inline static const std::uint8_t C_FLAG_BIT{ 0 };

    // Bitmasks for testing those flags
    inline static const std::uint8_t S_FLAG_MASK{ 1 << S_FLAG_BIT };
    inline static const std::uint8_t Z_FLAG_MASK{ 1 << Z_FLAG_BIT };
    inline static const std::uint8_t Y_FLAG_MASK{ 1 << Y_FLAG_BIT };
    inline static const std::uint8_t H_FLAG_MASK{ 1 << H_FLAG_BIT };
    inline static const std::uint8_t X_FLAG_MASK{ 1 << X_FLAG_BIT };
    inline static const std::uint8_t PV_FLAG_MASK{ 1 << PV_FLAG_BIT };
    inline static const std::uint8_t N_FLAG_MASK{ 1 << N_FLAG_BIT };
    inline static const std::uint8_t C_FLAG_MASK{ 1 << C_FLAG_BIT };

private:
    // The main registers are stored as a union of arrays named registers. They are referenced using indexes. Words
    // are stored in the endianness of the host processor. The alternate set of word registers AF', BC', DE', and
    // HL' is stored in the alternates member, as an array using the same ordering.
    union
    {
        std::uint8_t byte[14];
        std::uint16_t word[7];
    } m_registers{};

    std::uint16_t m_alternates[4]{}; // "Shadow" registers

    std::uint16_t m_i{ 0 };
    std::uint16_t m_r{ 0 };
    std::uint16_t m_pc{ 0 };
    std::uint16_t m_iff1{ 0 };
    std::uint16_t m_iff2{ 0 };

    // Internally, the emulation only updates m_pc when it is exiting, it uses 'pc' internally which is updated byte
    // by byte. Neither is exactly what we want when adding in debug hooks, and changing those two uses to support
    // debug hooks is difficult. So 'effective' PC is added which is just used as a return value for use by
    // debuggers.
    std::uint16_t m_effective_pc{ 0 };

    enum class InterruptMode
    {
        IM0,
        IM1,
        IM2
    };
    InterruptMode m_im{ InterruptMode::IM0 };

    // Register decoding tables
    void *m_register_table[16]{}, *m_dd_register_table[16]{}, *m_fd_register_table[16]{};

    bool is_default_table() const;
    void set_default_table();
    void set_dd();
    void set_fd();

    // Access registers via indirection tables. S() is for the special cases "LD H/L, (IX/Y + d)"
    // and "LD (IX/Y + d), H/L".
    [[nodiscard]] std::uint8_t& R(int r) const;
    [[nodiscard]] std::uint8_t& S(int s) const;
    [[nodiscard]] std::uint16_t& RR(int rr) const;
    [[nodiscard]] std::uint16_t& SS(int ss) const;
    [[nodiscard]] std::uint16_t& HL_IX_IY() const;

    // Run until either a breakpoint or termination or elapsed_cycles (+emulated cycles) >= max_cycles.
    // Using a 'max_cycles' value of zero is equivalent to emulating a single instruction.
    // 'unbounded' means to run continuously until a HALT or similar is encountered.
    size_t emulate(std::uint8_t opcode, bool unbounded, size_t elapsed_cycles = 0, size_t max_cycles = 0);

    // Helper methods which were originally macros which accessed global data. As a result some of these have
    // somewhat ugly signatures & semantics, but that's because they're gradually being changed from macros which
    // use other macros and globals into something eventually better.
    bool test_cc(std::uint8_t cc);
    bool test_dd(std::uint8_t dd);
    std::uint8_t read_indirect_hl(std::uint16_t& pc, size_t& elapsed_cycles);
    void op_add(std::uint8_t x);
    void op_adc(std::uint8_t x);
    void op_sub(std::uint8_t x);
    void op_sbc(std::uint8_t x);
    void op_and(std::uint8_t x);
    void op_or(std::uint8_t x);
    void op_xor(std::uint8_t x);
    void op_cp(std::uint8_t x);
    void op_inc(std::uint8_t& x);
    void op_dec(std::uint8_t& x);
    void op_rlc(std::uint8_t& x);
    void op_rl(std::uint8_t& x);
    void op_rrc(std::uint8_t& x);
    void op_rr_instruction(std::uint8_t& x);
    void op_sla(std::uint8_t& x);
    void op_sll(std::uint8_t& x);
    void op_sra(std::uint8_t& x);
    void op_srl(std::uint8_t& x);

    // Current register decoding table, use it to determine if the current instruction is prefixed. It points to:
    //   m_dd_register_table for 0xdd prefixes;
    //   m_fd_register_table for 0xfd prefixes;
    //   m_register_table otherwise.
    void** m_current_register_table;

    IMemory& m_memory;
    IProcessorObserver& m_processor_observer;

    // Use an ordered map to make displaying of actions nicer. Given that we don't expect to have more than a few
    // actions defined at any one time, the performance improvements of unordered_map aren't an issue here.
    std::multimap<std::uint16_t, std::unique_ptr<DebugAction>> m_debug_actions;
};

} // namespace zcpm
