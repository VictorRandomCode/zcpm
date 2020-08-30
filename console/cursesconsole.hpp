#pragma once

#include <string>

#include "iconsole.hpp"

namespace ZCPM::Console
{

  class Curses final : public IConsole
  {
  public:
    Curses();
    ~Curses();

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

    // VT100 sequences as per http://ascii-table.com/ansi-escape-sequences-vt-100.php
    void ansi_cub() const;             // Move cursor left n lines
    void ansi_cup(int v, int h) const; // Move cursor to screen location v,h
    void ansi_ed0() const;             // Clear screen from cursor down
    void ansi_ed2() const;             // Clear entire screen
    void ansi_el0() const;             // Clear line from cursor right
    void ansi_el2() const;             // Clear entire line
    void ansi_sgr0() const;            // Turn off character attributes
    void ansi_sgr1() const;            // Turn bold mode on
    void ansi_sgr5() const;            // Turn blinking mode on
    void ansi_sgr7() const;            // Turn reverse video on

    static const int KeyboardDelayMs = 1;

    // When we get character-by-character output, it can include escape sequences.  We
    // save these incomplete sequences here until we have enough info to do something
    // with them.
    std::string m_pending;
  };

} // namespace ZCPM::Console
