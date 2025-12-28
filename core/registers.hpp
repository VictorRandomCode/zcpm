#pragma once

#include <cstdint>

namespace zcpm
{

// This data structure is returned when querying the hardware for the current status, typically for displaying by
// the debugger or for more involved logging

class Registers final
{
public:
    std::uint16_t AF;
    std::uint16_t BC;
    std::uint16_t DE;
    std::uint16_t HL;
    std::uint16_t IX;
    std::uint16_t IY;
    std::uint16_t SP;
    std::uint16_t PC;

    std::uint16_t altAF;
    std::uint16_t altBC;
    std::uint16_t altDE;
    std::uint16_t altHL;

    // Trivial helpers just to make use of the above a bit more readable
    static std::uint8_t low_byte_of(std::uint16_t value)
    {
        return value & 0xFF;
    }
    static std::uint8_t high_byte_of(std::uint16_t value)
    {
        return (value >> 8) & 0xFF;
    }
};

} // namespace zcpm
