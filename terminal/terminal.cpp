#include "terminal.hpp"

#include <ncurses.h>

namespace zcpm::terminal
{
    Terminal::Terminal(int rows, int columns, const std::string& keymap_filename)
        : m_keymap(keymap_filename), m_rows(rows), m_columns(columns)
    {
    }

    char Terminal::get_translated_char() const
    {
        // If we have yet to return all of a previous mapped (expanded?) key, return the next character
        if (!m_pending_keystrokes.empty())
        {
            const auto result = m_pending_keystrokes.front();
            m_pending_keystrokes.pop_front();
            return result;
        }

        // This method might be called after is_character_ready() which told the caller that
        // data is now ready to be returned, in which case we should read and return it immediately.
        // Or, it may be called without knowing if anything's ready, in which case we should
        // block until something is ready and then return that.  But either way, we're in "timeout"
        // mode currently which means we will quickly timeout if nothing is ready, so we need to
        // temporarily go back to blocking mode for this operation.  Yuk.

        ::timeout(-1);              // Temporarily back to blocking reads
        const int ch = ::getch();   // Read the character, blocking if needed
        ::timeout(KeyboardDelayMs); // And then back to 1ms timeout mode

        if (ch == 0x7F) // BACKSPACE/DELETE
        {
            // Backspace was pressed; Map a linux terminal 7F to a CP/M style 08
            return static_cast<char>(0x08);
        }
        if (ch == 0x0A)
        {
            // Enter was pressed, but that needs to be mapped to a CP/M style 0D
            return static_cast<char>(0x0D);
        }

        auto mapped_keys = m_keymap.translate(ch);
        const auto next = mapped_keys.front();
        mapped_keys.pop_front();
        m_pending_keystrokes = mapped_keys; // Might now be empty
        return static_cast<char>(next);
    }
} // namespace zcpm::terminal
