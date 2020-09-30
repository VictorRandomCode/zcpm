#pragma once

#include <string>

#include "terminal.hpp"

namespace ZCPM::Terminal
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
  };

} // namespace ZCPM::Terminal
