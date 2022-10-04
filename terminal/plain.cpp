#include "plain.hpp"

#include <iostream>
#include <poll.h>
#include <string>
#include <termios.h>
#include <vector>

namespace zcpm::terminal
{

    Plain::Plain(int rows, int columns) : Terminal(rows, columns)
    {
    }

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

} // namespace zcpm::terminal
