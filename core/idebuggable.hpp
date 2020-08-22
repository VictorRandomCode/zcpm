#pragma once

#include <cstdint>
#include <memory>
#include <ostream>

namespace ZCPM
{

  class DebugAction;
  class Registers;

  class IDebuggable
  {
  public:
    virtual ~IDebuggable() = default;

    // Single getter for all registers in one step
    virtual void get_registers(Registers& registers) const = 0;

    // Get the next 4 bytes that pc+offset is pointing at
    virtual std::tuple<uint8_t, uint8_t, uint8_t, uint8_t> get_opcodes_at(uint16_t pc, uint16_t offset) const = 0;

    // Add a debug action (e.g. a breakpoint)
    virtual void add_action(std::unique_ptr<DebugAction> p_action) = 0;

    virtual void show_actions(std::ostream& os) const = 0;

    // Remove the specified action (index is 1..count), removes true if successful.
    // The index parameter is intended to correspond to that shown by show_actions()
    // so that the user can view actions via show_actions() and erase them via remove_action().
    virtual bool remove_action(size_t index) = 0;
  };

} // namespace ZCPM
