#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ZCPM
{

  class Fcb final
  {
  public:
    Fcb();

    void set(const std::vector<std::string>& s);

    [[nodiscard]] const uint8_t* get() const;
    [[nodiscard]] size_t size() const
    {
      return sizeof(U::m_bytes);
    }

  private:
    void set_first(const std::string& s);
    void set_second(const std::string& s);

    // This class overlays a FCB instance in the system's memory.  The FCB is a 36 byte
    // structure, which we implement as a fixed-size array with unions for the interesting
    // bits.  The details are from the "CP/M 2.0 Interface Guide", page 7 (for example).
    struct Fields
    {
      uint8_t m_dr;   // Drive code (0-16)
      uint8_t m_f[8]; // File name in ASCII uppercase, high bit = 0
      uint8_t m_t[3]; // File type in ASCII uppercase, high bit = 0
      uint8_t m_ex;   // Current extent number
      uint8_t m_s1;   // Reserved for internal system use
      uint8_t m_s2;   // Reserved for internal system use
      uint8_t m_rc;   // Record count
      uint8_t m_d[8]; // Reserved
      uint8_t m_cr;   // Current record for sequential operations
      uint8_t m_r[3]; // Random record number
    };
    union U
    {
      uint8_t m_bytes[36];
      Fields m_fields;
    };

    U m_u;
  };

} // namespace ZCPM
