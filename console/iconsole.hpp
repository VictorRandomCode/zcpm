#pragma once

#include <string>

namespace ZCPM::Console
{

  class IConsole
  {
  public:
    virtual ~IConsole() = default;

    // Send a single character to the console; also handles tabs, start/stop
    // scroll, etc
    virtual void print(char ch) = 0;

    // Send a series of characters to the console; these could possibly include
    // embedded escape sequences which are interpreted by the console
    virtual void print(const std::string& s) = 0;

    // Reads a string from the user as per BDOS function 0x10.  'mx' is the maximum
    // number of characters to accept.  'initial' is any pre-filled content.
    virtual std::string read_console_buffer(size_t mx, const std::string& initial) = 0;

    // Check to see if a character has been typed at the console
    [[nodiscard]] virtual bool is_character_ready() const = 0;

    // Get a pending character (blocking read)
    virtual char get_char() = 0;

    // Put a single character to the console, unfiltered
    virtual void put_char(char ch) = 0;

  private:
  };

} // namespace ZCPM::Console
