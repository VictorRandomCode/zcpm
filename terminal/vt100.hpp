#pragma once

#include "terminal.hpp"

#include <string>

namespace zcpm::terminal
{

class Vt100 final : public Terminal
{
public:
    Vt100(int rows, int columns, const std::string& keymap_filename = "");

    Vt100(const Vt100&) = delete;
    Vt100& operator=(const Vt100&) = delete;
    Vt100(Vt100&&) = delete;
    Vt100& operator=(Vt100&&) = delete;

    ~Vt100() override;

    // Send a single character to the console; also handles tabs, start/stop
    // scroll, etc
    void print(char ch) override;

    // Check to see if a character has been typed at the console
    [[nodiscard]] bool is_character_ready() const override;

    // Get a pending character (blocking read)
    char get_char() override;

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
