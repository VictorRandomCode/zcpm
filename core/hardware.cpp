#include "hardware.hpp"

#include "bdos.hpp"
#include "bios.hpp"
#include "processor.hpp"
#include "registers.hpp"

#include <cstring>
#include <format>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>
#include <zcpm/terminal/terminal.hpp>

namespace zcpm
{

Hardware::Hardware(std::unique_ptr<terminal::Terminal> p_terminal, const Config& behaviour)
    : m_processor(std::make_unique<Processor>(*this, *this)), m_config(behaviour), m_pterminal(std::move(p_terminal))
{
    m_memory.fill(0);

    // TODO: Add setters/getters etc for this kind of thing, rather than hiding it here

    // Monitor *any* write of page zero
    add_watch_write(0x0000, 0x0100);

    // Monitor reads in page zero, except for the BDOS or BIOS jump vectors
    add_watch_read(0x0003, 2);
    add_watch_read(0x0008, (0x0100 - 8));

    // Load the symbol table
    m_symbols.load(m_config.bdos_sym, "BDOS");
    m_symbols.load(m_config.user_sym, "USER");

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

void Hardware::set_fbase_and_wboot(std::uint16_t fbase, std::uint16_t wboot)
{
    m_fbase = fbase;

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

bool Hardware::check_and_handle_bdos_and_bios(std::uint16_t address) const
{
    // Note that BDOS calls are logged but not intercepted.  But BIOS calls are logged *and* intercepted. This is
    // because our BDOS is implemented via a binary blob (a real BDOS implementation), which will in turn make calls
    // into our own custom BIOS implementation. So BDOS calls are checked & logged but not intercepted, but BIOS
    // calls need to be intercepted and translated to (e.g.) host system calls.

    // Does this appear to be a BDOS call?  i.e., a jump to FBASE from 0005?
    if (address == m_fbase)
    {
        if (m_config.log_bdos)
        {
            // "Parse" the pending BDOS call into various bits of useful information
            const auto registers = m_processor->get_registers();
            const auto [bdos_name, description] = bdos::describe_call(registers, *this);

            // Log the information
            spdlog::info("BDOS: {}{}", bdos_name, format_stack_info());
            spdlog::info("BDOS: {}", description);
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

void Hardware::add_watch_read(std::uint16_t base, std::uint16_t count)
{
    for (auto i = base; i < base + count; ++i)
    {
        m_watch_read.insert(i);
    }
}

void Hardware::add_watch_write(std::uint16_t base, std::uint16_t count)
{
    for (auto i = base; i < base + count; ++i)
    {
        m_watch_write.insert(i);
    }
}

void Hardware::add_symbol(std::uint16_t a, std::string_view label)
{
    m_symbols.add("ZCPM", a, label);
}

std::uint8_t Hardware::read_byte(std::uint16_t address) const
{
    const auto result = m_memory[address];
    check_watched_memory_byte(address, Access::READ, result);
    return result;
}

std::uint8_t Hardware::read_byte(std::uint16_t address, size_t& elapsed_cycles) const
{
    const auto result = read_byte(address);
    elapsed_cycles += 3;
    return result;
}

std::uint16_t Hardware::read_word(std::uint16_t address) const
{
    const auto result_low = m_memory[address];
    const auto result_high = m_memory[(address + 1) & 0xffff];
    const auto result = result_low | (result_high << 8);
    check_watched_memory_word(address, Access::READ, result);
    return result;
}

std::uint16_t Hardware::read_word(std::uint16_t address, size_t& elapsed_cycles) const
{
    const auto result = read_word(address);
    elapsed_cycles += 6;
    return result;
}

void Hardware::write_byte(std::uint16_t address, std::uint8_t x)
{
    check_watched_memory_byte(address, Access::WRITE, x);
    m_memory[address] = x;
}

void Hardware::write_byte(std::uint16_t address, std::uint8_t x, size_t& elapsed_cycles)
{
    write_byte(address, x);
    elapsed_cycles += 3;
}

void Hardware::write_word(std::uint16_t address, std::uint16_t x)
{
    check_watched_memory_word(address, Access::WRITE, x);
    m_memory[address] = x;
    m_memory[(address + 1) & 0xffff] = x >> 8;
}

void Hardware::write_word(std::uint16_t address, std::uint16_t x, size_t& elapsed_cycles)
{
    write_word(address, x);
    elapsed_cycles += 6;
}

std::uint8_t Hardware::read_byte_step(std::uint16_t& address, size_t& elapsed_cycles) const
{
    const auto result = read_byte(address);
    address++;
    elapsed_cycles += 3;
    return result;
}

std::uint16_t Hardware::read_word_step(std::uint16_t& address, size_t& elapsed_cycles) const
{
    const auto result = read_word(address);
    address += 2;
    elapsed_cycles += 6;
    return result;
}

void Hardware::push(std::uint16_t x, size_t& elapsed_cycles)
{
    m_processor->reg_sp() -= 2;
    write_word(m_processor->reg_sp(), x, elapsed_cycles);
}

std::uint16_t Hardware::pop(size_t& elapsed_cycles)
{
    const auto result = read_word(m_processor->reg_sp(), elapsed_cycles);
    m_processor->reg_sp() += 2;
    return result;
}

std::uint8_t Hardware::input_byte(int port)
{
    if (m_input_handler)
    {
        try
        {
            return m_input_handler(*this, port);
        }
        catch (const std::exception& e)
        {
            spdlog::info("Exception in user input handler: {}", e.what());
            return 0;
        }
    }
    else
    {
        return 0;
    }
}

void Hardware::output_byte(int port, std::uint8_t x)
{
    if (m_output_handler)
    {
        try
        {
            m_output_handler(*this, port, x);
        }
        catch (const std::exception& e)
        {
            spdlog::info("Exception in user output handler: {}", e.what());
        }
    }
}

void Hardware::copy_to_ram(const std::uint8_t* buffer, size_t count, std::uint16_t base)
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

void Hardware::copy_from_ram(std::uint8_t* buffer, size_t count, std::uint16_t base) const
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

void Hardware::dump(std::uint16_t base, size_t count) const
{
    const size_t bytes_per_line = 16;
    size_t bytes_this_line = 0;
    std::string buf_address, buf_hex, buf_ascii;
    for (size_t i = 0; (i < count) && (base + i <= 0xFFFF); ++i)
    {
        if (bytes_this_line == 0)
        {
            buf_address = std::format("{:04X}", base + i);
            buf_hex.clear();
            buf_ascii.clear();
        }

        const auto byte = m_memory[base + i];
        buf_hex += std::format(" {:02X}", byte);
        const auto ch = ((byte >= ' ') && (byte <= 0x7E)) ? static_cast<char>(byte) : '.';
        buf_ascii += ch;

        ++bytes_this_line;

        if (bytes_this_line == bytes_per_line)
        {
            spdlog::info("{}{}", buf_address, buf_hex);
            buf_address.clear();
            buf_hex.clear();
            buf_ascii.clear();
            bytes_this_line = 0;
        }
    }
    if (!buf_address.empty() && !buf_hex.empty())
    {
        spdlog::info("{}{} {}", buf_address, buf_hex, buf_ascii);
    }
}

void Hardware::check_memory_accesses(bool protect)
{
    if (m_config.memcheck && (m_check_memory_accesses != protect))
    {
        spdlog::info("{} memory access checks", protect ? "Enabling" : "Disabling");
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

    const std::uint16_t sp = m_processor->get_sp();

    auto user = false;
    auto startup = false;
    for (auto step = 0; !user && !startup && (step < max_steps); ++step)
    {
        std::uint16_t ret = read_word(sp + step * 2);
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
        ss << " << " << describe_address(ret);
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

std::tuple<bool, std::uint16_t> Hardware::evaluate_address_expression(std::string_view s) const
{
    return m_symbols.evaluate_address_expression(s);
}

IDebuggable* Hardware::get_idebuggable() const
{
    return m_processor.get();
}

void Hardware::check_watched_memory_byte(std::uint16_t address, Access mode, std::uint8_t value) const
{
    if (!m_config.memcheck || !m_check_memory_accesses)
    {
        return;
    }

    // FIXME! This can be misleading; when we display PC, that can be the
    // wrong value because we're reading m_pc from the processor which "lags"
    // the actual PC. Can be tricky, and not so easy to fix (I've had a quick try already).

    if ((mode == Access::READ) && (m_watch_read.count(address)))
    {
        spdlog::info("    {:02X} <- {} at PC={}", value, describe_address(address), describe_address(m_processor->get_pc()));
    }
    if ((mode == Access::WRITE) && (m_watch_write.count(address)))
    {
        spdlog::info("    {:02X} -> {} at PC={}", value, describe_address(address), describe_address(m_processor->get_pc()));
        if (is_fatal_write(address))
        {
            throw std::runtime_error("Aborting: illegal memory write");
        }
    }
    if ((mode == Access::WRITE) && m_pbios && (m_pbios->is_bios(address)))
    {
        spdlog::info("BIOS write to {} at PC={}", describe_address(address), describe_address(m_processor->get_pc()));
        throw std::runtime_error("BIOS tampering!");
    }
}

void Hardware::check_watched_memory_word(std::uint16_t address, Access mode, std::uint16_t value) const
{
    if (!m_config.memcheck || !m_check_memory_accesses)
    {
        return;
    }

    // FIXME!  This can be misleading; when we display PC, that can be the wrong value because we're reading
    // m_pc from the processor which "lags" the actual PC.  Can be tricky, and not so easy to fix.

    if ((mode == Access::READ) && (m_watch_read.count(address + 0) || m_watch_read.count(address + 1)))
    {
        spdlog::info("  {:04X} <- {} at PC={}", value, describe_address(address), describe_address(m_processor->get_pc()));
    }
    if ((mode == Access::WRITE) && (m_watch_write.count(address) || m_watch_write.count(address + 1)))
    {
        spdlog::info("  {:04X} -> {} at PC={}", value, describe_address(address), describe_address(m_processor->get_pc()));
        if (is_fatal_write(address))
        {
            // Detected an attempt to clobber very low addresses.
            throw std::runtime_error("Aborting: illegal memory write");
        }
    }
    if ((mode == Access::WRITE) && m_pbios && (m_pbios->is_bios(address + 0) || m_pbios->is_bios(address + 1)))
    {
        spdlog::info("BIOS write to {} at PC={}", describe_address(address), describe_address(m_processor->get_pc()));
        throw std::runtime_error("BIOS tampering!");
    }
}

std::string Hardware::describe_address(std::uint16_t a) const
{
    auto result = std::format("{:04X}", a);

    if (!m_symbols.empty())
    {
        result += std::string(" (") + m_symbols.describe(a) + std::string(")");
    }

    return result;
}

bool Hardware::is_fatal_write(std::uint16_t address) const
{
    // A few programs will try to modify the warm start vector (at locations 0000,0001,0002)
    // to hook in their own intercepts. For example Supercalc.
    if ((address <= 0x0002) && m_config.protect_warm_start_vector)
    {
        // Warm start vector
        return true;
    }

    // CP/M debuggers will intercept the BDOS jump vector, which means that they'll
    // try to modify address 0006 & 0007.  But normal programs won't.
    if (((address >= 0x0005) && (address <= 0x0007)) && m_config.protect_bdos_jump)
    {
        // BDOS jump vector
        return true;
    }

    // Everything else is ok
    return false;
}
} // namespace zcpm
