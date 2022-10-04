#include "writer.hpp"

#include <zcpm/builder/builder.hpp>
#include <zcpm/core/debugaction.hpp>
#include <zcpm/core/hardware.hpp>
#include <zcpm/core/idebuggable.hpp>
#include <zcpm/core/system.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/log/utility/setup.hpp>

#include <cerrno>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <regex>
#include <replxx.hxx>
#include <string>
#include <vector>

namespace
{
    using TokenVector = std::vector<std::string>;

    // The current implementation of how commands are defined and parsed is too onerous. It works, but it won't scale.
    // When time allows, this should be reworked to use a more expressive grammar, possibly via some third party
    // library.

    struct Command
    {
        const TokenVector m_verbs;     // Command 'verb' plus zero or more synonyms
        const TokenVector m_nouns;     // Zero or more possibilities for the first 'noun'
        const size_t m_min_word_count; // Minimum count of parameter words expected
        const size_t m_max_word_count; // Maximum count of parameter words expected
        const std::string m_help;
        const replxx::Replxx::Color m_colour;
        // Takes tokenised command line, returns true if the Debugger should stop
        const std::function<bool(const TokenVector& input)> m_handler;

        void describe(std::ostream& os) const
        {
            os << "  " << m_verbs.front();
            for (size_t i = 0; i < m_min_word_count; ++i)
            {
                os << " <param>";
            }
            if (m_min_word_count < m_max_word_count)
            {
                os << " [";
                for (auto i = m_min_word_count; i < m_max_word_count; ++i)
                {
                    os << " <param>";
                }
                os << ']';
            }
            os << " : " << m_help;
        }
    };

    std::ostream& operator<<(std::ostream& os, const Command& command)
    {
        command.describe(os);
        return os;
    }

    // Given ("first second,third fourth") returns {"first","second,third","fourth"}
    // Given ("first second,third fourth",",") returns {"first second","third fourth"}
    TokenVector parse_words(const std::string& input, const std::string& delimiter = " ")
    {
        TokenVector result;
        boost::split(result, input, boost::is_any_of(delimiter));
        return result;
    }

    // Assuming payload is of the form either "foo1+17a,32b" or "123"
    std::unique_ptr<zcpm::DebugAction> parse_and_create_debug_action(zcpm::System* p_machine,
                                                                     const std::string& type,
                                                                     const std::string& payload)
    {
        BOOST_ASSERT(p_machine);

        const std::regex command_regex("([A-Za-z0-9+-]+)(?:,([0-9A-Fa-f]+))?", std::regex_constants::ECMAScript);
        std::smatch command_match;
        if (!std::regex_search(payload, command_match, command_regex))
        {
            std::cout << "Failed to parse breakpoint command" << std::endl;
            std::cout << "Expected it in the form (e.g.) 'b0100' or 'bfoo1+17' or 'bblah-2,1a'" << std::endl;
            return {};
        }

        // In which case location_string is now "foo1+17a" and count_string is now "32b"
        // (count_string can be empty if no ",blah" has been specified)
        BOOST_ASSERT(command_match.size() >= 3);
        const auto location_string(command_match[1].str());
        const auto count_string(command_match[2].str());
        const auto [ok, a] = p_machine->m_hardware.evaluate_address_expression(location_string);
        if (!ok)
        {
            std::cout << "Couldn't evaluate address expression" << std::endl;
            return {};
        }

        // Create the breakpoint/passpoint/watchpoint
        std::unique_ptr<zcpm::DebugAction> result;
        if (type == "breakpoint")
        {
            if (count_string.empty())
            {
                result = zcpm::DebugAction::create(zcpm::DebugAction::Type::BREAKPOINT, a, location_string);
            }
            else
            {
                std::cout << "Too many values for a breakpoint" << std::endl;
            }
        }
        else if (type == "passpoint")
        {
            if (count_string.empty())
            {
                std::cout << "Passpoint requires a count" << std::endl;
            }
            else
            {
                result =
                    zcpm::DebugAction::create(zcpm::DebugAction::Type::PASSPOINT, a, location_string, count_string);
            }
        }
        else if (type == "watchpoint")
        {
            if (count_string.empty())
            {
                result = zcpm::DebugAction::create(zcpm::DebugAction::Type::WATCHPOINT, a, location_string);
            }
            else
            {
                std::cout << "Too many values for a watchpoint" << std::endl;
            }
        }
        else
        {
            std::cout << "Unknown debug action type" << std::endl;
        }

        return result;
    }

    // Hooks for replxx callbacks

    replxx::Replxx::completions_t hook_completion(const std::string& input,
                                                  int& context_len,
                                                  const std::vector<Command>& commands)
    {
        replxx::Replxx::completions_t result;

        if (input.size() == static_cast<size_t>(context_len))
        {
            // Current context is all the input, so just match the leading (partial) verb
            for (const auto& command : commands)
            {
                if (input.compare(0, context_len, command.m_verbs.front(), 0, context_len) == 0)
                {
                    result.emplace_back(command.m_verbs.front(), command.m_colour);
                }
            }
        }
        else
        {
            // Current context appears to be a parameter, not the verb.  So tokenise the input and
            // then use the completed verb as the context for completion of this parameter.

            const auto words(parse_words(input));
            BOOST_ASSERT(words.size() > 1);

            const auto& verb(words.front());
            const auto& partial(words.back());

            for (const auto& command : commands)
            {
                for (const auto& alias : command.m_verbs)
                {
                    if (alias == verb)
                    {
                        // Bail out early if this already appears to be too much input
                        if (words.size() > command.m_max_word_count + 1)
                        {
                            return {};
                        }

                        // We have a matching verb/alias.  Build a list of partial matching nouns.
                        const auto& options = command.m_nouns;
                        for (const auto& option : options)
                        {
                            if (partial.compare(0, context_len, option, 0, context_len) == 0)
                            {
                                result.emplace_back(option, command.m_colour);
                            }
                        }
                    }
                }
            }
        }

        return result;
    }

    // Return true if happy, false if we should quit
    bool find_and_handle_command(const std::vector<Command>& commands, const std::string& input)
    {
        const auto words = parse_words(input);
        BOOST_ASSERT(!words.empty());
        const auto& verb = words.front();

        for (const auto& command : commands)
        {
            for (const auto& command_verb : command.m_verbs)
            {
                if (verb == command_verb)
                {
                    // Found it.  Handle it if we can.
                    if ((words.size() >= command.m_min_word_count + 1) &&
                        (words.size() <= command.m_max_word_count + 1))
                    {
                        try
                        {
                            if (command.m_handler(words))
                            {
                                return false; // Indicate that we should quit
                            }
                        }
                        catch (const std::exception& e)
                        {
                            std::cerr << "Exception: " << e.what() << std::endl;
                        }

                        return true; // Indicate that all is well and we should keep running
                    }

                    std::cout << "Wrong parameter count for '" << verb << "'; found " << words.size() - 1
                              << " but need " << command.m_min_word_count;
                    if (command.m_max_word_count > command.m_min_word_count)
                    {
                        std::cout << ".." << command.m_max_word_count;
                    }
                    std::cout << std::endl;

                    return true;
                }
            }
        }

        std::cout << "Unknown command '" << verb << "'" << std::endl;
        return true;
    }

    auto run(Writer& writer, zcpm::System* p_machine, zcpm::IDebuggable* p_debuggable)
    {
        const std::vector<Command> commands{
            Command{ { "clear" },
                     {},
                     1,
                     1,
                     "Removes a debugger action",
                     replxx::Replxx::Color::DEFAULT,
                     [&p_debuggable](const TokenVector& input)
                     {
                         const auto number = std::strtoul(input[1].c_str(), nullptr, 0);
                         const auto ok = p_debuggable->remove_action(number);
                         if (ok)
                         {
                             std::cout << "Removed." << std::endl;
                         }
                         else
                         {
                             std::cout << "Not removed, an error occured." << std::endl;
                         }
                         return false;
                     } },
            Command{ { "dump" },
                     {},
                     1,
                     2,
                     "Dump memory",
                     replxx::Replxx::Color::DEFAULT,
                     [&writer](const TokenVector& input)
                     {
                         const auto base = std::strtoul(input[1].c_str(), nullptr, 16);
                         auto count = 12U;
                         if (input.size() > 2)
                         {
                             count = std::strtoul(input[2].c_str(), nullptr, 16);
                         }
                         writer.dump(base, count);
                         return false;
                     } },
            Command{ { "examine", "x" },
                     {},
                     0,
                     0,
                     "Show current register values",
                     replxx::Replxx::Color::DEFAULT,
                     [&writer](const TokenVector& /*input*/)
                     {
                         writer.examine();
                         return false;
                     } },
            Command{ { "go" },
                     {},
                     0,
                     0,
                     "Set the program runnning",
                     replxx::Replxx::Color::DEFAULT,
                     [&p_machine, &writer](const TokenVector& /*input*/)
                     {
                         p_machine->run();
                         writer.examine();
                         return false;
                     } },
            Command{ { "help" },
                     {},
                     0,
                     0,
                     "Shows this information",
                     replxx::Replxx::Color::DEFAULT,
                     [&commands](const TokenVector& /*input*/)
                     {
                         for (const auto& command : commands)
                         {
                             std::cout << command << std::endl;
                         }
                         return false;
                     } },
            Command{ { "list" },
                     {},
                     0,
                     2,
                     "Disassemble the next N instructions",
                     replxx::Replxx::Color::DEFAULT,
                     [&writer](const TokenVector& input)
                     {
                         auto base = -1;  // Default to using current PC as the starting point
                         auto count = 12; // Default to listing 12 instructions
                         if (input.size() >= 2)
                         {
                             base = std::strtoul(input[1].c_str(), nullptr, 16);
                             if (input.size() >= 3)
                             {
                                 count = std::strtoul(input[2].c_str(), nullptr, 16);
                             }
                         }
                         writer.list(base, count);
                         return false;
                     } },
            Command{ { "monitor" },
                     {},
                     0,
                     0,
                     "Run the program, showing each step as it happens",
                     replxx::Replxx::Color::DEFAULT,
                     [&p_machine, &writer](const TokenVector& /*input*/)
                     {
                         // Monitor; like 'go', but trace step-by-step (lots of output!)
                         writer.examine();
                         do
                         {
                             p_machine->step();
                             writer.examine();
                         } while (p_machine->m_hardware.running());
                         return false;
                     } },
            Command{ { "quit" },
                     {},
                     0,
                     0,
                     "Exit from ZCPM",
                     replxx::Replxx::Color::RED,
                     [](const TokenVector& /*input*/) { return true; } },
            Command{ { "set" },
                     { "breakpoint", "passpoint", "watchpoint" },
                     2,
                     2,
                     "Set a debug action",
                     replxx::Replxx::Color::DEFAULT,
                     [&p_debuggable, &p_machine](const TokenVector& input)
                     {
                         BOOST_ASSERT(input.size() == 3);
                         // Create the breakpoint/passpoint/watchpoint
                         auto action = parse_and_create_debug_action(p_machine, input[1], input[2]);
                         // And remember it if successful
                         if (action)
                         {
                             p_debuggable->add_action(std::move(action));
                         }
                         return false;
                     } },
            Command{ { "show" },
                     { "symbols", "actions", "registers" },
                     1,
                     1,
                     "Show state information",
                     replxx::Replxx::Color::GREEN,
                     [&p_debuggable, &p_machine, &writer](const TokenVector& input)
                     {
                         BOOST_ASSERT(input.size() > 1);
                         const auto& noun = input[1];
                         if (noun == "symbols")
                         {
                             p_machine->m_hardware.dump_symbol_table();
                         }
                         else if (noun == "actions")
                         {
                             p_debuggable->show_actions(std::cout);
                         }
                         else if (noun == "registers")
                         {
                             writer.examine();
                         }
                         else
                         {
                             std::cout << "Unknown option" << std::endl;
                         }
                         return false;
                     } },
            Command{ { "trace" },
                     {},
                     0,
                     0,
                     "Single step",
                     replxx::Replxx::Color::RED,
                     [&p_machine, &writer](const TokenVector& /*input*/)
                     {
                         // Single step (similar to DebugZ 't' command)
                         p_machine->step();
                         writer.examine();
                         return false;
                     } },
        };

        replxx::Replxx rx;

        rx.install_window_change_handler();

        // set the path to the history file
        std::string history_file{ "./.zcpm_history.txt" };

        // load the history file if it exists
        rx.history_load(history_file);

        // set the max history size
        rx.set_max_history_size(128);

        // set the max number of hint rows to show
        rx.set_max_hint_rows(3);

        rx.set_completion_callback(
            [&commands](const std::string& input, int context_len) -> replxx::Replxx::completions_t
            { return hook_completion(input, context_len, commands); });

        // other api calls
        rx.set_word_break_characters(" \t.,-%!;:=*~^'\"/?<>|[](){}");
        rx.set_completion_count_cutoff(128);
        rx.set_double_tab_completion(false);
        rx.set_complete_on_empty(true);
        rx.set_beep_on_ambiguous_completion(false);
        rx.set_no_color(false);

        while (true)
        {
            // Prompt and retrieve input from the user, initially as a char*
            char const* cinput;
            do
            {
                cinput = rx.input("ZCPM> ");
            } while ((cinput == nullptr) && (errno == EAGAIN));
            if (cinput == nullptr)
            {
                break;
            }
            // Convert it to a std::string for convenience
            const std::string input = cinput;

            if (input.empty())
            {
                // Empty line, no action, read input again
                continue;
            }

            rx.history_add(input);

            if (!find_and_handle_command(commands, input))
            {
                break;
            }
        }

        rx.history_save(history_file);
    }

} // namespace

int main(int argc, char* argv[])
{
    std::unique_ptr<zcpm::System> p_machine;
    try
    {
        p_machine = zcpm::build_machine(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    if (!p_machine)
    {
        return EXIT_FAILURE;
    }

    // And we'll need a Debuggable to help us query the system
    auto p_debuggable = p_machine->m_hardware.get_idebuggable(); // Yuk.  TODO.

    // Create a Writer instance, that knows how to format the info into a particular form,
    // and where to write it.  In the future we will have different Writer instances for
    // different output forms, and have them send info to an arbitrary stream.
    Writer writer(p_debuggable, p_machine->m_hardware, std::cout);

    try
    {
        run(writer, p_machine.get(), p_debuggable);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
