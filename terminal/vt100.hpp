#pragma once

#include <string>

#include "terminal.hpp"

namespace zcpm::terminal
{

    class Vt100 final : public Terminal
    {
    public:
        Vt100(int rows, int columns, const std::string& keymap_filename = "");
        ~Vt100();

        // Send a single character to the console; also handles tabs, start/stop
        // scroll, etc
        void print(char ch) override;

        // Send a series of characters to the console; these could possibly include
        // embedded escape sequences which are interpreted by the console
        void print(const std::string& s) override;

        // Reads a string from the user as per BDOS function 0x10.  'mx' is the maximum
        // number of characters to accept.  'initial' is any pre-filled content.
        std::string read_console_buffer(size_t mx, const std::string& initial) override;

        // Check to see if a character has been typed at the console
        [[nodiscard]] bool is_character_ready() const override;

        // Get a pending character (blocking read)
        char get_char() override;

        // Put a single character to the console, unfiltered
        void put_char(char ch) override;

    private:
        // A 'ncurses aware' character output, which does *not* do a refresh; that
        // is deliberately left to the caller
        void outch(char ch);

        // Check the 'pending' output characters; if we have sufficient info then handle them
        // and erase them.
        void process_pending();

        // When we get character-by-character output, it can include escape sequences.  We
        // save these incomplete sequences here until we have enough info to do something
        // with them.
        std::string m_pending;
    };

} // namespace zcpm::terminal
