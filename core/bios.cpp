#include "bios.hpp"

#include "disk.hpp"
#include "hardware.hpp"
#include "processor.hpp"

#include <boost/log/trivial.hpp>

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

#include <zcpm/terminal/terminal.hpp>

// The constructor sets up various tables in RAM for the BIOS. This is the layout (from low address to high):
//
// BIOS jump table (m_discovered_base)
//   (then some unused space)
// Stubs base (m_stubs_base, which is m_discovered_base+100)
//   (access to stubs is checked at runtime to implement BIOS hooks)
// Stubs top (m_stubs_top)
// DPH (m_dph_base) one per disk in theory, but just one for now
//   DPB (one per disk flavour, but just one for now)
//   Scratchpads etc
// m_dph_top

namespace zcpm
{

Bios::Bios(Hardware* p_hardware, terminal::Terminal* p_terminal) : m_phardware(p_hardware), m_pterminal(p_terminal)
{
    const size_t table_size = 33; // As per "CP/M 3 System Guide", Table 2-1

    // Address 0 should be a JP, 1 and 2 are the destination. See "CP/M 3 System Guide", Table 2-3
    // That destination should be the warm boot (WBOOT) in the BIOS. WBOOT is the second jump vector
    // in the jump table, so subtract 3 bytes to get the actual start.
    const std::uint16_t base = m_phardware->read_byte(1) + (m_phardware->read_byte(2) << 8) - 3;
    m_discovered_base = base;

    // Sanity check; we expect to be modifying a jump table, i.e. a series of JP XXXX instructions.
    // Make sure that it looks that way before proceeding.
    if ((m_phardware->read_byte(base + 0) != 0xC3) || (m_phardware->read_byte(base + 3) != 0xC3))
    {
        throw std::runtime_error("BIOS jump table not found");
    }

    // Set up the BIOS stubs (which are just a series of RET instructions that are intercepted at runtime)
    // to be 0x0100 above the BIOS jump table. The aim is to leave enough space for stack above that, although
    // maybe stack should be elsewhere?
    m_stubs_base = base + 0x0100;

    BOOST_LOG_TRIVIAL(trace) << std::format("Rewriting BIOS jump table at {:04X}", base);

    // Write our own jump table over the top of whatever is there currently, making it point to another
    // one ("stubs") higher in memory, and that's the one that is intercepted (which should reduce the
    // risk of someone peeking the 'main' jump table and then jumping to the destination code).
    for (size_t i = 0; i < table_size; i++)
    {
        // Modify the JP so that it points to the corresponding stub entry
        m_phardware->write_word(base + i * 3 + 1, m_stubs_base + i);
    }

    // In the stubs, modify the destination of each JP to be a RET
    for (size_t i = 0; i < table_size; i++)
    {
        m_phardware->write_byte(m_stubs_base + i, 0xC9);
    }

    // Remember the top of the stubs area
    m_stubs_top = m_stubs_base + table_size - 1;

    // Write a known pattern into memory from the top of the BIOS jump table until the start
    // of the BIOS stubs area. (i.e., between the two regions)
    for (unsigned int i = m_discovered_base + table_size * 3; i < m_stubs_base; ++i)
    {
        m_phardware->write_byte(i, 0x00);
    }

    // Set up a single DPH. Normally BIOS would have a different variant for each different disk type (e.g.
    // one for a 8" floppy, one for a HDD, etc). But we're treating all "disks" the same, so we're basing
    // this single one on a typical HDD implementation. DPH is always 16 bytes by definition.
    m_dph_base = m_stubs_top + 1;
    const auto dirbf = m_dph_base + 0x10;
    const auto hdblk = dirbf + 0x80;
    const auto chkhd1 = hdblk + 0x10;  // we'll reserve some space for this, although none are actually used for HDD
    const auto allhd1 = chkhd1 + 0x10; // a 256 byte scratch area
    m_dph_top = allhd1 + 0x00FF;

    m_phardware->write_word(m_dph_base + 0x00, 0x0000); // XLT; set to zero because sector translation not needed
    m_phardware->write_word(m_dph_base + 0x02, 0x0000); // Scratchpad for use by BDOS
    m_phardware->write_word(m_dph_base + 0x04, 0x0000); // Scratchpad for use by BDOS
    m_phardware->write_word(m_dph_base + 0x06, 0x0000); // Scratchpad for use by BDOS
    m_phardware->write_word(m_dph_base + 0x08, dirbf);  // DIRBUF: 128-byte scratchpad for BDOS directory operations
    m_phardware->write_word(m_dph_base + 0x0A, hdblk);  // DPB: a parameter block describing a simulated HDD
    m_phardware->write_word(m_dph_base + 0x0C, chkhd1); // CSV: scratchpad for check for changed disks
    m_phardware->write_word(m_dph_base + 0x0E, allhd1); // ALV: scratchpad for disk storage allocation information

    // DIRBF is a 128 byte scratch area; leave it uninitialised

    // HDBLK is a table describing our simulated HDD
    m_phardware->write_word(hdblk + 0x00, 0x0080);    // SPT: Sectors per track
    m_phardware->write_byte(hdblk + 0x02, Disk::BSH); // BSH: Block shift factor
    m_phardware->write_byte(hdblk + 0x03, Disk::BLM); // BLM: Data allocation block mask
    m_phardware->write_byte(hdblk + 0x04, 0x00);      // EXM: Extent mask
    m_phardware->write_word(hdblk + 0x05, 0x07F7);    // DSM: Disk size in blocks - 1
    m_phardware->write_word(hdblk + 0x07, 0x03FF);    // DRM: Directory max
    m_phardware->write_byte(hdblk + 0x09, 0xFF);      // AL0: Alloc 0
    m_phardware->write_byte(hdblk + 0x0A, 0xFF);      // AL1: Alloc 1
    m_phardware->write_word(hdblk + 0x0B,
                            0x0000);               // CKS: Check size (For a HDD, can have 0. Removable media would need non-zero)
    m_phardware->write_word(hdblk + 0x0D, 0x0000); // OFF: Track offset

    // CHKHD1 is a scratch table for directory entries. But as our disk (a simulated HDD) is not
    // removable media, this can be zero bytes. Reserve two bytes for it just for neatness.
    // ALLHD1 is a scratch table for storage allocation. Seems to be 256 bytes; why?

    // Write a known pattern into memory from above the DPH stuff until top of 64K RAM
    for (unsigned int i = m_dph_top + 1; i <= 0xFFFF; ++i)
    {
        m_phardware->write_byte(i, 0x00);
    }

    BOOST_LOG_TRIVIAL(trace) << std::format("BIOS jump table {:04X}..{:04X}, BIOS stubs {:04X}..{:04X}, DPH etc {:04X}..{:04X}",
                                            m_discovered_base,
                                            m_discovered_base + table_size * 3 - 1,
                                            m_stubs_base,
                                            m_stubs_top,
                                            m_dph_base,
                                            m_dph_top);

    BOOST_LOG_TRIVIAL(trace) << std::format("     dirbf={:04X} hdblk={:04X} chkhd1={:04X} allhd1={:04X}", dirbf, hdblk, chkhd1, allhd1);

    // Set up monitoring of the DPH etc stuff
    m_phardware->add_watch_read(m_dph_base, m_dph_top - m_dph_base + 1);
    m_phardware->add_watch_write(m_dph_base, m_dph_top - m_dph_base + 1);

    // And add some pretend symbols to help make the run logs easier to read when something reads
    // or writes that data
    m_phardware->add_symbol(m_dph_base, "DPHBASE");
    m_phardware->add_symbol(dirbf, "DIRBF");
    m_phardware->add_symbol(hdblk, "HDBLK");
    m_phardware->add_symbol(allhd1, "ALLHD1");
    m_phardware->add_symbol(m_dph_top, "DPHTOP");
}

Bios::~Bios() = default;

bool Bios::is_bios(std::uint16_t address) const
{
    // Check for the whole range including the jump table (whose address is already determined by
    // the loaded binary memory image) as well as our set of targets of each of the jump vectors.
    // Technically there's a range of bytes between those two sets that is actually spare memory,
    // but that would be over-complicating things.
    return (address >= m_discovered_base) && (address <= m_stubs_top);
}

bool Bios::check_and_handle(std::uint16_t address)
{
    if ((address < m_stubs_base) || (address > m_stubs_top))
    {
        return false;
    }

    // The address is within the BIOS stubs. Work out what BIOS function is being called
    const unsigned int fn = address - m_stubs_base;

    const auto prefix(std::format("BIOS fn#{:d} ", fn));
    std::string msg;

    switch (fn)
    {
    case 0:
    {
        msg = "BOOT()";
        log_bios_call(prefix, msg);
        fn_boot();
    }
    break;
    case 1:
    {
        // WBOOT is called as part of initialisation, but if we get here it means that user
        // code is trying to (either directly or indirectly) call it again, which is used as
        // a termination condition.
        msg = "WBOOT()";
        log_bios_call(prefix, msg);
        m_phardware->set_finished(true);
    }
    break;
    case 2:
    {
        msg = "CONST()";
        log_bios_call(prefix, msg);
        // Return A=FF if a character is ready to be read, A=00 otherwise
        m_phardware->m_processor->reg_a() = m_pterminal->is_character_ready() ? 0xFF : 0x00;
    }
    break;
    case 3:
    {
        BOOST_LOG_TRIVIAL(trace) << prefix << "CONIN()";
        // Block until a character is ready, and then return it in A
        m_phardware->m_processor->reg_a() = m_pterminal->get_char();
        const auto ch = m_phardware->m_processor->get_a();
        msg = std::format("CONIN({:02X})", ch);
        log_bios_call(prefix, msg);
    }
    break;
    case 4:
    {
        const auto ch = static_cast<char>(m_phardware->m_processor->get_c());
        if (ch >= ' ')
        {
            msg = std::format("CONOUT({:02X} '{:c}')", ch, ch);
        }
        else
        {
            msg = std::format("CONOUT({:02X})", ch);
        }
        log_bios_call(prefix, msg);
        m_pterminal->print(ch);
    }
    break;
    case 8:
    {
        msg = "HOME()";
        log_bios_call(prefix, msg);
        fn_home();
    }
    break;
    case 9:
    {
        const auto disk = m_phardware->m_processor->get_c(); // Disk index; 0=A, 1=B, etc
        const auto flag = m_phardware->m_processor->get_e(); // Bit 0 is the "has been logged in before" flag
        msg = std::format("SELDSK(disk={:02X},flag={:02X})", disk, flag);
        log_bios_call(prefix, msg);
        fn_seldsk(disk, flag);
    }
    break;
    case 10:
    {
        const auto bc = m_phardware->m_processor->get_bc();
        msg = std::format("SETTRK({:04X})", bc);
        log_bios_call(prefix, msg);
        fn_settrk(bc);
    }
    break;
    case 11:
    {
        const auto bc = m_phardware->m_processor->get_bc();
        msg = std::format("SETSEC({:04X})", bc);
        log_bios_call(prefix, msg);
        fn_setsec(bc);
    }
    break;
    case 12:
    {
        const auto bc = m_phardware->m_processor->get_bc();
        msg = std::format("SETDMA({:04X})", bc);
        log_bios_call(prefix, msg);
        fn_setdma(bc);
    }
    break;
    case 13:
    {
        msg = "READ()";
        log_bios_call(prefix, msg);
        m_phardware->m_processor->reg_a() = fn_read();
    }
    break;
    case 14:
    {
        const auto c = m_phardware->m_processor->get_c();
        msg = std::format("WRITE({:02X})", c);
        log_bios_call(prefix, msg);
        m_phardware->m_processor->reg_a() = fn_write(c);
    }
    break;
    case 16:
    {
        const auto bc = m_phardware->m_processor->get_bc();
        const auto de = m_phardware->m_processor->get_de();
        msg = std::format("SECTRAN({:04X},{:04X})", bc, de);
        log_bios_call(prefix, msg);
        const auto physical_sector_number = fn_sectran(bc, de);
        m_phardware->m_processor->reg_hl() = physical_sector_number;
    }
    break;
    default:
    {
        msg = "Unknown!";
        log_bios_call(prefix, msg);
        throw std::logic_error("BIOS unfinished, FIXME!");
    }
    }

    // Typically we're returning to a 'RET' in the intercepted BIOS which will then
    // let the user code carry on without further ado.

    return true;
}

void Bios::fn_boot()
{
    // Based on 'Skeletal CBIOS' from 'CP/M 2.2 Operating System Manual'

    // Zero IOBYTE
    m_phardware->write_byte(0x0003, 0x00);

    // Zero CDISK
    m_phardware->write_byte(0x0004, 0x00);
}

void Bios::fn_wboot()
{
    // Based on 'Skeletal CBIOS' from 'CP/M 2.2 Operating System Manual'

    fn_seldsk(0, 0); // Select the first drive
    fn_home();       // Go to track 00

    // At this point in the sample BIOS, it loads CP/M CCP from disk and then jumps into CCP. So
    // this probably means that there's nothing else to do here in our implementation.
}

void Bios::fn_home()
{
    // Based on 'Skeletal CBIOS' from 'CP/M 2.2 Operating System Manual'

    fn_settrk(0);
}

void Bios::fn_seldsk(std::uint8_t /*disk*/, std::uint8_t /*flag*/)
{
    // See http://www.seasip.info/Cpm/bios.html#seldsk

    // Implementation based on SELDSK in ~/cpmsim/srccpm2/bios.asm, specifically for a HDD.
    // Return HL pointing to a DPH as per http://www.seasip.info/Cpm/dph.html
    // NOTE: Should return zero in HL if the specified disk does not exist, TODO!
    m_phardware->m_processor->reg_hl() = m_dph_base;
}

void Bios::fn_settrk(std::uint16_t track)
{
    // See http://www.seasip.info/Cpm/bios.html#settrk
    m_track = track;
}

void Bios::fn_setsec(std::uint16_t sector)
{
    // See http://www.seasip.info/Cpm/bios.html#setsec
    m_sector = sector;
}

void Bios::fn_setdma(std::uint16_t base)
{
    // See http://www.seasip.info/Cpm/bios.html#setdma
    m_dma = base;
}

std::uint8_t Bios::fn_read()
{
    // See http://www.seasip.info/Cpm/bios.html#read
    // Read the sector at m_track/m_sector into m_dma, return success status (which ends up in register A)

    BOOST_LOG_TRIVIAL(trace) << std::format("Read TRACK:{:04X},SECTOR:{:04X} into {:04X}", m_track, m_sector, m_dma);

    // Allocate a sector-size chunk of memory, into which data is read
    Disk::SectorData buffer{};
    // Read the specified disk sector into that memory chunk
    m_disk.read(buffer, m_track, m_sector);
    // And then copy the memory chunk into the emulated system's RAM
    m_phardware->copy_to_ram(buffer.data(), buffer.size(), m_dma);

    return 0;
}

std::uint8_t Bios::fn_write(std::uint8_t /*deblocking*/)
{
    // See http://www.seasip.info/Cpm/bios.html#write
    // Write from m_dma to the sector at m_track/m_sector, return success status (which ends up in register A)

    BOOST_LOG_TRIVIAL(trace) << std::format("Write TRACK:{:04X},SECTOR:{:04X} from {:04X}", m_track, m_sector, m_dma);

    // To help with debugging
    m_phardware->dump(m_dma, Disk::SectorSize);

    // Allocate a sector-size chunk of memory
    Disk::SectorData buffer{};

    // Copy from emulated RAM into that chunk
    m_phardware->copy_from_ram(buffer.data(), buffer.size(), m_dma);
    // Write from that chunk to the disk (this is double-handling but given that we're using this to
    // access disk which is much slower it won't be an issue, and it's all memcpy() underneath
    // anyway so it's not slow)
    m_disk.write(buffer, m_track, m_sector);

    return 0;
}

std::uint16_t Bios::fn_sectran(std::uint16_t logical_sector_number, std::uint16_t /*trans_table*/) const
{
    // Simplification: directly map a logical sector number to a physical sector number and
    // ignore the translation table. (This is what the BIOS would do if skewing was implemented
    // in hardware)
    return logical_sector_number;
}

void Bios::log_bios_call(std::string_view prefix, std::string_view message) const
{
    BOOST_LOG_TRIVIAL(trace) << "  " << prefix << message << m_phardware->format_stack_info();
}

} // namespace zcpm
