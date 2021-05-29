#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_set>

#include "handlers.hpp"
#include "imemory.hpp"
#include "processor.hpp"
#include "symboltable.hpp"

namespace ZCPM
{

    namespace Terminal
    {
        class Terminal;
    }
    class Bios;
    class IDebuggable;
    class Processor;

    class Hardware final
        : public IProcessorObserver
        , public IMemory
    {
    public:
        Hardware(std::unique_ptr<Terminal::Terminal> p_terminal,
                 bool memcheck,
                 const std::string& bdos_sym = "",
                 const std::string& user_sym = "");

        ~Hardware();

        // Methods to hook up user handlers on events of interest
        void set_input_handler(const InputHandler& h);
        void set_output_handler(const OutputHandler& h);

        // Configure where FBASE and WBOOT are (to help the system recognise BDOS & BIOS accesses)
        void set_fbase_and_wboot(uint16_t fbase, uint16_t wboot);

        // Call the BIOS 'BOOT' & 'WBOOT' functions, in order to initialise BIOS data structures for subsequent BIOS
        // operations
        void call_bios_boot();

        // Call the specified BDOS function.  This is needed as part of system initialisation.
        void call_bdos(int op);

        void reset();

        // Implements IProcessorObserver

        void set_finished(bool finished) override;
        bool running() const override;
        bool check_and_handle_bdos_and_bios(uint16_t address) const override;

        //

        // TODO: Add more methods to set/query/remove memory watch points
        void add_watch_read(uint16_t base, uint16_t count = 1);
        void add_watch_write(uint16_t base, uint16_t count = 1);

        // Directly add a one-off entry to the symbol table, which can be helpful for analysing run logs
        void add_symbol(uint16_t a, const std::string& label);

        // Implements IMemory

        uint8_t read_byte(uint16_t address) const override;
        uint8_t read_byte(uint16_t address, size_t& elapsed_cycles) const override;
        uint16_t read_word(uint16_t address) const override;
        uint16_t read_word(uint16_t address, size_t& elapsed_cycles) const override;
        void write_byte(uint16_t address, uint8_t x) override;
        void write_byte(uint16_t address, uint8_t x, size_t& elapsed_cycles) override;
        void write_word(uint16_t address, uint16_t x) override;
        void write_word(uint16_t address, uint16_t x, size_t& elapsed_cycles) override;
        uint8_t read_byte_step(uint16_t& address, size_t& elapsed_cycles) const override;
        uint16_t read_word_step(uint16_t& address, size_t& elapsed_cycles) const override;
        void push(uint16_t x, size_t& elapsed_cycles) override;
        uint16_t pop(size_t& elapsed_cycles) override;
        uint8_t input_byte(int port) override;
        void output_byte(int port, uint8_t x) override;
        void copy_to_ram(const uint8_t* buffer, size_t count, uint16_t base) override;
        void copy_from_ram(uint8_t* buffer, size_t count, uint16_t base) const override;
        void dump(uint16_t base, size_t count) const override;
        void check_memory_accesses(bool protect) override;

        //

        // Return human-readable info about the stack state
        std::string format_stack_info() const;

        void dump_symbol_table() const;

        // Try to evaluate an expression such as 'foo1' where 'foo1' is a known label or perhaps 'foo2+23'.  Note that
        // all values are hexadecimal.  Returns a (success,value) pair, success=false means an evaluation failure.
        std::tuple<bool, uint16_t> evaluate_address_expression(const std::string& s) const;

        IDebuggable* get_idebuggable() const;

        // This is public, and is an ugly hack. The underlying problem is that the system we're emulated is tightly
        // coupled so it's hard to avoid the same patterns in emulation.
        std::unique_ptr<Processor> m_processor;

    private:
        enum class Access
        {
            READ,
            WRITE
        };

        void check_watched_memory_byte(uint16_t address, Access mode, uint8_t value) const;
        void check_watched_memory_word(uint16_t address, Access mode, uint16_t value) const;

        std::string describe_address(uint16_t a) const;

        // Optional handler to call when we do a Z80 'IN'
        InputHandler m_input_handler;

        // Optional handler to call when we do a Z80 'OUT'
        OutputHandler m_output_handler;

        std::unique_ptr<Terminal::Terminal> m_pterminal;

        std::array<uint8_t, 0x10000> m_memory{};

        bool m_finished = false;

        std::unique_ptr<Bios> m_pbios;

        bool m_check_memory_accesses = false; // Indicates if we have temporarily allowed/disallowed memory checks
        bool m_enable_memory_checks; // The 'master switch' for memory checks, overrides temporary setting above

        // Addresses that we are watching; for now we just log their access, but longer-term we'll invoke a
        // user-supplied handler
        std::unordered_set<uint16_t> m_watch_read;

        // Addresses that we are watching; for now we just log their access, but longer-term we'll invoke a
        // user-supplied handler
        std::unordered_set<uint16_t> m_watch_write;

        uint16_t m_fbase = 0;
        uint16_t m_wboot = 0;

        // Table of known symbols.
        SymbolTable m_symbols;
    };

} // namespace ZCPM
