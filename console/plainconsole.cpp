#include <iostream>
#include <string>
#include <vector>

#include "plainconsole.hpp"

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
    // TODO
    return false;
  }

  char Plain::get_char()
  {
    // TODO
    return ' ';
  }

  void Plain::put_char(char ch)
  {
    std::cout << ch;
    std::cout.flush();
  }

} // namespace ZCPM::Console
