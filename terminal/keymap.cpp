#include "keymap.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <fstream>
#include <ncurses.h>
#include <optional>
#include <string>

namespace zcpm
{
    namespace
    {
        // Convert a ncurses key name (eg KEY_RIGHT) to its numeric equivalent if it is known
        std::optional<int> ncurses_index_of(const std::string& name)
        {
            static const std::map<std::string, int> ncurses_to_int = {
                { "KEY_LEFT", KEY_LEFT }, { "KEY_RIGHT", KEY_RIGHT }, { "KEY_UP", KEY_UP },
                { "KEY_DOWN", KEY_DOWN }, { "KEY_NPAGE", KEY_NPAGE }, { "KEY_PPAGE", KEY_PPAGE },
                { "KEY_HOME", KEY_HOME }, { "KEY_END", KEY_END },
            };

            if (ncurses_to_int.contains(name))
            {
                return ncurses_to_int.at(name);
            }
            return std::nullopt;
        }

        // Convert a string such as "^KD" to a control-K (ASCII 11) and D (ascii 4)
        std::list<char> parse_sequence(const std::string& sequence)
        {
            std::list<char> result;
            for (auto i = 0U; i < sequence.length(); ++i)
            {
                char key;
                if ((sequence[i] == '^') && (i < sequence.length() - 1))
                {
                    key = sequence[++i] - 'A' + 1;
                }
                else
                {
                    key = sequence[i];
                }
                result.push_back(key);
            }
            return result;
        }
    } // namespace

    Keymap::Keymap(const std::string& filename)
    {
        if (filename.empty())
        {
            return;
        }

        std::ifstream file(filename);
        if (!file.is_open())
        {
            throw std::runtime_error("Can't open keymap file: " + filename);
        }
        std::string s;
        while (std::getline(file, s))
        {
            // Allow entries to be commented out
            const auto hash = s.find_first_of('#');
            if (hash != std::string::npos)
            {
                s = s.substr(0, hash);
            }

            // Assume that each line is in the format 'KEY_RIGHT ^KC'.
            std::vector<std::string> fields;
            const auto input(boost::to_upper_copy(s));
            boost::split(fields, input, boost::is_any_of("\t "), boost::token_compress_on);
            if (fields.size() == 2)
            {
                if (const auto key = ncurses_index_of(fields[0]))
                {
                    m_keymap.emplace(*key, parse_sequence(fields[1]));
                }
                else
                {
                    throw std::runtime_error("Unknown ncurses key " + fields[0] + " in " + filename);
                }
            }
        }
    }

    std::list<char> Keymap::translate(int key) const
    {
        const auto iter = m_keymap.find(key);
        if (iter != m_keymap.end())
        {
            return iter->second;
        }

        if (key >= KEY_MIN)
        {
            // The key is one that should be mapped but isn't.
            BOOST_LOG_TRIVIAL(trace) << "Warning: unmapped curses key #" << key;
        }

        // Return just the key, so it is in effect an unmapped sequence
        return { static_cast<char>(key) };
    }
} // namespace zcpm