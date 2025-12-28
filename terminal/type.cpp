#include "type.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include <istream>

namespace zcpm::terminal
{
std::istream& operator>>(std::istream& in, Type& terminal)
{
    std::string token;
    in >> token;

    boost::to_upper(token);

    if (token == "PLAIN")
    {
        terminal = Type::PLAIN;
    }
    else if (token == "VT100")
    {
        terminal = Type::VT100;
    }
    else if (token == "TELEVIDEO")
    {
        terminal = Type::TELEVIDEO;
    }
    else
    {
        throw boost::program_options::validation_error(boost::program_options::validation_error::invalid_option_value, "Invalid terminal");
    }

    return in;
}

} // namespace zcpm::terminal
