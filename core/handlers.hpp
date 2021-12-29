#pragma once

#include <cstdint>
#include <functional>

namespace zcpm
{

    class Hardware;

    /// For the Z80 'IN' instruction
    using InputHandler = std::function<uint8_t(Hardware&, int)>;

    /// For the Z80 'OUT' instruction
    using OutputHandler = std::function<void(Hardware&, int, uint8_t)>;

} // namespace zcpm
