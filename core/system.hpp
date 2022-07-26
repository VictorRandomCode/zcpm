#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "handlers.hpp"
#include "hardware.hpp"

namespace zcpm
{

    namespace terminal
    {
        class Terminal;
    }

    class System final
    {
    public:
        System(std::unique_ptr<terminal::Terminal> p_terminal,
               bool memcheck,
               const std::string& bdos_sym = "",
               const std::string& user_sym = "");

        System(const System&) = delete;
        System& operator=(const System&) = delete;
        System(System&&) = delete;
        System& operator=(System&&) = delete;

        ~System();

        // Configure FBASE and WBOOT (to help the system recognise BDOS & BIOS accesses), and
        // then invoke the BIOS initialisation code
        void setup_bios(uint16_t fbase, uint16_t wboot);

        // Perform necessary BDOS initialisation
        void setup_bdos();

        // Load a binary file into memory at the specified base address, not worrying about cmdline args
        bool load_binary(uint16_t base, const std::string& filename);

        // Set up the FCB in page zero based on the specified command line arguments
        bool load_fcb(const std::vector<std::string>& args);

        // Set PC to 0x0100 in readiness for running
        void reset();

        // Execute the system for instruction_count instructions
        void step(size_t instruction_count = 1);

        // Run the system until termination or a breakpoint
        void run();

        void set_input_handler(const InputHandler& handler);
        void set_output_handler(const OutputHandler& handler);

        // This is public.  Normally "implementation details" such as this would be hidden,
        // but in the system being modelling the various items are deeply coupled anyway.
        // TODO: more thought needed on this issue.

        Hardware m_hardware;
    };

} // namespace zcpm
