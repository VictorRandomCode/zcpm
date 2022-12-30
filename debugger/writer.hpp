#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

namespace zcpm
{
    class IDebuggable;
    class IMemory;
    class Registers;
    class System;
} // namespace zcpm

// A class which encapsulates the knowledge of how to display information for our machine, without having that
// information IN the machine; this will allow us to maintain those two concerns separately and will also allow us to
// (for example) have one Writer which uses DebugZ formatting and another which uses DDT formatting.

// TODO: Extract an interface once this class settles down, and make sure the interface is used by end-users rather than
// this initial concrete implementation.  This particular initial implementation is based on DebugZ.

class Writer final
{
public:
    Writer(const zcpm::IDebuggable* p_debuggable, zcpm::IMemory& memory, std::ostream& os);

    // Display the current registers and the pending instruction
    void examine() const;

    // Display disassembly from PC onwards.
    // start < 0 means to use PC, otherwise to use that actual address value
    void list(int start, size_t instructions) const;

    // Display memory from PC onwards.
    // start < 0 means to use PC, otherwise to use that actual address value
    void dump(int start, size_t bytes) const;

private:
    // For displaying a line in a 'list' output
    void display(uint16_t address, std::string_view s1, std::string_view s2) const;

    // For displaying a line in an 'examine' output
    void display(const zcpm::Registers& registers,
                 std::string_view s1,
                 std::string_view s2,
                 const uint16_t offset = 0) const;

    std::string flags_to_string(uint8_t f) const;

    const zcpm::IDebuggable* m_pdebuggable;
    zcpm::IMemory& m_memory;
    std::ostream& m_os;
};
