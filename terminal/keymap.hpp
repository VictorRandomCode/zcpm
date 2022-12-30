#pragma once

#include <list>
#include <map>
#include <string>

namespace zcpm
{

    class Keymap final
    {
    public:
        // Load mappings from the specified keymap file. Example entry in the file:
        //   KEY_RIGHT ^KC
        // which means that if a right-arrow key is pressed, then a control-K is generated and then a C is generated.
        // The key naming (e.g. KEY_RIGHT) is as per 'man getch' from ncurses. Each key (e.g. KEY_RIGHT) should appear
        // no more than once. If an entry for given key doesn't exist, then the key is returned untranslated. Note that
        // curses doesn't define a type for keystrokes such as these, they are simply 'int'.
        explicit Keymap(std::string_view filename);

        // Return the mapping for a given key. If not known, then returns a single-element list with the same key.
        std::list<char> translate(int key) const;

    private:
        std::map<int, std::list<char>> m_keymap;
    };

} // namespace zcpm
