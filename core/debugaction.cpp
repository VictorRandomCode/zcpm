#include "debugaction.hpp"

#include <boost/log/trivial.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <ostream>
#include <utility>

#include <fmt/core.h>

namespace zcpm
{

constexpr auto FACILITY = "DEBUG";

std::unique_ptr<DebugAction> DebugAction::create(Type type, std::uint16_t address, std::string_view location, const std::string& count)
{
    // This is only used for passpoint/watchpoint, but doing this for an empty string for breakpoint won't hurt
    const auto count_value = std::strtoul(count.c_str(), nullptr, 16);

    switch (type)
    {
    case Type::BREAKPOINT: BOOST_ASSERT(count.empty()); return std::make_unique<Breakpoint>(address, location);
    case Type::PASSPOINT: BOOST_ASSERT(!count.empty()); return std::make_unique<PassPoint>(address, location, count_value);
    case Type::WATCHPOINT: BOOST_ASSERT(count.empty()); return std::make_unique<Watchpoint>(address, location);
    default: return nullptr; // To keep gcc happy
    }
}

DebugAction::DebugAction(std::uint16_t address, std::string_view location) : m_location(location), m_address(address)
{
}

std::uint16_t DebugAction::get_address() const
{
    return m_address;
}

std::ostream& operator<<(std::ostream& os, const DebugAction& a)
{
    os << a.describe();
    return os;
}

Breakpoint::Breakpoint(std::uint16_t address, std::string_view location) : DebugAction(address, location)
{
}

bool Breakpoint::evaluate(std::uint16_t address) const
{
    if (m_address == address)
    {
        BOOST_LOG_TRIVIAL(trace) << fmt::format("{}: Breakpoint at {:04X}", FACILITY, address);
        std::cout << fmt::format("{}: Breakpoint at {:04X}", FACILITY, address) << std::endl;
        return false;
    }
    return true;
}

std::string Breakpoint::describe() const
{
    return fmt::format("Breakpoint at {:04X} (entered as '{}')", m_address, m_location);
}

Watchpoint::Watchpoint(std::uint16_t address, std::string_view location) : DebugAction(address, location)
{
}

bool Watchpoint::evaluate(std::uint16_t address) const
{
    if (m_address == address)
    {
        const auto message = fmt::format("{}: Watchpoint at {:04X}", FACILITY, address);
        BOOST_LOG_TRIVIAL(trace) << message;
        std::cout << message << std::endl;
    }
    return true; // Allow the debugger to keep running
}

std::string Watchpoint::describe() const
{
    return fmt::format("Watchpoint at {:04X} (entered as '{}')", m_address, m_location);
}

PassPoint::PassPoint(std::uint16_t address, std::string_view location, std::uint16_t initial)
    : DebugAction(address, location), m_remaining(initial)
{
}

bool PassPoint::evaluate(std::uint16_t address) const
{
    if (m_address == address)
    {
        // We've hit the passpoint; determine the correct action based on the remaining count
        if ((m_remaining == 0) || (--m_remaining == 0))
        {
            const auto message = fmt::format("{}: Passpoint at {:04X} expired, stopping", FACILITY, address);
            BOOST_LOG_TRIVIAL(trace) << message;
            std::cout << message << std::endl;
            return false;
        }
        BOOST_LOG_TRIVIAL(trace) << fmt::format("{}: Passpoint at {:04X} not yet expired", FACILITY, address);
    }

    // Carry on, don't stop the debugger
    return true;
}

std::string PassPoint::describe() const
{
    return fmt::format("Passpoint  at {:04X} (entered as '{}'), {:d} remaining", m_address, m_location, m_remaining);
}

} // namespace zcpm
