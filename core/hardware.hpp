#pragma once

#include "config.hpp"
#include "handlers.hpp"
#include "imemory.hpp"
#include "processor.hpp"
#include "symboltable.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace zcpm
{

namespace terminal
{
    class Terminal;
}
class Bios;
class IDebuggable;

class Hardware final
    : public IProcessorObserver
    , public IMemory
{
public:
    Hardware(std::unique_ptr<terminal::Terminal> p_terminal, const Config& behaviour);

    Hardware(const Hardware&) = delete;
    Hardware& operator=(const Hardware&) = delete;
    Hardware(Hardware&&) = delete;
    Hardware& operator=(Hardware&&) = delete;

    ~Hardware() override;

    // Methods to hook up user handlers on events of interest
    void set_input_handler(const InputHandler& h);
    void set_output_handler(const OutputHandler& h);

    // Configure where FBASE and WBOOT are (to help the system recognise BDOS & BIOS accesses)
    void set_fbase_and_wboot(std::uint16_t fbase, std::uint16_t wboot);

    // Call the BIOS 'BOOT' & 'WBOOT' functions, in order to initialise BIOS data structures for subsequent BIOS operations
    void call_bios_boot();

    // Call the specified BDOS function.  This is needed as part of system initialisation.
    void call_bdos(int op);

    void reset();

    // Implements IProcessorObserver

    void set_finished(bool finished) override;
    [[nodiscard]] bool running() const override;
    [[nodiscard]] bool check_and_handle_bdos_and_bios(std::uint16_t address) const override;

    //

    // TODO: Add more methods to set/query/remove memory watch points
    void add_watch_read(std::uint16_t base, std::uint16_t count = 1);
    void add_watch_write(std::uint16_t base, std::uint16_t count = 1);

    // Directly add a one-off entry to the symbol table, which can be helpful for analysing run logs
    void add_symbol(std::uint16_t a, std::string_view label);

    // Implements IMemory

    [[nodiscard]] std::uint8_t read_byte(std::uint16_t address) const override;
    std::uint8_t read_byte(std::uint16_t address, size_t& elapsed_cycles) const override;
    [[nodiscard]] std::uint16_t read_word(std::uint16_t address) const override;
    std::uint16_t read_word(std::uint16_t address, size_t& elapsed_cycles) const override;
    void write_byte(std::uint16_t address, std::uint8_t x) override;
    void write_byte(std::uint16_t address, std::uint8_t x, size_t& elapsed_cycles) override;
    void write_word(std::uint16_t address, std::uint16_t x) override;
    void write_word(std::uint16_t address, std::uint16_t x, size_t& elapsed_cycles) override;
    std::uint8_t read_byte_step(std::uint16_t& address, size_t& elapsed_cycles) const override;
    std::uint16_t read_word_step(std::uint16_t& address, size_t& elapsed_cycles) const override;
    void push(std::uint16_t x, size_t& elapsed_cycles) override;
    std::uint16_t pop(size_t& elapsed_cycles) override;
    std::uint8_t input_byte(int port) override;
    void output_byte(int port, std::uint8_t x) override;
    void copy_to_ram(const std::uint8_t* buffer, size_t count, std::uint16_t base) override;
    void copy_from_ram(std::uint8_t* buffer, size_t count, std::uint16_t base) const override;
    void dump(std::uint16_t base, size_t count) const override;
    void check_memory_accesses(bool protect) override;

    //

    // Return human-readable info about the stack state
    [[nodiscard]] std::string format_stack_info() const;

    void dump_symbol_table() const;

    // Try to evaluate an expression such as 'foo1' where 'foo1' is a known label or perhaps 'foo2+23'.  Note that all values are
    // hexadecimal.  Returns a (success,value) pair, success=false means an evaluation failure.
    [[nodiscard]] std::tuple<bool, std::uint16_t> evaluate_address_expression(std::string_view s) const;

    [[nodiscard]] IDebuggable* get_idebuggable() const;

    // This is public, and is an ugly hack. The underlying problem is that the system we're emulated is tightly coupled, so it's hard to
    // avoid the same patterns in emulation.
    std::unique_ptr<Processor> m_processor;

private:
    enum class Access
    {
        READ,
        WRITE
    };

    void check_watched_memory_byte(std::uint16_t address, Access mode, std::uint8_t value) const;
    void check_watched_memory_word(std::uint16_t address, Access mode, std::uint16_t value) const;

    [[nodiscard]] std::string describe_address(std::uint16_t a) const;

    // Returns true if an attempted write to the specified address should be fatal for ZCPM
    [[nodiscard]] bool is_fatal_write(std::uint16_t address) const;

    // Runtime options from commandline switches
    const Config m_config;

    // Optional handler to call when we do a Z80 'IN'
    InputHandler m_input_handler;

    // Optional handler to call when we do a Z80 'OUT'
    OutputHandler m_output_handler;

    std::unique_ptr<terminal::Terminal> m_pterminal;

    std::array<std::uint8_t, 0x10000> m_memory{};

    bool m_finished{ false };

    std::unique_ptr<Bios> m_pbios;

    bool m_check_memory_accesses{ false }; // Indicates if we have temporarily allowed/disallowed memory checks

    // Addresses that we are watching; for now we just log their access, but longer-term we'll invoke a user-supplied handler
    std::unordered_set<std::uint16_t> m_watch_read;

    // Addresses that we are watching; for now we just log their access, but longer-term we'll invoke a user-supplied handler
    std::unordered_set<std::uint16_t> m_watch_write;

    std::uint16_t m_fbase{ 0 };

    // Table of known symbols.
    SymbolTable m_symbols;
};

} // namespace zcpm
