#pragma once

#include <cstdint>

namespace ZCPM
{

  // This data structure is returned when querying the hardware for
  // the current status, typically for displaying by the debugger
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
  };

} // namespace ZCPM
