#pragma once

#include <cstdint>

namespace ZCPM
{

    // This data structure is returned when querying the hardware for the current status, typically for displaying by
    // the debugger or for more involved logging

    class Registers final
    {
    public:
        uint16_t AF;
        uint16_t BC;
        uint16_t DE;
        uint16_t HL;
        uint16_t IX;
        uint16_t IY;
        uint16_t SP;
        uint16_t PC;

        uint16_t altAF;
        uint16_t altBC;
        uint16_t altDE;
        uint16_t altHL;

        // Trivial helpers just to make use of the above a bit more readable
        static uint8_t lowbyte(uint16_t value)
        {
            return value & 0xFF;
        }
        static uint8_t highbyte(uint16_t value)
        {
            return (value >> 8) & 0xFF;
        }
    };

} // namespace ZCPM
