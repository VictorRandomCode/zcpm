#pragma once

#include <cstdint>

namespace zcpm
{

class IMemory
{
public:
    virtual ~IMemory() = default;

    // RAM access; both with and without timing information
    [[nodiscard]] virtual std::uint8_t read_byte(std::uint16_t address) const = 0;
    [[nodiscard]] virtual std::uint8_t read_byte(std::uint16_t address, size_t& elapsed_cycles) const = 0;
    [[nodiscard]] virtual std::uint16_t read_word(std::uint16_t address) const = 0;
    [[nodiscard]] virtual std::uint16_t read_word(std::uint16_t address, size_t& elapsed_cycles) const = 0;
    virtual void write_byte(std::uint16_t address, std::uint8_t x) = 0;
    virtual void write_byte(std::uint16_t address, std::uint8_t x, size_t& elapsed_cycles) = 0;
    virtual void write_word(std::uint16_t address, std::uint16_t x) = 0;
    virtual void write_word(std::uint16_t address, std::uint16_t x, size_t& elapsed_cycles) = 0;

    // Read a byte and increment the address to the next byte
    [[nodiscard]] virtual std::uint8_t read_byte_step(std::uint16_t& address, size_t& elapsed_cycles) const = 0;

    // Read a word and increment the address to the next word
    [[nodiscard]] virtual std::uint16_t read_word_step(std::uint16_t& address, size_t& elapsed_cycles) const = 0;

    virtual void push(std::uint16_t x, size_t& elapsed_cycles) = 0;
    virtual std::uint16_t pop(size_t& elapsed_cycles) = 0;

    // I/O access
    virtual std::uint8_t input_byte(int port) = 0;
    virtual void output_byte(int port, std::uint8_t x) = 0;

    // Bulk copies to/from emulated RAM
    virtual void copy_to_ram(const std::uint8_t* buffer, size_t count, std::uint16_t base) = 0;
    virtual void copy_from_ram(std::uint8_t* buffer, size_t count, std::uint16_t base) const = 0;

    // Used for inspecting a section of memory
    virtual void dump(std::uint16_t base, size_t count) const = 0;

    // Should we be checking for 'naughty' memory accesses currently?
    virtual void check_memory_accesses(bool protect) = 0;
};

} // namespace zcpm
