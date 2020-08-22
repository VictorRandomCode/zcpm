#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "symboltable.hpp"

namespace ZCPM
{

  void SymbolTable::load(const std::string& filename, const std::string& prefix)
  {
    if (filename.empty())
    {
      return;
    }

    std::ifstream file(filename);
    if (!file.is_open())
    {
      throw std::runtime_error("Can't open " + filename);
    }
    std::string s;
    while (std::getline(file, s))
    {
      // We assume each line is in the format 'FOO: equ $1234'
      // So we assume that anything to the left of the colon is the label,
      // and anything to the right of the $ is the value in hex.
      const auto colon = s.find_first_of(':');
      const auto dollar = s.find_last_of('$');

      if ((colon != std::string::npos) && (dollar != std::string::npos) && (colon < dollar))
      {
        const auto label = std::string(s, 0, colon);
        const auto hvalue = std::string(s, dollar + 1, std::string::npos);
        if (!label.empty() || (!hvalue.empty()))
        {
          const auto key = std::strtol(hvalue.c_str(), nullptr, 16);
          add(prefix, key, label);
        }
      }
    }
  }

  void SymbolTable::add(const std::string& prefix, uint16_t a, const std::string& label)
  {
    m_symbols.insert({ a, { prefix, label } });
  }

  bool SymbolTable::empty() const
  {
    return m_symbols.empty();
  }

  std::string SymbolTable::describe(uint16_t a) const
  {
    // We rely on the map being keyed by address of each symbol, and the keys are in ascending order.
    // So we search backwards until we reach the address of interest (or the closest one just before),
    // and use that symbol as the return string.

    for (auto it = m_symbols.rbegin(); it != m_symbols.rend(); it++)
    {
      if (it->first <= a)
      {
        const auto offset = a - it->first;
        return (boost::format("%s:%s+%04X") % std::get<0>(it->second) % std::get<1>(it->second) % offset).str();
      }
    }

    return "?";
  }

  std::tuple<bool, uint16_t> SymbolTable::evaluate_address_expression(const std::string& s) const
  {
    // The supplied string could be something like "foo1+17a" where 'foo1' is in the symbol table
    // and 17a is a hex offset from that symbol.  Or it could be "foo2" where we use the unmodified
    // value of the 'foo2' symbol.

    // So we'll use this regex: ([A-Za-z0-9]+)(?:([+-])([A-Fa-f0-9]+))?
    // Given 'lab1+17a', group1 is 'lab1' and group2 is '+' and group3 is '17a'
    // Given 'lab1', group1 is 'lab1' other groups are empty

    const std::regex expression_regex("([A-Za-z0-9]+)(?:([+-])([A-Fa-f0-9]+))?");
    std::smatch expression_match;
    if (!std::regex_search(s, expression_match, expression_regex))
    {
      BOOST_LOG_TRIVIAL(trace) << "Can't parse '" << s << "' (1)";
      return { false, 0 };
    }

    // From the first regex match, try to make sense of the base
    const auto& base_string(expression_match[1].str());
    const auto [base_ok, base] = evaluate_symbol(base_string);
    if (!base_ok)
    {
      BOOST_LOG_TRIVIAL(trace) << "Can't parse base in '" << s << "'";
      return { false, 0 };
    }

    // From the second and third regex matches (if present), get the operator and offset
    const auto& opr(expression_match[2].str());
    const auto& offset_string(expression_match[3].str());

    // Do we have an operator & offset?  (these are optional)
    if ((opr.empty() && !offset_string.empty()) || (!opr.empty() && offset_string.empty()))
    {
      BOOST_LOG_TRIVIAL(trace) << "Can't parse '" << s << "' (2)";
      return { false, 0 };
    }

    if (opr.empty() || offset_string.empty())
    {
      // At least one of the operator or offset are missing, so treat this as just a base value.
      // Which means we're done, can now return the base as the result.
      return { true, base };
    }

    // We have an operator and offset.  Try to convert the offset into a number

    const auto [offset_ok, offset] = evaluate_symbol(offset_string);
    if (!offset_ok)
    {
      BOOST_LOG_TRIVIAL(trace) << "Can't parse offset in '" << s << "'";
      return { false, 0 };
    }

    // We now have the base and the offset, apply the operator and return
    if (opr == "+")
    {
      return { true, base + offset };
    }
    else if (opr == "-")
    {
      return { true, base - offset };
    }
    else
    {
      BOOST_LOG_TRIVIAL(trace) << "Unknown operator in '" << s << "'";
      return { false, 0 };
    }
  }

  void SymbolTable::dump() const
  {
    std::cout << m_symbols.size() << " entries in symbol table:" << std::endl;
    for (const auto& [key, value] : m_symbols)
    {
      const auto& [ns, name] = value;
      std::cout << boost::format("  %04X %s:%s") % key % ns % name << std::endl;
    }
  }

  std::tuple<bool, uint16_t> SymbolTable::evaluate_symbol(const std::string& s) const
  {
    // First see if it is a known symbol.  This involves a brute-force map search, yuk.
    const auto us = boost::to_upper_copy(s);
    for (const auto& [key, value] : m_symbols)
    {
      if (boost::to_upper_copy(std::get<1>(value)) == us)
      {
        // Found it, return a success indication with the result being the symbol value
        return { true, key };
      }
    }

    // Not a symbol, hopefully it's a valid hex string
    char* endptr = nullptr;
    const auto value = std::strtoul(s.c_str(), &endptr, 16);
    if (*endptr == '\0')
    {
      // Looks to be a complete valid hex string, good!
      return { true, value };
    }
    else
    {
      // Some or part of the string was not valid hex, bad.
      return { false, 0 };
    }
  }

} // namespace ZCPM
