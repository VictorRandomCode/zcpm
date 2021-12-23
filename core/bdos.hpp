#pragma once

#include <string>
#include <tuple>

#include "imemory.hpp"
#include "registers.hpp"

namespace zcpm::bdos
{

    // Helper code for displaying BDOS information. Keep in mind that ZCPM does not *implement* BDOS code, just BIOS
    // code which the BDOS calls. ZCPM uses a drop-in binary blob for a "standard" BDOS implementation, and it then
    // intercepts calls to BIOS and reimplements those.

    // Returns <FunctionName,Description>
    std::tuple<std::string, std::string> describe_call(const Registers& registers, const IMemory& memory);

} // namespace zcpm::bdos
