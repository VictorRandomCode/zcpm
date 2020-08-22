#include <regex>
#include <string>

#include <ncurses.h>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "cursesconsole.hpp"

namespace
{
  // Helper for parsing sequences such as "<ESC>[H" or "<ESC>[line;colH" or "<ESC>[countD".  If the supplied string
  // appears to be one of this form, puts the extracted line/col into the reference parameters as well as the
  // terminating character (one of H/r/f/A/B/C/D), and the return value is the length of the parsed sequence which
  // could now be erased.
  size_t parse_sequence_line_col(const std::string& s, int& line, int& col, char& ch)
  {
    const auto len = s.length();

    // Check for a minimum usable length
    if (len < 3)
    {
      // Can't be a complete sequence yet
      return 0;
    }

    // Make sure the sequence starts with "<ESC>["
    if (!s.starts_with("\x1B["))
    {
      // No match, immediate return
      return 0;
    }

    // Does it appear to end with one of the characters of interest?
    const auto last = s[len - 1];
    if (std::string("rHfABCD").find(last) == std::string::npos)
    {
      // It isn't something we yet recognise
      return 0;
    }

    // Looks good; extract what we need
    line = col = -1; // Until further notice
    ch = last;

    // Does there appear to be valid "line;col", and if so extract it
    if (len > 3)
    {
      const auto payload = s.substr(2, len - 3);

      // payload could be e.g. '3;14'.  Use a regular expression to extract what we need
      std::regex regex("(\\d+)");
      auto nbegin = std::sregex_iterator(payload.begin(), payload.end(), regex);
      auto nend = std::sregex_iterator();
      if (std::distance(nbegin, nend) == 2)
      {
        auto i = nbegin;
        const auto s1 = (*i++).str();
        const auto s2 = (*i++).str();
        line = std::atoi(s1.c_str()); // NOLINT(cert-err34-c)
        col = std::atoi(s2.c_str());  // NOLINT(cert-err34-c)
      }
    }

    return len;
  }
} // namespace

namespace ZCPM::Console
{

  Curses::Curses()
  {
    ::initscr();

    ::raw(); // Make sure that we receive control sequences (e.g. ^C) verbatim

    ::timeout(KeyboardDelayMs); // Enables a 1ms timeout on non-blocking keyboard read

    ::noecho(); // We want to display characters ourselves, not have them automatically echoed

    ::idlok(stdscr, true);    // Allow insert/delete row
    ::scrollok(stdscr, true); // Allow scrolling
  }

  Curses::~Curses()
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

  void Curses::print(char ch)
  {
    outch(ch);
    ::refresh();
  }

  void Curses::print(const std::string& s)
  {
    const auto len = s.size();
    for (size_t i = 0; i < len; i++)
    {
      outch(s[i]);
    }
    ::refresh();
  }

  std::string Curses::read_console_buffer(size_t mx, const std::string& initial)
  {
    // Remember the starting cursor position
    auto x = 0, y = 0;
    getsyx(y, x);

    // Display the initial data that the user edits/appends/etc
    std::string result(initial);
    ::addstr(result.c_str());

    // Set the cursor at the right place
    size_t len = result.size();

    // And start the editing loop
    int ch;
    bool editing = true;
    do
    {
      ch = ::getch();
      // TODO: Implement the various CP/M-style line editing things
      // as per the manual description of this function.
      if (ch == 0x7F) // BACKSPACE/DELETE
      {
        if (len == 0)
        {
          ::beep();
        }
        else
        {
          result.resize(--len);
          mvaddch(y, x + len, ' ');
          ::refresh();
          ::move(y, x + static_cast<int>(len));
        }
      }
      else if (ch == 0x0A) // RETURN pressed
      {
        editing = false;
      }
      else
      {
        if (len < mx)
        {
          result.push_back(ch);
          ++len;
          ::addch(ch);
          ::refresh();
        }
        else
        {
          ::beep();
        }
      }
    } while (editing);

    // CP/M seems to act as if the cursor is left at the beginning of the start line
    ::move(y, 0);
    ::refresh();

    return result;
  }

  bool Curses::is_character_ready() const
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

  char Curses::get_char()
  {
    // This method might be called after a is_character_ready() which told the caller that
    // data is now ready to be returned, in which case we should read and return it immediately.
    // Or, it may be called without knowing if anything's ready, in which case we should
    // block until something is ready and then return that.  But either way, we're in "timeout"
    // mode currently which means we will quickly timeout if nothing is ready, so we need to
    // temporarily go back to blocking mode for this operation.  Yuk.

    ::timeout(-1);              // Temporarily back to blocking reads
    int ch = ::getch();         // Read the character, blocking if needed
    ::timeout(KeyboardDelayMs); // And then back to 1ms timeout mode

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

  void Curses::put_char(char ch)
  {
    outch(ch);
    ::refresh();
  }

  void Curses::outch(char ch)
  {
    // If we've already got an escape sequence in progress, add this character to it.  process_pending()
    // will then consider the collected data, and if it can deal with it then it will do so
    // and erase the pending data.
    if (!m_pending.empty())
    {
      // If it appears that we're starting a *new* escape sequence with one already in progress, warn
      // the maintainer via the log file and drop the unhandled one.
      if (ch == '\033')
      {
        BOOST_LOG_TRIVIAL(trace) << "Warning: unimplemented escape sequence <ESC>" << m_pending.substr(1) << " dropped";
        m_pending.erase();
      }
      else
      {
        m_pending += std::string(1, ch);
        process_pending();
      }
      return;
    }

    if (ch == '\015') // CR
    {
      auto x = 0, y = 0;
      getsyx(y, x);
      ::move(y, 0);
    }
    else if (ch == '\012') // LF
    {
      // Normally, on a LF we simply go down one row.  But if we're already at the
      // bottom row we need to force a scroll in order to have the same behaviour
      // as a CP/M console.
      auto col = 0, row = 0;
      getsyx(row, col);

      auto maxrow = 0, maxcol = 0;
      getmaxyx(stdscr, maxrow, maxcol);

      if (row + 1 < maxrow)
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
    else // Anything else
    {
      // Make sure that a 7F is displayed as a space, for compatability with our reference system
      if (ch == 0x7F)
      {
        ch = ' ';
      }
      ::addch(ch);
    }
  }

  void Curses::process_pending()
  {
    // TODO: This is currently somewhat brute-force until I can flesh this out more so
    // that patterns become more apparent and then the approach can be rationalised.

    // For now, I'm mapping VT100 sequences (see http://ascii-table.com/ansi-escape-sequences-vt-100.php)
    // to ncurses commands.  I'm only adding VT100 sequences as-needed.

    int line, col;
    char ch;
    const auto nparsed = parse_sequence_line_col(m_pending, line, col, ch);
    if (nparsed > 0)
    {
      switch (ch)
      {
      case 'H':
      {
        if ((line >= 1) && (col >= 1))
        {
          BOOST_LOG_TRIVIAL(trace) << boost::format("CURSES cup (line=%d col=%d)") % line % col;
          ::move(line - 1, col - 1);
        }
        else
        {
          BOOST_LOG_TRIVIAL(trace) << "CURSES cursorhome";
          ::move(0, 0);
        }
      }
      break;
      case 'r':
        BOOST_LOG_TRIVIAL(trace) << "CURSES TODO1";
        // TODO (Set top and bottom lines of a window)
        break;
      case 'f':
        BOOST_LOG_TRIVIAL(trace) << "CURSES TODO2";
        // TODO (Move cursor to screen location); same as H???
        break;
      case 'D':
      {
        BOOST_LOG_TRIVIAL(trace) << "CURSES CUB  (line=" << line << ",col=" << col << ')';
        auto x = 0, y = 0;
        getsyx(y, x);
        ::move(y, x - 1);
      }
      break;
      default: BOOST_LOG_TRIVIAL(trace) << "Warning: Unimplemented escape sequence, TODO!"; break;
      }
      m_pending.erase(0, nparsed);
    }
    else if ((m_pending == "\x1B[J") || (m_pending == "\x1B[0J")) // "ED0" (clear screen from cursor down)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES ED0";
      ::clrtobot();
      m_pending.erase();
    }
    else if (m_pending == "\x1B[2J") // "ED2" (clear entire screen)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES ED2";
      // Note that ncurses ::clear() seems to home the cursor which is NOT what we want, so
      // we need to manually work around that
      auto x = 0, y = 0;
      getsyx(y, x);
      ::clear();
      ::move(y, x);
      m_pending.erase();
    }
    else if ((m_pending == "\x1B[K") || (m_pending == "\x1B[0K")) // "EL0" (clear line from cursor right)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES EL0";
      ::clrtoeol();
      m_pending.erase();
    }
    else if (m_pending == "\x1B[2K") // "EL2" (clear entire line)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES EL2";
      // There is no direct ncurses equivalent, so we need to do this in a few steps
      auto x = 0, y = 0;
      getsyx(y, x);
      ::move(y, 0);
      ::clrtoeol();
      ::move(y, x);
      m_pending.erase();
    }
    else if (m_pending == "\x1B[L") // WS.COM uses this; seems to be insert line
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES INSERTLINE";
      ::insertln();
      m_pending.erase();
    }
    else if (m_pending == "\x1B[M") // WS.COM uses this; seems to be delete line
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES DELETELINE";
      ::deleteln();
      m_pending.erase();
    }
    else if (m_pending == "\x1B[0m") // "SCR0" (turn off character attributes)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES SCR0";
      ::attrset(A_NORMAL);
      m_pending.erase();
    }
    else if (m_pending == "\x1B[1m") // "SGR1" (turn bold mode on)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES SGR1";
      ::attron(A_BOLD);
      m_pending.erase();
    }
    else if (m_pending == "\x1B[5m") // "SGR5" (turn blink mode on)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES SGR5";
      ::attron(A_BLINK);
      m_pending.erase();
    }
    else if (m_pending == "\x1B[7m") // "SGR7" (turn reverse video on)
    {
      BOOST_LOG_TRIVIAL(trace) << "CURSES SGR7";
      ::attron(A_REVERSE);
      m_pending.erase();
    }
  }

} // namespace ZCPM::Console
