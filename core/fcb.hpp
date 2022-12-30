#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zcpm
{
    class IMemory;

    // Implements a CP/M FCB (File Control Block; see http://seasip.info/Cpm/fcb.html)
    class Fcb final
    {
    public:
        // Default constructor which mimics what a real CCP would initialise
        Fcb();

        // Construct from an existing in-memory instance
        Fcb(const IMemory& memory, uint16_t address);

        // Set content based on a single filename
        void set(std::string_view s1);

        // Set content based on a pair of filenames
        void set(std::string_view s1, std::string_view s2);

        // Return raw binary data that this FCB contains
        [[nodiscard]] const uint8_t* get() const;

        // Return the size of this FCB (which should always be the same!)
        [[nodiscard]] size_t size() const
        {
            return sizeof(U::m_bytes);
        }

        // Return a brief human-readable summary of this FCB for logging purposes. If show_both_filenames is not set
        // then just show the single filename, but if show_both_filenames is set then also show the filename at +10H etc
        std::string describe(bool show_both_filenames) const;

    private:
        void set_first(std::string_view s);
        void set_second(std::string_view s);

        // This class overlays a FCB instance in the system's memory.  The FCB is a 36 byte
        // structure, implemented as a fixed-size array with unions for the interesting
        // bits.  The details are from the "CP/M 2.0 Interface Guide", page 7 (for example).
        struct Fields
        {
            uint8_t m_dr;    // Drive code (0-16)
            uint8_t m_f[8];  // File name in ASCII uppercase, high bit = 0
            uint8_t m_t[3];  // File type in ASCII uppercase, high bit = 0
            uint8_t m_ex;    // Current extent number
            uint8_t m_s1;    // Reserved for internal system use
            uint8_t m_s2;    // Reserved for internal system use
            uint8_t m_rc;    // Record count
            uint8_t m_d[16]; // Reserved
            uint8_t m_cr;    // Current record for sequential operations
            uint8_t m_r[3];  // Random record number
        };
        union U
        {
            uint8_t m_bytes[sizeof(Fields)];
            Fields m_fields;
        };

        U m_u;
    };

} // namespace zcpm
