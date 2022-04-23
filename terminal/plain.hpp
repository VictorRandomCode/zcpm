#pragma once

#include <string>

#include "terminal.hpp"

namespace zcpm::terminal
{

    // This is a "pass-through" terminal emulation; it doesn't attempt to interpret any specific escape sequences, just
    // letting the host terminal program do the work.
    class Plain final : public Terminal
    {
    public:
        Plain(int rows, int columns);
        ~Plain();

        // Send a single character to the console; also handles tabs, start/stop
        // scroll, etc
        void print(char ch) override;

        // Check to see if a character has been typed at the console
        [[nodiscard]] bool is_character_ready() const override;

        // Get a pending character (blocking read)
        char get_char() override;
    };

} // namespace zcpm::terminal
