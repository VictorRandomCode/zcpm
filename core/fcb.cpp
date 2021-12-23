#include <stdexcept>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "fcb.hpp"

namespace
{
    // e.g. "b:foo.txt" will set dr=2 name="FOO     " extn="TXT"
    // e.g. "a.b" will set dr=0 name="A       " extn="B  "
    //
    // Returns <dr, name, extn>
    std::tuple<uint8_t, std::string, std::string> parse_filename(const std::string& s)
    {
        // TODO: Needs to be less simplistic; probably also need to create some unit tests for this logic

        // split into drive/file
        std::vector<std::string> fields;
        const auto input(boost::to_upper_copy(s));
        boost::split(fields, input, boost::is_any_of(":"));

        uint8_t dr = 0;

        std::string filename;
        if (fields.size() > 1)
        {
            dr = static_cast<uint8_t>(fields[0][0] - 'A' + 1);
            filename = fields[1];
        }
        else
        {
            filename = fields[0];
        }

        // split into filename/extension
        std::vector<std::string> parts;
        boost::split(parts, filename, boost::is_any_of("."));

        std::string name(8, ' ');
        std::string extn(3, ' ');

        if (!parts.empty())
        {
            for (size_t i = 0; (i < parts[0].size()) && (i < 8); i++)
            {
                name[i] = parts[0][i];
            }

            if (parts.size() > 1)
            {
                for (size_t i = 0; (i < parts[1].size()) && (i < 3); i++)
                {
                    extn[i] = parts[1][i];
                }
            }
        }
        return { dr, name, extn };
    }

} // namespace

namespace zcpm
{

    Fcb::Fcb() : m_u()
    {
        // Set up defaults based on what experiments on a 'real' implementation show
        size_t i = 0;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x02;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x20;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0x00;
        m_u.m_bytes[i++] = 0xFB;
        m_u.m_bytes[i++] = 0xB5;
        m_u.m_bytes[i++] = 0xFB;
    }

    Fcb::Fcb(const IMemory& memory, uint16_t address) : m_u()
    {
        for (unsigned int i = 0; i < size(); ++i)
        {
            m_u.m_bytes[i] = memory.read_byte(address + i);
        }
    }

    void Fcb::set(const std::string& s1)
    {
        set_first(s1);
    }

    void Fcb::set(const std::string& s1, const std::string& s2)
    {
        set_first(s1);
        set_second(s2);
    }

    const uint8_t* Fcb::get() const
    {
        return m_u.m_bytes;
    }

    std::string Fcb::describe(bool show_both_filenames) const
    {
        std::string name1;
        const auto& dr = m_u.m_fields.m_dr;
        if (dr)
        {
            name1 += char('A' + dr - 1);
            name1 += ':';
        }
        for (unsigned char ch : m_u.m_fields.m_f)
        {
            if (ch != ' ')
            {
                name1 += ch;
            }
        }
        name1 += '.';
        for (unsigned char ch : m_u.m_fields.m_t)
        {
            if (ch != ' ')
            {
                name1 += ch;
            }
        }

        std::string name2;
        if (show_both_filenames)
        {
            // Note that drive code for second filename is ignored, as per CP/M documentation for rename file
            for (unsigned int i = 1; i <= 8; ++i)
            {
                char ch = m_u.m_fields.m_d[i];
                if (ch != ' ')
                {
                    name2 += ch;
                }
            }
            name2 += '.';
            for (unsigned int i = 1; i <= 3; ++i)
            {
                char ch = m_u.m_fields.m_d[8 + i];
                if (ch != ' ')
                {
                    name2 += ch;
                }
            }
        }

        auto numbers =
            (boost::format("EX=%d RC=%d CR=%d R=%d/%d/%d") % static_cast<unsigned short>(m_u.m_fields.m_ex) %
             static_cast<unsigned short>(m_u.m_fields.m_rc) % static_cast<unsigned short>(m_u.m_fields.m_cr) %
             static_cast<unsigned short>(m_u.m_fields.m_r[0]) % static_cast<unsigned short>(m_u.m_fields.m_r[1]) %
             static_cast<unsigned short>(m_u.m_fields.m_r[2]))
                .str();

        if (show_both_filenames)
        {
            return (boost::format(R"("%s","%s" %s)") % name1 % name2 % numbers).str();
        }
        else
        {
            return (boost::format(R"("%s" %s)") % name1 % numbers).str();
        }
    }

    void Fcb::set_first(const std::string& s)
    {
        auto [dr, name, extn] = parse_filename(s);

        m_u.m_fields.m_dr = dr;

        for (size_t i = 0; i < name.size(); i++)
        {
            m_u.m_fields.m_f[i] = static_cast<uint8_t>(name[i]);
        }

        for (size_t i = 0; i < extn.size(); i++)
        {
            m_u.m_fields.m_t[i] = static_cast<uint8_t>(extn[i]);
        }
    }

    void Fcb::set_second(const std::string& s)
    {
        // This is a bit ugly.  According to the official docs we pretty much just overwrite
        // EX/S1/S2/etc with the second filename info.  So we'll do exactly that.

        auto [dr, name, extn] = parse_filename(s);

        for (size_t i = 0; i < name.size(); i++)
        {
            m_u.m_bytes[0x11 + i] = static_cast<uint8_t>(name[i]);
        }

        for (size_t i = 0; i < extn.size(); i++)
        {
            m_u.m_bytes[0x19 + i] = static_cast<uint8_t>(extn[i]);
        }
    }

} // namespace zcpm
