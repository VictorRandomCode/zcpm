#pragma once

#include <string>

#include "keymap.hpp"

namespace zcpm::terminal
{

    class Terminal
    {
    public:
        Terminal(int rows, int columns, const std::string& keymap_filename = "");

        virtual ~Terminal() = default;

        // Send a single character to the console; also handles tabs, start/stop
        // scroll, etc
        virtual void print(char ch) = 0;

        // Reads a string from the user as per BDOS function 0x10.  'mx' is the maximum
        // number of characters to accept.  'initial' is any pre-filled content.
        virtual std::string read_console_buffer(size_t mx, const std::string& initial) = 0;

        // Check to see if a character has been typed at the console
        [[nodiscard]] virtual bool is_character_ready() const = 0;

        // Get a pending character (blocking read)
        virtual char get_char() = 0;

        // Get a pending character (blocking read) via a keymap
        virtual char get_translated_char() const;

    protected:
        const Keymap m_keymap;
        const int m_rows;
        const int m_columns;

        const int KeyboardDelayMs = 1;

    private:
        // Keystrokes that are yet to be returned after a mapping
        mutable std::list<char> m_pending_keystrokes;
    };

} // namespace zcpm::terminal
