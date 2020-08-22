#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

namespace ZCPM
{
  class IDebuggable;
  class Registers;
  class System;
} // namespace ZCPM

// A class which encapsulates the knowledge of how to display information for our machine, without having that
// information IN the machine; this will allow us to maintain those two concerns separately and will also allow us to
// (for example) have one Writer which uses DebugZ formatting and another which which uses DDT formatting.

// TODO: Extract an interface once this class settles down, and make sure the interface is used by end-users rather than
// this initial concrete implementation.  This particular initial implementation is based on DebugZ.

class Writer final
{
public:
  Writer(const ZCPM::IDebuggable* p_debuggable, ZCPM::IMemory& memory, std::ostream& os);

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
  void display(uint16_t address, const std::string& s1, const std::string& s2) const;

  // For displaying a line in an 'examine' output
  void display(const ZCPM::Registers& registers, const std::string& s1, const std::string& s2) const;

  std::string flags_to_string(uint8_t f) const;

  const ZCPM::IDebuggable* m_pdebuggable;
  ZCPM::IMemory& m_memory;
  std::ostream& m_os;
};
