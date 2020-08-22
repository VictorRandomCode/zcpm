#pragma once

#include <cstdint>

namespace ZCPM
{

  class IMemory
  {
  public:
    virtual ~IMemory() = default;

    // RAM access; both with and without timing information
    [[nodiscard]] virtual uint8_t read_byte(uint16_t address) const = 0;
    [[nodiscard]] virtual uint8_t read_byte(uint16_t address, size_t& elapsed_cycles) const = 0;
    [[nodiscard]] virtual uint16_t read_word(uint16_t address) const = 0;
    [[nodiscard]] virtual uint16_t read_word(uint16_t address, size_t& elapsed_cycles) const = 0;
    virtual void write_byte(uint16_t address, uint8_t x) = 0;
    virtual void write_byte(uint16_t address, uint8_t x, size_t& elapsed_cycles) = 0;
    virtual void write_word(uint16_t address, uint16_t x) = 0;
    virtual void write_word(uint16_t address, uint16_t x, size_t& elapsed_cycles) = 0;

    // Read a byte and increment the address to the next byte
    [[nodiscard]] virtual uint8_t read_byte_step(uint16_t& address, size_t& elapsed_cycles) const = 0;

    // Read a word and increment the address to the next word
    [[nodiscard]] virtual uint16_t read_word_step(uint16_t& address, size_t& elapsed_cycles) const = 0;

    virtual void push(uint16_t x, size_t& elapsed_cycles) = 0;
    virtual uint16_t pop(size_t& elapsed_cycles) = 0;

    // I/O access
    virtual uint8_t input_byte(int port) = 0;
    virtual void output_byte(int port, uint8_t x) = 0;

    // Bulk copies to/from emulated RAM
    virtual void copy_to_ram(const uint8_t* buffer, size_t count, uint16_t base) = 0;
    virtual void copy_from_ram(uint8_t* buffer, size_t count, uint16_t base) const = 0;

    // Used for inspecting a section of memory
    virtual void dump(uint16_t base, size_t count) const = 0;

    // Should we be checking for 'naughty' memory accesses currently?
    virtual void check_memory_accesses(bool protect) = 0;
  };

} // namespace ZCPM
