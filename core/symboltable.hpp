#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace ZCPM
{

    class SymbolTable final
    {
    public:
        // Load symbols from the specified label file, and store them in the specified namespace
        void load(const std::string& filename, const std::string& prefix);

        // Add one-off symbols
        void add(const std::string& prefix, uint16_t a, const std::string& label);

        // Do we have any entries?
        bool empty() const;

        // Using the symbol table content, return a string that describes the supplied address in
        // terms of known symbols.
        std::string describe(uint16_t a) const;

        // Try to evaluate an expression such as 'foo1' where 'foo1' is a known label
        // or perhaps 'foo2+23'.  Note that all values are hexadecimal.  Returns a
        // (success,value) pair, success=false means an evaluation failure.  Note that
        // this is not a full expression evaluator, it is VERY simplistic, sufficient
        // for our most common use cases.
        std::tuple<bool, uint16_t> evaluate_address_expression(const std::string& s) const;

        // Show contents to stdout
        void dump() const;

    private:
        std::tuple<bool, uint16_t> evaluate_symbol(const std::string& s) const;

        // Keyed by address, and the corresponding value is (namespace,label), e.g. ('BIOS','BLAH2')
        std::multimap<uint16_t, std::tuple<std::string, std::string>> m_symbols;
    };

} // namespace ZCPM
