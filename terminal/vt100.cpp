#include "vt100.hpp"

#include <ncurses.h>
#include <optional>
#include <regex>
#include <string>

#include <spdlog/spdlog.h>

namespace
{
// Parses an escape sequence (which may yet be incomplete) and returns <count, values, ch> where:
// - count is the number of characters parsed that can now be erased,
// - values is a vector of semicolon-separated numbers
// - ch is the terminating character.
// So given "<ESC>[0;4;5m" count=8 values=[0,4,5] ch='m'
// Or       "<ESC>[m" count=3 values=[] ch='m'
// Returns nullopt if nothing can (yet) be parsed
using ParsedSequence = std::tuple<size_t, std::vector<int>, char>;
std::optional<ParsedSequence> parse_sequence(const std::string& s)
{
    const auto len = s.length();

    // Check for a minimum usable length and that it starts with "<ESC>["
    if ((len < 3) || !s.starts_with("\x1B["))
    {
        // Can't be a complete sequence yet
        return std::nullopt;
    }

    const auto last = s[len - 1];
    const auto terminators = std::string("rHfABCDmJKLM");

    // Check for a valid sequence with *no* numeric values (common occurrence, so optimise for this path)
    if ((len == 3) && terminators.find(last) != std::string::npos)
    {
        // Empty value list
        return ParsedSequence{ len, {}, last };
    }

    if (terminators.find(last) == std::string::npos)
    {
        // It isn't something we yet recognise, most like a sequence which hasn't yet been fully collated
        return std::nullopt;
    }

    // Is all the intervening content digits or semicolons only?
    if (s.find_first_not_of("0123456789;", 3) != len - 1)
    {
        spdlog::warn("not just a numeric sequence in '{}'", s);
        return std::nullopt;
    }

    // Looks good; extract what we need

    std::vector<int> values;

    // payload could be e.g. '3;14'.  Use a regular expression to extract what we need
    const auto payload = s.substr(2, len - 3);

    std::regex regex("(\\d+)");
    for (auto i = std::sregex_iterator(payload.begin(), payload.end(), regex); i != std::sregex_iterator(); ++i)
    {
        const auto match = (*i).str();
        const auto value = std::atoi(match.c_str()); // NOLINT(cert-err34-c)
        values.push_back(value);
    }

    return ParsedSequence{ len, values, last };
}

// VT100 sequences as per http://ascii-table.com/ansi-escape-sequences-vt-100.php

// Move cursor left n lines
void ansi_cub()
{
    spdlog::info("CURSES CUB");
    auto x = 0, y = 0;
    getsyx(y, x);
    ::move(y, x - 1);
}

// Move cursor to screen location v,h
void ansi_cup(int v, int h)
{
    spdlog::info("CURSES cup (v={:d} h={:d})", v, h);
    ::move(v - 1, h - 1);
}

// Clear screen from cursor down
void ansi_ed0()
{
    spdlog::info("CURSES ED0");
    ::clrtobot();
}

// Clear entire screen
void ansi_ed2()
{
    spdlog::info("CURSES ED2");
    // Note that ncurses ::clear() seems to home the cursor which is NOT what we want, so we need to manually work
    // around that
    auto x = 0, y = 0;
    getsyx(y, x);
    ::clear();
    ::move(y, x);
}

// Clear line from cursor right
void ansi_el0()
{
    spdlog::info("CURSES EL0");
    ::clrtoeol();
}

// Clear entire line
void ansi_el2()
{
    spdlog::info("CURSES EL2");
    // There is no direct ncurses equivalent, so we need to do this in a few steps
    auto x = 0, y = 0;
    getsyx(y, x);
    ::move(y, 0);
    ::clrtoeol();
    ::move(y, x);
}

// Turn off character attributes
void ansi_sgr0()
{
    spdlog::info("CURSES SGR0");
    ::attrset(A_NORMAL);
}

// Turn bold mode on
void ansi_sgr1()
{
    spdlog::info("CURSES SGR1");
    ::attron(A_BOLD);
}

// Turn blinking mode on
void ansi_sgr5()
{
    spdlog::info("CURSES SGR5");
    ::attron(A_BLINK);
}

// Turn reverse video on
void ansi_sgr7()
{
    spdlog::info("CURSES SGR7");
    ::attron(A_REVERSE);
}

// Set alternate keypad mode
void ansi_deckpam()
{
    spdlog::info("CURSES DECKPAM");
    // Not implemented
}

// Enter/exit ANSI mode (VT52)
void ansi_setansi()
{
    spdlog::info("CURSES setansi");
    // Not implemented
}

} // namespace

namespace zcpm::terminal
{

Vt100::Vt100(int rows, int columns, const std::string& keymap_filename) : Terminal(rows, columns, keymap_filename)
{
    ::initscr();

    ::raw(); // Make sure that we receive control sequences (e.g. ^C) verbatim

    ::timeout(KeyboardDelayMs); // Enables a 1ms timeout on non-blocking keyboard read

    ::noecho(); // We want to display characters ourselves, not have them automatically echoed

    ::idlok(stdscr, true);    // Allow insert/delete row
    ::scrollok(stdscr, true); // Allow scrolling
    ::keypad(stdscr, true);   // Ask curses to give us e.g. KEY_LEFT instead of <ESC>[D
}

Vt100::~Vt100()
{
    // If there's been no user *input* and we don't block here, the user will
    // not see anything displayed at all, so we need to do this before teardown.
    ::getch();

    ::endwin();

    if (!m_pending.empty())
    {
        spdlog::warn("incomplete escape sequence <ESC>{} at termination", m_pending.substr(1));
    }
}

void Vt100::print(char ch)
{
    outch(ch);
    ::refresh();
}

bool Vt100::is_character_ready() const
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

char Vt100::get_char()
{
    return get_translated_char();
}

void Vt100::outch(char ch)
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
            spdlog::warn("unimplemented escape sequence <ESC>{} dropped", m_pending.substr(1));
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
    else if (ch == '\033') // ESC
    {
        m_pending += std::string(1, ch);
    }
    else if (ch == '\007') // Control-G aka Bell
    {
        ::beep();
    }
    else // Anything else
    {
        // Make sure that a 7F is displayed as a space, for compatibility with our reference system
        if (ch == 0x7F)
        {
            ch = ' ';
        }

        ::addch(ch);

        // If we were already at the maximum column, force a 'wrap' to the start of the next row so that the next
        // character output is in the right place
        if (col + 1 == m_columns)
        {
            col = 0;
            if (row + 1 < m_rows)
            {
                // TODO: Unsure of correct behaviour here; simply stick with the bottom row, or should we scroll?
                ++row;
            }
            ::move(row, col);
        }
    }
}

void Vt100::process_pending()
{
    // Map VT100 sequences (see http://ascii-table.com/ansi-escape-sequences-vt-100.php)
    // to ncurses commands. Sequences are added only as needed, not all of them upfront.

    if (auto parsed = parse_sequence(m_pending); parsed) // Handle e.g. "<ESC>[fooH" here
    {
        const auto [num_parsed, values, ch] = *parsed;

        switch (ch)
        {
        case 'D': ansi_cub(); break;

        case 'H':
        {
            if (values.size() == 2)
            {
                ansi_cup(values[0], values[1]);
            }
            else if (values.empty())
            {
                spdlog::info("CURSES cursorhome");
                ::move(0, 0);
            }
            else
            {
                spdlog::warn("'H' has {}", values.size());
            }
        }
        break;

        case 'J':
        {
            if (values.empty())
            {
                ansi_ed0();
            }
            else
            {
                if (values.size() > 1)
                {
                    spdlog::warn("Unexpected value count");
                }
                switch (values[0])
                {
                case 0: ansi_ed0(); break;
                case 2: ansi_ed2(); break;
                default: spdlog::warn("n={} unhandled for EDn", values[0]);
                }
            }
        }
        break;

        case 'K':
        {
            if (values.empty())
            {
                ansi_el0();
            }
            else
            {
                if (values.size() > 1)
                {
                    spdlog::warn("Unexpected value count");
                }
                switch (values[0])
                {
                case 0: ansi_el0(); break;
                case 2: ansi_el2(); break;
                default: spdlog::warn("n={} unhandled for ELn", values[0]);
                }
            }
        }
        break;

        case 'L': // WS.COM uses this; seems to be insert line
            spdlog::info("CURSES INSERTLINE");
            ::insertln();
            break;

        case 'M': // WS.COM uses this; seems to be delete line
            spdlog::info("CURSES DELETELINE");
            ::deleteln();
            break;

        case 'f':
            spdlog::info("CURSES TODO2");
            // TODO (Move cursor to screen location); same as H???
            break;

        case 'm':
        {
            if (values.empty())
            {
                ansi_sgr0();
            }
            else
            {
                for (const auto value : values)
                {
                    switch (value)
                    {
                    case 0: ansi_sgr0(); break;
                    case 1: ansi_sgr1(); break;
                    case 5: ansi_sgr5(); break;
                    case 7: ansi_sgr7(); break;
                    default: spdlog::warn("n={} unhandled for SGRn", value);
                    }
                }
            }
        }
        break;

        case 'r':
            spdlog::info("CURSES TODO1");
            // TODO (Set top and bottom lines of a window)
            break;

        default: spdlog::warn("Unimplemented escape sequence, TODO!"); break;
        }
        m_pending.erase(0, num_parsed);
    }
    else if (m_pending == "\x1B=")
    {
        ansi_deckpam();
        m_pending.erase();
    }
    else if (m_pending == "\x1B<")
    {
        ansi_setansi();
        m_pending.erase();
    }
}

} // namespace zcpm::terminal
