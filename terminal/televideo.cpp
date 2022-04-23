#include <string>

#include <ncurses.h>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "televideo.hpp"

namespace zcpm::terminal
{

    // Implementation notes on Televideo 920/925 within ZCPM
    // - Only sequences actually encountered are being implemented, no point going crazy
    // - Not yet worrying about "protected areas", e.g. a ^Z should be "Clear unprotected to insert character"
    // - Character addressing is 80x24; the docs use "from 1" counting, so valid row numbers are 1..24, etc.

    Televideo::Televideo(int rows, int columns, const std::string& keymap_filename)
        : Terminal(rows, columns, keymap_filename)
    {
        ::initscr();

        ::raw(); // Make sure that we receive control sequences (e.g. ^C) verbatim

        ::timeout(KeyboardDelayMs); // Enables a 1ms timeout on non-blocking keyboard read

        ::noecho(); // We want to display characters ourselves, not have them automatically echoed

        ::idlok(stdscr, true);    // Allow insert/delete row
        ::scrollok(stdscr, true); // Allow scrolling
        ::keypad(stdscr, true);   // Ask curses to give us e.g. KEY_LEFT instead of <ESC>[D
    }

    Televideo::~Televideo()
    {
        // If there's been no user *input* and we don't block here, the user will
        // not see anything displayed at all, so we need to do this before teardown.
        ::getch();

        ::endwin();

        if (!m_pending.empty())
        {
            BOOST_LOG_TRIVIAL(trace) << "Warning: incomplete escape sequence <ESC>" << m_pending.substr(1)
                                     << " at termination";
        }
    }

    void Televideo::print(char ch)
    {
        outch(ch);
        ::refresh();
    }

    bool Televideo::is_character_ready() const
    {
        // Is there a character available?
        const auto ch = ::getch();
        if (ch == ERR)
        {
            // No
            return false;
        }
        else
        {
            // Yes.  Make sure we don't lose it, so that :getch() returns it later as needed
            ::ungetch(ch);
            return true;
        }
    }

    char Televideo::get_char()
    {
        return get_translated_char();
    }

    void Televideo::outch(char ch)
    {
        // If we've already got an escape sequence in progress, add this character to it.  process_pending()
        // will then consider the collected data, and if it can deal with it then it will do so
        // and erase the pending data.
        if (!m_pending.empty())
        {
            // If it appears that we're starting a *new* escape sequence with one already in progress, warn
            // the maintainer via the log file and drop the unhandled one.
            if (ch == '\033') // ESC
            {
                BOOST_LOG_TRIVIAL(trace) << "Warning: unimplemented escape sequence '<ESC>" << m_pending.substr(1)
                                         << "' (" << m_pending.size() << " chars) dropped";
                m_pending.erase();
            }
            else
            {
                m_pending += std::string(1, ch);
                process_pending();
            }
            return;
        }

        auto col = 0, row = 0;
        getsyx(row, col);

        if (ch == '\015') // CR
        {
            ::move(row, 0);
        }
        else if (ch == '\012') // LF
        {
            // Normally, on a LF we simply go down one row.  But if we're already at the
            // bottom row we need to force a scroll in order to have the same behaviour
            // as a CP/M console.

            if (row + 1 < m_rows)
            {
                // Not yet at last row, so move down one row
                ::move(row + 1, col);
            }
            else
            {
                // Already at last row, so force a scroll
                ::scrl(1);
            }
        }
        else if (ch == '\010') // Control-H (Backspace?)
        {
            if (col > 0)
            {
                ::move(row, col - 1);
            }
            else if (row > 0)
            {
                ::move(row - 1, m_columns - 1);
            }
            else
            {
                // Unsure about this situation, needs experiments...
                ::move(0, 0);
            }
        }
        else if (ch == '\011') // Control-I (TAB?)
        {
            // TODO: Currently assuming standard 8 column tabs, this is too simplistic
            if (col < m_columns)
            {
                const auto new_column = ((col + 1) / 8) * 8;
                ::move(row, new_column);
            }
        }
        else if (ch == '\033') // ESC
        {
            m_pending += std::string(1, ch);
        }
        else if (ch == '\032') // Control-Z
        {
            BOOST_LOG_TRIVIAL(trace) << "CURSES clear all";
            ::clear();         // Note that this also homes the cursor
            ::attrset(A_BOLD); // Televideo uses half/full intensity, default is full
        }
        else if (ch == '\016') // Control-N
        {
            // "Protect mode off" (zcpm doesn't (yet) implement protected mode anyway)
        }
        else if (ch == '\007') // Control-G aka Bell
        {
            ::beep();
        }
        else // Anything else
        {
            if ((ch < ' ') || (ch > '~'))
            {
                BOOST_LOG_TRIVIAL(trace) << boost::format("Warning: unhandled CURSES %02X") %
                                                static_cast<unsigned short>(ch);
            }

            // Make sure that a 7F is displayed as a space, for compatibility with our reference system
            if (ch == 0x7F)
            {
                ch = ' ';
            }

            ::addch(ch);

            // If we were already at the maximum column, force a 'wrap' to the start of the next row so that the next
            // character output is in the right place. And if we're at the maximum row as well, force a scroll.
            if (col + 1 == m_columns)
            {
                col = 0;
                if (row + 1 < m_rows)
                {
                    ++row;
                }
                else
                {
                    ::scrl(1);
                }
                ::move(row, col);
            }
        }
    }

    void Televideo::process_pending()
    {
        // Map Televideo sequences (see
        // https://archive.org/details/bitsavers_televideo9deo925UsersGuideJan1983_5637627/page/n23/mode/2up) to ncurses
        // commands. Sequences are added only as needed, not all of them upfront.

        // Sanity checks
        BOOST_ASSERT(m_pending.size() > 1);
        BOOST_ASSERT(m_pending[0] == '\033');

        const auto& first = m_pending[1]; // First pending character *after* the ESC

        if ((first == ':') || (first == ';') || (first == '+') || (first == '*'))
        {
            // Refer Televideo doc at 4.9.2.4; zcpm treats all 4 flavours the same, although in theory there should be
            // subtle differences with the way that spaces/nulls/protected fields are handled.
            BOOST_LOG_TRIVIAL(trace) << "CURSES clear all";
            ::clear();         // Note that this also homes the cursor
            ::attrset(A_BOLD); // Televideo uses half/full intensity, default is full
            m_pending.erase();
            return;
        }
        if (first == 'T')
        {
            BOOST_LOG_TRIVIAL(trace) << "CURSES erase EOL with spaces";
            ::clrtoeol();
            m_pending.erase();
            return;
        }
        if (first == 'R')
        {
            BOOST_LOG_TRIVIAL(trace) << "CURSES line delete";
            ::deleteln();
            m_pending.erase();
            return;
        }
        if (first == 'E')
        {
            // According to 4.9.2.3 in the Televideo reference, this "inserts a line consisting of fill characters at
            // the cursor position. This causes the cursor to move to the start of the new line and all following lines
            // to move down one line"
            BOOST_LOG_TRIVIAL(trace) << "CURSES line insert";
            ::insertln();
            // Move to first column
            auto x = 0, y = 0;
            getsyx(y, x);
            ::move(y, 0);
            m_pending.erase();
            return;
        }
        if ((first == '=') && (m_pending.size() == 4))
        {
            // According to 4.5.1 in the Televideo reference, the row/col pair are offset by +31
            auto row = static_cast<int>(m_pending[2]);
            auto col = static_cast<int>(m_pending[3]);
            BOOST_ASSERT(row > 31);
            BOOST_ASSERT(col > 31);
            BOOST_LOG_TRIVIAL(trace) << boost::format("CURSES address (row=%d col=%d)") % (row - 31) % (col - 31);
            ::move(row - 32, col - 32);
            m_pending.erase();
            return;
        }
        if (first == '(')
        {
            // Half intensity off (which zcpm interprets as 'bold on')
            BOOST_LOG_TRIVIAL(trace) << "CURSES half intensity off";
            m_pending.erase();
            ::attron(A_BOLD);
            return;
        }
        if (first == ')')
        {
            // Half intensity on (which zcpm interprets as 'bold off')
            BOOST_LOG_TRIVIAL(trace) << "CURSES half intensity on";
            m_pending.erase();
            ::attroff(A_BOLD);
            return;
        }
        if (first == '>')
        {
            // Keyclick on [NOT IMPLEMENTED]
            BOOST_LOG_TRIVIAL(trace) << "CURSES keyclick on";
            m_pending.erase();
            return;
        }
        if (first == '<')
        {
            // Keyclick off [NOT IMPLEMENTED]
            BOOST_LOG_TRIVIAL(trace) << "CURSES keyclick off";
            m_pending.erase();
            return;
        }
        if ((first == 'j') || (m_pending == "\x1BG4"))
        {
            // End of reverse video
            BOOST_LOG_TRIVIAL(trace) << "CURSES reverse video";
            m_pending.erase();
            ::attron(A_REVERSE);
            return;
        }
        if ((first == 'k') || (m_pending == "\x1BG0"))
        {
            // End of reverse video
            BOOST_LOG_TRIVIAL(trace) << "CURSES reverse video end";
            m_pending.erase();
            ::attroff(A_REVERSE);
            return;
        }

        // If we get here, that means that we appear to have an incomplete pending escape sequence, and the intent is
        // that another character will get appended to the buffer before this method is called again.
    }

} // namespace zcpm::terminal
