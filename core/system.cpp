#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <zcpm/terminal/terminal.hpp>

#include "fcb.hpp"
#include "hardware.hpp"
#include "processor.hpp"
#include "system.hpp"

namespace zcpm
{

    System::System(std::unique_ptr<terminal::Terminal> p_terminal, const Config& behaviour)
        : m_hardware(std::move(p_terminal), behaviour)
    {
    }

    System::~System() = default;

    void System::setup_bios(uint16_t fbase, uint16_t wboot)
    {
        // Set up the key vectors
        m_hardware.set_fbase_and_wboot(fbase, wboot);

        // Call the BIOS 'BOOT' & 'WBOOT' functions so that the various BIOS data structures are initialised
        // before loading a user executable
        m_hardware.call_bios_boot();
    }

    void System::setup_bdos()
    {
        m_hardware.check_memory_accesses(false);

        // Call BDOS:RSTDSK so that disk data structures are initialised
        const auto rstdsk = 13;
        BOOST_LOG_TRIVIAL(trace) << "Directly calling BDOS fn#" << rstdsk;
        m_hardware.call_bdos(rstdsk);

        m_hardware.check_memory_accesses(true);
    }

    bool System::load_binary(uint16_t base, const std::string& filename)
    {
        std::FILE* fp = std::fopen(filename.c_str(), "r");
        if (!fp)
        {
            std::cerr << "Can't open '" << filename << "'" << std::endl;
            return false;
        }

        std::fseek(fp, 0, SEEK_END);
        const auto filesize = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);

        BOOST_LOG_TRIVIAL(trace) << "Reading " << filesize << " bytes into memory at " << boost::format("%04X") % base
                                 << " from " << filename;

        // Ideally we'd fread() directly into the memory buffer, but to do that means having more coupling
        // than is safe, so instead a temporary buffer is used here, which increases the peak RAM of this
        // process.
        std::vector<uint8_t> buf(filesize, 0);

        std::fread(buf.data(), 1, buf.size(), fp);

        m_hardware.copy_to_ram(buf.data(), buf.size(), base);

        std::fclose(fp);

        return true;
    }

    bool System::load_fcb(const std::vector<std::string>& args)
    {
        const uint16_t fcb_base = 0x005C; // Base of the FCB

        Fcb fcb;
        if (args.size() == 1)
        {
            fcb.set(args[0]);
        }
        else if (args.size() > 1)
        {
            // CP/M only deals with two arguments
            fcb.set(args[0], args[1]);
        }
        m_hardware.copy_to_ram(fcb.get(), fcb.size(), fcb_base);

        const uint16_t command_tail_base = 0x0080;
        // Set up the command tail at 0x0080; for each parameter there is a leading space
        // plus the parameter itself.  The parameters are stored as uppercase, and experiments
        // show that they are always followed by a single null byte (not sure if that
        // matters, but just in case...)
        std::string tail;
        for (const auto& s : args)
        {
            tail += " ";
            tail += boost::to_upper_copy(s);
        }
        m_hardware.write_byte(command_tail_base, tail.size());
        for (size_t i = 0; i < tail.size(); ++i)
        {
            m_hardware.write_byte(command_tail_base + 1 + i, tail[i]);
        }
        m_hardware.write_byte(command_tail_base + tail.size() + 1, 0x00);

        return true;
    }

    void System::reset()
    {
        m_hardware.reset();
        m_hardware.m_processor->reg_pc() = 0x0100; // Standard CP/M start address for loaded binaries

        // Set the stack such that a RET from the end of the loaded binary will cause a
        // jump to 00000 hence abort.

        // Note that the "CP/M 2.2 Operating System Manual" says that "upon entry to a transient
        // program, the CCP leaves the stack pointer set to an eight-level stack area with the CCP
        // return address pushed onto the stack, leaving seven levels before overflow occurs."

        const uint16_t sp = 0xF800; // Arbitrary free space
        m_hardware.m_processor->reg_sp() = sp;
        m_hardware.write_word(sp + 0, 0x0000);
        m_hardware.write_word(sp + 2, 0x0000);
        m_hardware.write_word(sp + 4, 0x0000);

        m_hardware.check_memory_accesses(true);
    }

    void System::step(size_t instruction_count)
    {
        m_hardware.set_finished(false);
        for (size_t i = 0; i < instruction_count; i++)
        {
            m_hardware.m_processor->emulate_instruction();
        }
    }

    void System::run()
    {
        m_hardware.set_finished(false);
        BOOST_LOG_TRIVIAL(trace) << "Starting execution of user code";
        m_hardware.m_processor->emulate();
    }

    void System::set_input_handler(const InputHandler& handler)
    {
        m_hardware.set_input_handler(handler);
    }

    void System::set_output_handler(const OutputHandler& handler)
    {
        m_hardware.set_output_handler(handler);
    }

} // namespace zcpm
