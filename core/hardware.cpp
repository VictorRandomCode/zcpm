#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <zcpm/terminal/terminal.hpp>

#include "bdos.hpp"
#include "bios.hpp"
#include "hardware.hpp"
#include "processor.hpp"
#include "registers.hpp"

namespace
{
    // Returns true if an attempted write to the specified address should be fatal for ZCPM
    bool is_fatal_write(uint16_t address)
    {
        // Note that a few programs will try to modify the warm start vector (at locations 00,01,02)
        // to hook in their own intercepts. For example Supercalc.
        // TODO: Add a runtime flag to enable/disable this check.
        if (address <= 0x0002)
        {
            // Warm start vector
            return true;
        }

        // Note that CP/M debuggers will intercept the BDOS jump vector, which means that they'll
        // try to modify address 0006 & 0007.  But normal programs won't do that.
        // TODO: Add a runtime flag to enable/disable this check.
        if ((address >= 0x0005) && (address <= 0x0007))
        {
            // BDOS jump vector
            return true;
        }

        // Everything else is ok
        return false;
    }

} // namespace

namespace zcpm
{

    Hardware::Hardware(std::unique_ptr<terminal::Terminal> p_terminal,
                       bool memcheck,
                       const std::string& bdos_sym,
                       const std::string& user_sym)
        : m_processor(std::make_unique<Processor>(*this, *this)),
          m_pterminal(std::move(p_terminal)),
          m_enable_memory_checks(memcheck)
    {
        m_memory.fill(0);

        // TODO: Add setters/getters etc for this kind of thing, rather than hiding it here

        // Monitor *any* write of page zero
        add_watch_write(0x0000, 0x0100);

        // Monitor reads in page zero, except for the BDOS or BIOS jump vectors
        add_watch_read(0x0003, 2);
        add_watch_read(0x0008, (0x0100 - 8));

        // Load the symbol table
        m_symbols.load(bdos_sym, "BDOS");
        m_symbols.load(user_sym, "USER");

        // And a symbol to help investigate accesses to the very top of memory.  I *think* these
        // are because of our direct call to a BDOS function on startup in which case all is well,
        // but I'm yet to investigate that properly.
        add_symbol(0xFFF0, "TBD!");
    }

    Hardware::~Hardware() = default;

    void Hardware::set_input_handler(const InputHandler& h)
    {
        m_input_handler = h;
    }

    void Hardware::set_output_handler(const OutputHandler& h)
    {
        m_output_handler = h;
    }

    void Hardware::set_fbase_and_wboot(uint16_t fbase, uint16_t wboot)
    {
        m_fbase = fbase;
        m_wboot = wboot;

        // Set up jump to BIOS 'WBOOT' from 0000
        write_byte(0x0000, 0xc3);           // JP
        write_byte(0x0001, wboot & 0x00FF); // Low byte of 'WBOOT' from assembled code
        write_byte(0x0002, wboot >> 8);     // High byte of 'WBOOT' from assembled code

        // Set up jump to BDOS ('FBASE') from 0005
        write_byte(0x0005, 0xc3);           // JP
        write_byte(0x0006, fbase & 0x00FF); // Low byte of 'FBASE' from assembled code
        write_byte(0x0007, fbase >> 8);     // High byte of 'FBASE' from assembled code

        // Find the start of the BIOS in the current memory image, and then manipulate the
        // jump tables etc so that we can intercept BIOS calls ourselves.
        m_pbios = std::make_unique<Bios>(this, m_pterminal.get());
    }

    void Hardware::call_bios_boot()
    {
        m_pbios->fn_boot();
        m_pbios->fn_wboot();
    }

    void Hardware::call_bdos(int op)
    {
        m_processor->reg_c() = op;
        m_processor->reg_pc() = 0x0005;
        m_processor->emulate();
    }

    void Hardware::reset()
    {
        m_processor->reset_state();
    }

    void Hardware::set_finished(bool finished)
    {
        m_finished = finished;
    }

    bool Hardware::running() const
    {
        return !m_finished;
    }

    bool Hardware::check_and_handle_bdos_and_bios(uint16_t address) const
    {
        // Note that BDOS calls are logged but not intercepted.  But BIOS calls are logged *and* intercepted. This is
        // because our BDOS is implemented via a binary blob (a real BDOS implementation), which will in turn make calls
        // into our own custom BIOS implementation. So BDOS calls are checked & logged but not intercepted, but BIOS
        // calls need to be intercepted and translated to (e.g.) host system calls.

        // Does this appear to be a BDOS call?  i.e., a jump to FBASE from 0005?
        if (address == m_fbase)
        {
            // TODO: the following code is just for logging/debugging. If performance becomes an issue, execution of
            // this block should be conditional on a new boolean. Although this is only executed on each entry to BDOS,
            // which isn't that bad...
            if (true) // TODO: execution to be controlled by a new run-time option
            {
                // "Parse" the pending BDOS call into various bits of useful information
                const auto registers = m_processor->get_registers();
                const auto [bdos_name, description] = bdos::describe_call(registers, *this);

                // Log the information
                BOOST_LOG_TRIVIAL(trace) << "BDOS: " << bdos_name << format_stack_info();
                BOOST_LOG_TRIVIAL(trace) << "BDOS: " << description;
            }

            return false; // BIOS was not intercepted
        }

        if (m_pbios)
        {
            return m_pbios->check_and_handle(address);
        }
        else
        {
            return false;
        }
    }

    void Hardware::add_watch_read(uint16_t base, uint16_t count)
    {
        for (auto i = base; i < base + count; ++i)
        {
            m_watch_read.insert(i);
        }
    }

    void Hardware::add_watch_write(uint16_t base, uint16_t count)
    {
        for (auto i = base; i < base + count; ++i)
        {
            m_watch_write.insert(i);
        }
    }

    void Hardware::add_symbol(uint16_t a, const std::string& label)
    {
        m_symbols.add("ZCPM", a, label);
    }

    uint8_t Hardware::read_byte(uint16_t address) const
    {
        const auto result = m_memory[address];
        check_watched_memory_byte(address, Access::READ, result);
        return result;
    }

    uint8_t Hardware::read_byte(uint16_t address, size_t& elapsed_cycles) const
    {
        const auto result = read_byte(address);
        elapsed_cycles += 3;
        return result;
    }

    uint16_t Hardware::read_word(uint16_t address) const
    {
        const auto result_low = m_memory[address];
        const auto result_high = m_memory[(address + 1) & 0xffff];
        const auto result = result_low | (result_high << 8);
        check_watched_memory_word(address, Access::READ, result);
        return result;
    }

    uint16_t Hardware::read_word(uint16_t address, size_t& elapsed_cycles) const
    {
        const auto result = read_word(address);
        elapsed_cycles += 6;
        return result;
    }

    void Hardware::write_byte(uint16_t address, uint8_t x)
    {
        check_watched_memory_byte(address, Access::WRITE, x);
        m_memory[address] = x;
    }

    void Hardware::write_byte(uint16_t address, uint8_t x, size_t& elapsed_cycles)
    {
        write_byte(address, x);
        elapsed_cycles += 3;
    }

    void Hardware::write_word(uint16_t address, uint16_t x)
    {
        check_watched_memory_word(address, Access::WRITE, x);
        m_memory[address] = x;
        m_memory[(address + 1) & 0xffff] = x >> 8;
    }

    void Hardware::write_word(uint16_t address, uint16_t x, size_t& elapsed_cycles)
    {
        write_word(address, x);
        elapsed_cycles += 6;
    }

    uint8_t Hardware::read_byte_step(uint16_t& address, size_t& elapsed_cycles) const
    {
        const auto result = read_byte(address);
        address++;
        elapsed_cycles += 3;
        return result;
    }

    uint16_t Hardware::read_word_step(uint16_t& address, size_t& elapsed_cycles) const
    {
        const auto result = read_word(address);
        address += 2;
        elapsed_cycles += 6;
        return result;
    }

    void Hardware::push(uint16_t x, size_t& elapsed_cycles)
    {
        m_processor->reg_sp() -= 2;
        write_word(m_processor->reg_sp(), x, elapsed_cycles);
    }

    uint16_t Hardware::pop(size_t& elapsed_cycles)
    {
        const auto result = read_word(m_processor->reg_sp(), elapsed_cycles);
        m_processor->reg_sp() += 2;
        return result;
    }

    uint8_t Hardware::input_byte(int port)
    {
        if (m_input_handler)
        {
            try
            {
                return m_input_handler(*this, port);
            }
            catch (const std::exception& e)
            {
                BOOST_LOG_TRIVIAL(trace) << "Exception in user input handler: " << e.what();
                return 0;
            }
        }
        else
        {
            return 0;
        }
    }

    void Hardware::output_byte(int port, uint8_t x)
    {
        if (m_output_handler)
        {
            try
            {
                m_output_handler(*this, port, x);
            }
            catch (const std::exception& e)
            {
                BOOST_LOG_TRIVIAL(trace) << "Exception in user output handler: " << e.what();
            }
        }
    }

    void Hardware::copy_to_ram(const uint8_t* buffer, size_t count, uint16_t base)
    {
        if (!buffer || !count)
        {
            return;
        }

        // Limit the number of bytes so that we don't try to write past the end of RAM
        count = std::min(count, m_memory.size() - base);

        // TODO: This is exceedingly ugly, find a cleaner (but efficient!) solution
        std::memcpy(m_memory.data() + base, buffer, count);
    }

    void Hardware::copy_from_ram(uint8_t* buffer, size_t count, uint16_t base) const
    {
        if (!buffer || !count)
        {
            return;
        }

        // Limit the number of bytes so that we don't try to read past the end of RAM
        count = std::min(count, m_memory.size() - base);

        // TODO: This is exceedingly ugly, find a cleaner (but efficient!) solution
        std::memcpy(buffer, m_memory.data() + base, count);
    }

    void Hardware::dump(uint16_t base, size_t count) const
    {
        const size_t bytes_per_line = 16;
        size_t bytes_this_line = 0;
        std::string buf_address, buf_hex, buf_ascii;
        for (size_t i = 0; (i < count) && (base + i <= 0xFFFF); ++i)
        {
            if (bytes_this_line == 0)
            {
                buf_address = (boost::format("%04X:") % (base + i)).str();
                buf_hex.clear();
                buf_ascii.clear();
            }

            const auto byte = m_memory[base + i];
            buf_hex += (boost::format(" %02X") % static_cast<unsigned short>(byte)).str();
            const auto ch = ((byte >= ' ') && (byte <= 0x7E)) ? static_cast<char>(byte) : '.';
            buf_ascii += (boost::format("%c") % ch).str();

            ++bytes_this_line;

            if (bytes_this_line == bytes_per_line)
            {
                BOOST_LOG_TRIVIAL(trace) << buf_address << buf_hex << ' ' << buf_ascii;
                buf_address.clear();
                buf_hex.clear();
                buf_ascii.clear();
                bytes_this_line = 0;
            }
        }
        if (!buf_address.empty() && !buf_hex.empty())
        {
            BOOST_LOG_TRIVIAL(trace) << buf_address << buf_hex << ' ' << buf_ascii;
        }
    }

    void Hardware::check_memory_accesses(bool protect)
    {
        if (m_enable_memory_checks && (m_check_memory_accesses != protect))
        {
            BOOST_LOG_TRIVIAL(trace) << (protect ? "Enabling" : "Disabling") << " memory access checks";
            m_check_memory_accesses = protect;
        }
    }

    std::string Hardware::format_stack_info() const
    {
        // Ideally this method would simply iterate via SP back to user memory, but many programs manually set/restore
        // SP, so this needs to be bounded.

        // TODO: If the stack values are displayed as-is the user needs to keep in mind that they are the *return*
        // addresses, which is normally 3 bytes after the *source* address, and that can be confusing. It is tempting
        // to subtract 3 bytes from each value before display, but that also could be misleading.
        // So for now, this is a compile-time option that one day could/should be configurable.
        const bool use_source_value = true;

        const int max_steps = 4;

        std::stringstream ss;

        const uint16_t sp = m_processor->get_sp();

        auto user = false;
        auto startup = false;
        for (auto step = 0; !user && !startup && (step < max_steps); ++step)
        {
            uint16_t ret = read_word(sp + step * 2);
            if (use_source_value)
            {
                ret -= 3;
            }
            if ((ret >= 0x0100) && ret < m_fbase)
            {
                // This stack location is in user space, so stop the backtrace at this point
                user = true;
            }
            if (ret >= 0xFFF0)
            {
                // BDOS calls "manually" called on startup of ZCPM result in address such as these in the
                // stack track, showing the stack past these values is of no value
                startup = true;
            }
            ss << boost::format(" << %s") % describe_address(ret);
            if (use_source_value)
            {
                ss << "+3";
            }
        }

        return ss.str();
    }

    void Hardware::dump_symbol_table() const
    {
        m_symbols.dump();
    }

    std::tuple<bool, uint16_t> Hardware::evaluate_address_expression(const std::string& s) const
    {
        return m_symbols.evaluate_address_expression(s);
    }

    IDebuggable* Hardware::get_idebuggable() const
    {
        return m_processor.get();
    }

    void Hardware::check_watched_memory_byte(uint16_t address, Access mode, uint8_t value) const
    {
        if (!m_enable_memory_checks || !m_check_memory_accesses)
        {
            return;
        }

        // FIXME! This can be misleading; when we display PC, that can be the
        // wrong value because we're reading m_pc from the processor which "lags"
        // the actual PC. Can be tricky, and not so easy to fix (I've had a quick try already).

        if ((mode == Access::READ) && (m_watch_read.count(address)))
        {
            BOOST_LOG_TRIVIAL(trace) << boost::format("    %02X <- %s at PC=%s") % static_cast<unsigned short>(value) %
                                            describe_address(address) % describe_address(m_processor->get_pc());
        }
        if ((mode == Access::WRITE) && (m_watch_write.count(address)))
        {
            BOOST_LOG_TRIVIAL(trace) << boost::format("    %02X -> %s at PC=%s") % static_cast<unsigned short>(value) %
                                            describe_address(address) % describe_address(m_processor->get_pc());
            // TODO: If we detect an attempt to clobber very low addresses, panic. It's a sign of bad logic in our
            // system.
            // TODO: Try and find the root cause for this situation, and maybe handle this more gracefully?
            if (is_fatal_write(address))
            {
                throw std::runtime_error("Aborting: illegal memory write");
            }
        }
        if ((mode == Access::WRITE) && m_pbios && (m_pbios->is_bios(address)))
        {
            BOOST_LOG_TRIVIAL(trace) << boost::format("BIOS write to %s at PC=%s") % describe_address(address) %
                                            describe_address(m_processor->get_pc());
            throw std::runtime_error("BIOS tampering!");
        }
    }

    void Hardware::check_watched_memory_word(uint16_t address, Access mode, uint16_t value) const
    {
        if (!m_enable_memory_checks || !m_check_memory_accesses)
        {
            return;
        }

        // FIXME!  This can be misleading; when we display PC, that can be the wrong value because we're reading
        // m_pc from the processor which "lags" the actual PC.  Can be tricky, and not so easy to fix.

        if ((mode == Access::READ) && (m_watch_read.count(address + 0) || m_watch_read.count(address + 1)))
        {
            BOOST_LOG_TRIVIAL(trace) << boost::format("  %04X <- %s at PC=%s") % value % describe_address(address) %
                                            describe_address(m_processor->get_pc());
        }
        if ((mode == Access::WRITE) && (m_watch_write.count(address) || m_watch_write.count(address + 1)))
        {
            BOOST_LOG_TRIVIAL(trace) << boost::format("  %04X -> %s at PC=%s") % value % describe_address(address) %
                                            describe_address(m_processor->get_pc());
            if (is_fatal_write(address))
            {
                // We've detected an attempt to clobber very low addresses, panic. Either the program under test is
                // naughty, or we've got a logic bug.
                throw std::runtime_error("Aborting: illegal memory write");
            }
        }
        if ((mode == Access::WRITE) && m_pbios && (m_pbios->is_bios(address + 0) || m_pbios->is_bios(address + 1)))
        {
            BOOST_LOG_TRIVIAL(trace) << boost::format("BIOS write to %s at PC=%s") % describe_address(address) %
                                            describe_address(m_processor->get_pc());
            throw std::runtime_error("BIOS tampering!");
        }
    }

    std::string Hardware::describe_address(uint16_t a) const
    {
        auto result = (boost::format("%04X") % a).str();

        if (!m_symbols.empty())
        {
            result += std::string(" (") + m_symbols.describe(a) + std::string(")");
        }

        return result;
    }

} // namespace zcpm
