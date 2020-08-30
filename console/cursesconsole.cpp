#include <regex>
#include <string>

#include <ncurses.h>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "cursesconsole.hpp"

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
      BOOST_LOG_TRIVIAL(trace) << "Warning: not just a numeric sequence in '" << s << "'";
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
      // Make sure that a 7F is displayed as a space, for compatibility with our reference system
      if (ch == 0x7F)
      {
        ch = ' ';
      }
      ::addch(ch);
    }
  }

  void Curses::process_pending()
  {
    // Map VT100 sequences (see http://ascii-table.com/ansi-escape-sequences-vt-100.php)
    // to ncurses commands. Sequences are added only as needed, not all of them upfront.

    if (auto parsed = parse_sequence(m_pending); parsed)
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
          BOOST_LOG_TRIVIAL(trace) << "CURSES cursorhome";
          ::move(0, 0);
        }
        else
        {
          BOOST_LOG_TRIVIAL(trace) << "Warning: 'H' has " << values.size();
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
            BOOST_LOG_TRIVIAL(trace) << "Warning: Unexpected value count";
          }
          switch (values[0])
          {
          case 0: ansi_ed0(); break;
          case 2: ansi_ed2(); break;
          default: BOOST_LOG_TRIVIAL(trace) << "Warning: n=" << values[0] << " unhandled for EDn";
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
            BOOST_LOG_TRIVIAL(trace) << "Warning: Unexpected value count";
          }
          switch (values[0])
          {
          case 0: ansi_el0(); break;
          case 2: ansi_el2(); break;
          default: BOOST_LOG_TRIVIAL(trace) << "Warning: n=" << values[0] << " unhandled for ELn";
          }
        }
      }
      break;

      case 'L': // WS.COM uses this; seems to be insert line
        BOOST_LOG_TRIVIAL(trace) << "CURSES INSERTLINE";
        ::insertln();
        break;

      case 'M': // WS.COM uses this; seems to be delete line
        BOOST_LOG_TRIVIAL(trace) << "CURSES DELETELINE";
        ::deleteln();
        break;

      case 'f':
        BOOST_LOG_TRIVIAL(trace) << "CURSES TODO2";
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
            default: BOOST_LOG_TRIVIAL(trace) << "Warning: n=" << value << " unhandled for SGRn";
            }
          }
        }
      }
      break;

      case 'r':
        BOOST_LOG_TRIVIAL(trace) << "CURSES TODO1";
        // TODO (Set top and bottom lines of a window)
        break;

      default: BOOST_LOG_TRIVIAL(trace) << "Warning: Unimplemented escape sequence, TODO!"; break;
      }
      m_pending.erase(0, num_parsed);
    }
  }

  void Curses::ansi_cub() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES CUB";
    auto x = 0, y = 0;
    getsyx(y, x);
    ::move(y, x - 1);
  }

  void Curses::ansi_cup(int v, int h) const
  {
    BOOST_LOG_TRIVIAL(trace) << boost::format("CURSES cup (v=%d h=%d)") % v % h;
    ::move(v - 1, h - 1);
  }

  void Curses::ansi_ed0() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES ED0";
    ::clrtobot();
  }

  void Curses::ansi_ed2() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES ED2";
    // Note that ncurses ::clear() seems to home the cursor which is NOT what we want, so we need to manually work
    // around that
    auto x = 0, y = 0;
    getsyx(y, x);
    ::clear();
    ::move(y, x);
  }

  void Curses::ansi_el0() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES EL0";
    ::clrtoeol();
  }

  void Curses::ansi_el2() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES EL2";
    // There is no direct ncurses equivalent, so we need to do this in a few steps
    auto x = 0, y = 0;
    getsyx(y, x);
    ::move(y, 0);
    ::clrtoeol();
    ::move(y, x);
  }

  void Curses::ansi_sgr0() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES SGR0";
    ::attrset(A_NORMAL);
  }

  void Curses::ansi_sgr1() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES SGR1";
    ::attron(A_BOLD);
  }

  void Curses::ansi_sgr5() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES SGR5";
    ::attron(A_BLINK);
  }

  void Curses::ansi_sgr7() const
  {
    BOOST_LOG_TRIVIAL(trace) << "CURSES SGR7";
    ::attron(A_REVERSE);
  }

} // namespace ZCPM::Console
