#include <iostream>
#include <string>
#include <vector>

#include <poll.h>
#include <termios.h>

#include "plain.hpp"

namespace ZCPM::Console
{

  Plain::~Plain()
  {
    // Make sure we don't leave an unfinished output line
    std::cout << std::endl;
  }

  void Plain::print(char ch)
  {
    std::cout << ch;
    std::cout.flush();
  }

  void Plain::print(const std::string& s)
  {
    std::cout << s;
    std::cout.flush();
  }

  std::string Plain::read_console_buffer(size_t mx, const std::string& /*initial*/)
  {
    // TODO: We don't yet use 'initial'; we'd probably need GNU ReadLine for that

    std::vector<char> bytes(mx + 2);
    std::cin.getline(bytes.data(), mx);
    std::string result(bytes.data());
    return result;
  }

  bool Plain::is_character_ready() const
  {
    struct pollfd fd[1] = { { 0, POLLIN, 0 } };
    return poll(fd, 1, 0) > 0;
  }

  char Plain::get_char()
  {
    // Temporarily disable both canonicalised input and echo on stdin
    struct termios original_flags;
    tcgetattr(fileno(stdin), &original_flags);
    auto modified_flags = original_flags;
    modified_flags.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(fileno(stdin), TCSANOW, &modified_flags);

    // Using the modified settings, read a single character
    auto ch = std::fgetc(stdin);

    // Restore original settings on stdin
    tcsetattr(fileno(stdin), TCSANOW, &original_flags);

    // Translate if needed
    if (ch == 0x7F) // BACKSPACE/DELETE
    {
      // Backspace was pressed; Map a linux terminal 7F to a CP/M style 08
      ch = 0x08;
    }
    else if (ch == 0x0A)
    {
      // Enter was pressed, but that needs to be mapped to a CP/M style 0D
      ch = 0x0D;
    }

    return static_cast<char>(ch);
  }

  void Plain::put_char(char ch)
  {
    std::cout << ch;
    std::cout.flush();
  }

} // namespace ZCPM::Console
