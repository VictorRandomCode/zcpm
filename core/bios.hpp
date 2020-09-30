#pragma once

#include <cstdint>
#include <string>

#include "disk.hpp"

namespace ZCPM
{

  namespace Terminal
  {
    class Terminal;
  }
  class Hardware;

  class Bios final
  {
  public:
    Bios(Hardware* p_hardware, Terminal::Terminal* p_terminal);

    ~Bios();

    bool is_bios(uint16_t address) const;

    // Check if the specified address is within our custom BIOS implementation (and hence should be intercepted). If so,
    // work out what the intercepted BIOS call is trying to do and do whatever is needed, and then allows the caller to
    // return to normal processing.  Returns true if BIOS was intercepted.
    bool check_and_handle(uint16_t address);

    // BIOS function implementations
    void fn_boot();                                                                  // #00
    void fn_wboot();                                                                 // #01
    void fn_home();                                                                  // #08
    void fn_seldsk(uint8_t disk, uint8_t flag);                                      // #09
    void fn_settrk(uint16_t track);                                                  // #10
    void fn_setsec(uint16_t sector);                                                 // #11
    void fn_setdma(uint16_t base);                                                   // #12
    uint8_t fn_read();                                                               // #13
    uint8_t fn_write(uint8_t deblocking);                                            // #14
    uint16_t fn_sectran(uint16_t logical_sector_number, uint16_t trans_table) const; // #16

  private:
    void log_bios_call(const std::string& prefix, const std::string& message) const;

    Hardware* m_phardware;

    Terminal::Terminal* m_pterminal;

    Disk m_disk;

    // Where the current memory image has the start of the BIOS jump table
    // which we determine by inspecting the WBOOT vector.
    uint16_t m_discovered_base = 0; // Typically ends up being FC00

    // Calculated relative to discovered BIOS base, this is the start of the address
    // range where we point the set of BIOS vectors to jump to, and we intercept this
    // address range at runtime
    uint16_t m_stubs_base = 0; // Typically ends up being FD00

    // The highest address in the range of our BIOS "jump to" addresses
    uint16_t m_stubs_top = 0; // Typically ends up being FD62

    uint16_t m_dph_base = 0; // Base of the (single) DPH we create.
    // TODO: We currently use a single DPH which means that we treat everything as one big disk.  But
    // later on we may need to have multiple virtual disks, which means multiple DPH tables that all
    // point to the same DPB.  Needs more thought and/or experiments.
    uint16_t m_dph_top = 0; // Top address of DPH and related data structures

    uint16_t m_track = 0;    // Current track number on the current disk
    uint16_t m_sector = 0;   // Current sector number on the current disk
    uint16_t m_dma = 0x0080; // Address of the DMA buffer
  };

} // namespace ZCPM
