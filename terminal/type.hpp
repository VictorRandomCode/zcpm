#pragma once

namespace ZCPM::Terminal
{
    enum class Type
    {
        PLAIN,    // Terminal type which relies on the host terminal doing any needed translation; usually supports ANSI
        VT100,    // Full-featured VT100 emulation translates CP/M VT100 directives to portable ncurses commands
        TELEVIDEO // Televideo 920/925
    };

    std::istream& operator>>(std::istream& in, Type& terminal);

} // namespace ZCPM::Terminal
