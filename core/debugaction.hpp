#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

namespace zcpm
{

    class DebugAction
    {
    public:
        DebugAction(uint16_t address, std::string location);
        virtual ~DebugAction() = default;

        enum class Type
        {
            BREAKPOINT,
            PASSPOINT,
            WATCHPOINT,
        };

        // Factory method to instantiate a concrete subclass based on the supplied parameters
        static std::unique_ptr<DebugAction> create(Type type,
                                                   uint16_t address,
                                                   const std::string& location,
                                                   const std::string& count = "");

        [[nodiscard]] uint16_t get_address() const;

        // Called each time we reach a new address.  This method should return true
        // if the system should continue, false if it should break (returning to the
        // debugger prompt)
        [[nodiscard]] virtual bool evaluate(uint16_t address) const = 0;

        [[nodiscard]] virtual std::string describe() const = 0;

        friend std::ostream& operator<<(std::ostream& os, const DebugAction& a);

    protected:
        const std::string m_location; // Textual description of address being monitored, as entered by user
        const uint16_t m_address;     // Binary equivalent of the above
    };

    // Each time we reach the specified address, indicate that we should
    // return control to the debugger.
    class Breakpoint : public DebugAction
    {
    public:
        Breakpoint(uint16_t address, const std::string& location);
        [[nodiscard]] bool evaluate(uint16_t address) const override; // Will return false if we hit "our" address
        [[nodiscard]] std::string describe() const override;
    };

    // Always allows the debugger to keep running, but may cause some logging each time through
    class Watchpoint : public DebugAction
    {
    public:
        Watchpoint(uint16_t address, const std::string& location);
        [[nodiscard]] bool evaluate(uint16_t address) const override; // Will always return true
        [[nodiscard]] std::string describe() const override;
    };

    // While the 'remaining' value is still positive, has no effect, but once 'remaining'
    // hits zero it acts as a breakpoint
    class PassPoint : public DebugAction
    {
    public:
        PassPoint(uint16_t address, const std::string& location, uint16_t initial);
        [[nodiscard]] bool evaluate(uint16_t address) const override;
        [[nodiscard]] std::string describe() const override;

    private:
        mutable uint16_t m_remaining;
    };

} // namespace zcpm
