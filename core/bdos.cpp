#include "bdos.hpp"

#include "fcb.hpp"
#include "imemory.hpp"
#include "registers.hpp"

#include <fmt/core.h>

namespace zcpm::bdos
{

    namespace
    {

        // Given a CP/M-style string terminated by '$' at addr in memory, return a version of it suitable for logging
        std::string cpm_string_at(const IMemory& memory, uint16_t address)
        {
            // Build the result, but limit the length in case the CP/M code is trying to print something particularly
            // long or is pointing at rubbish (which isn't our problem, but we don't want logging of that to break us!).
            // This function also escapes any non-printables so as not to mess up the logfile.
            std::string result;
            for (auto offset = 0; offset < 30; ++offset)
            {
                auto ch = static_cast<char>(memory.read_byte(address + offset));
                if (ch == '$')
                {
                    return result;
                }
                if (std::isprint(ch))
                {
                    result += ch;
                }
                else
                {
                    result += fmt::format("<{:02X}>", ch);
                }
            }

            return result + " (etc)";
        }

        // Given a FCB, return a simple string that describes it, suitable for debug logs
        std::string describe_fcb(const IMemory& memory, uint16_t address, bool both = false)
        {
            const Fcb fcb(memory, address);
            return fmt::format("FCB at {:04X}: {}", address, fcb.describe(both));
        }

    } // namespace

    std::tuple<std::string, std::string> describe_call(const Registers& registers, const IMemory& memory)
    {
        // Register C indicates *which* BDOS call is being made
        const auto c = Registers::low_byte_of(registers.BC);

        const auto prefix = fmt::format("fn#{:d} ", c);

        // TODO: make the 'description' field be smarter; show actual filenames or register values etc.
        switch (c)
        {
        case 0: return { prefix + "P_TERMCPM", "System reset" };
        case 1: return { prefix + "C_READ", "Console input" };
        case 2:
        {
            auto ch = static_cast<char>(Registers::low_byte_of(registers.DE));
            auto description = fmt::format("Console output '{:c}' (ASCII 0x{:02X})", (std::isprint(ch) ? ch : '?'), ch);
            return { prefix + "C_WRITE", description };
        }
        case 6: return { prefix + "C_RAWIO", "Direct console I/O" };
        case 9:
        {
            auto payload = cpm_string_at(memory, registers.DE);
            auto description = "Print string \"" + payload + '\"';
            return { prefix + "C_WRITESTR", description };
        }
        case 10:
        {
            auto& buffer = registers.DE;
            auto max = memory.read_byte(buffer);
            auto description = fmt::format("Read console buffer (buffer at {:04X}, {:d} bytes max)", buffer, max);
            return { prefix + "C_READSTR", description };
        }
        case 11: return { prefix + "C_STAT", "Get console status" };
        case 12: return { prefix + "S_BDOSVER", "Return version number" };
        case 13: return { prefix + "DRV_ALLRESET", "Reset disk system" };
        case 14: return { prefix + "DRV_SET", "Select disk" };
        case 15:
        {
            auto description = "Open file (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_OPEN", description };
        }
        case 16:
        {
            auto description = "Close file (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_CLOSE", description };
        }
        case 17:
        {
            auto description = "Search for first (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_SFIRST", description };
        }
        case 18:
        {
            auto description = "Search for next (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_SNEXT", description };
        }
        case 19:
        {
            auto description = "Delete file (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_DELETE", description };
        }
        case 20:
        {
            auto description = "Read sequential (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_READ", description };
        }
        case 21:
        {
            auto description = "Write sequential (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_WRITE", description };
        }
        case 22:
        {
            auto description = "Make file (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_MAKE", description };
        }
        case 23:
        {
            auto description = "Rename file (" + describe_fcb(memory, registers.DE, true) + ")";
            return { prefix + "F_RENAME", description };
        }
        case 24: return { prefix + "DRV_LOGINVEC", "Return login vector" };
        case 25: return { prefix + "DRV_GET", "Return current disk" };
        case 26:
        {
            auto description = fmt::format("Set DMA address to {:04X}", registers.DE);
            return { prefix + "F_DMAOFF", description };
        }
        case 27: return { prefix + "DRV_ALLOCVEC", "Get addr(alloc)" };
        case 29: return { prefix + "DRV_ROVEC", "Get readonly vector" };
        case 30:
        {
            auto description = "Set file attributes (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_ATTRIB", description };
        }
        case 31: return { prefix + "DRV_DPB", "Get addr(diskparams)" };
        case 32:
        {
            auto e = Registers::low_byte_of(registers.DE);
            auto description = fmt::format("(E={:02X} means '{}')", e, ((e == 0xFF) ? "get" : "set"));
            return { prefix + "F_USERNUM", "Set/get user code " + description };
        }
        case 33:
        {
            auto description = "Read random (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_READRAND", description };
        }
        case 34:
        {
            auto description = "Write random (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_WRITERAND", description };
        }
        case 35:
        {
            auto description = "Compute file size (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_SIZE", description };
        }
        case 36:
        {
            auto description = "Set random record (" + describe_fcb(memory, registers.DE) + ")";
            return { prefix + "F_RANDREC", description };
        }
        default: return { prefix + "???", "" };
        }
    }

} // namespace zcpm::bdos
