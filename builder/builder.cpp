#include "builder.hpp"

#include <boost/program_options.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <zcpm/core/config.hpp>
#include <zcpm/core/system.hpp>
#include <zcpm/terminal/plain.hpp>
#include <zcpm/terminal/televideo.hpp>
#include <zcpm/terminal/terminal.hpp>
#include <zcpm/terminal/type.hpp>
#include <zcpm/terminal/vt100.hpp>

namespace zcpm
{

std::string home_plus(const std::string& addendum)
{
    auto value = std::string(std::getenv("HOME"));
    return value + "/" + addendum;
}

std::unique_ptr<zcpm::System> build_machine(int argc, char** argv)
{
    // Command line parameters and their defaults
    std::string logfile = "zcpm.log";
    bool tracing = false;
    std::string bdos_file_name;                      // The file that provides a binary BDOS (and CCP etc)
    std::uint16_t bdos_file_base = 0xDC00;           // Where to load that binary image
    std::uint16_t wboot = 0xF203;                    // Address of WBOOT in loaded binary BDOS
    std::uint16_t fbase = 0xE406;                    // Address of FBASE in loaded binary BDOS
    terminal::Type terminal = terminal::Type::PLAIN; // Terminal type
    std::string keymap_file_name;                    // The file that provides keystroke mapping for terminal emulation
    int columns = 80;                                // Number of display columns
    int rows = 24;                                   // Number of display rows
    Config config = { .memcheck = true,
                      .log_bdos = true,
                      .protect_warm_start_vector = true,
                      .protect_bdos_jump = true,
                      .bdos_sym = "~/zcpm/bdos.lab",
                      .user_sym = "" };
    std::string binary; // The CP/M binary that we try to load and execute
    std::vector<std::string> arguments;

    try
    {
        namespace po = boost::program_options;
        po::options_description desc("Supported options");
        desc.add_options()("help", "Displays this information")("bdosfile", po::value<std::string>(), "Binary file that provides BDOS etc")(
            "bdossym", po::value<std::string>(), "Optional symbol (.lab) file for BDOS")(
            "usersym", po::value<std::string>(), "Optional symbol (.lab) file for user executable")(
            "bdosbase", po::value<std::uint16_t>(), "Base address for binary BDOS file")(
            "wboot", po::value<std::uint16_t>(), "Address of WBOOT in loaded binary BDOS")(
            "fbase", po::value<std::uint16_t>(), "Address of FBASE in loaded binary BDOS")(
            "terminal", po::value<terminal::Type>(), "Terminal type to emulate")(
            "keymap", po::value<std::string>(), "Optional keymap file for terminal emulation")(
            "columns", po::value<int>(), "Terminal column count")("rows", po::value<int>(), "Terminal row count")(
            "memcheck", po::value<bool>(), "Enable memory access checks?")("logbdos", po::value<bool>(), "Enable logging of BDOS calls?")(
            "protectwarm", po::value<bool>(), "Protect warm start vector from modification?")(
            "protectbdosjump", po::value<bool>(), "Protect BDOS jump vector from modification?")(
            "logfile", po::value<std::string>(), "Name of logfile")("trace", po::value<bool>(), "Detailed (very verbose) logging?")(
            "binary", po::value<std::string>(), "CP/M binary input file to execute")(
            "args", po::value<std::vector<std::string>>(), "Parameters for binary");
        po::positional_options_description p;
        p.add("binary", 1)   // First 'free' argument is the binary under test
            .add("args", -1) // Any subsequent 'free' arguments are parameters for the binary
            ;

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
            std::println("ZCPM v0.1");
            std::println();
            std::cout << desc << std::endl;
            return nullptr;
        }
        bdos_file_name = (vm.count("bdosfile")) ? vm["bdosfile"].as<std::string>() : home_plus("zcpm/bdos.bin");
        config.bdos_sym = (vm.count("bdossym")) ? vm["bdossym"].as<std::string>() : home_plus("zcpm/bdos.lab");
        if (vm.count("bdosbase"))
        {
            bdos_file_base = vm["bdosbase"].as<std::uint16_t>();
        }
        if (vm.count("wboot"))
        {
            wboot = vm["wboot"].as<std::uint16_t>();
        }
        if (vm.count("fbase"))
        {
            fbase = vm["fbase"].as<std::uint16_t>();
        }
        if (vm.count("terminal"))
        {
            terminal = vm["terminal"].as<terminal::Type>();
        }
        keymap_file_name = (vm.count("keymap")) ? vm["keymap"].as<std::string>() : home_plus("zcpm/wordstar.keys");
        if (vm.count("columns"))
        {
            columns = vm["columns"].as<int>();
        }
        if (vm.count("rows"))
        {
            rows = vm["rows"].as<int>();
        }
        if (vm.count("memcheck"))
        {
            config.memcheck = vm["memcheck"].as<bool>();
        }
        if (vm.count("logbdos"))
        {
            config.log_bdos = vm["logbdos"].as<bool>();
        }
        if (vm.count("trace"))
        {
            tracing = vm["trace"].as<bool>();
        }
        if (vm.count("protectwarm"))
        {
            config.protect_warm_start_vector = vm["protectwarm"].as<bool>();
        }
        if (vm.count("protectbdosjump"))
        {
            config.protect_bdos_jump = vm["protectbdosjump"].as<bool>();
        }
        if (vm.count("usersym"))
        {
            config.user_sym = vm["usersym"].as<std::string>();
        }
        if (vm.count("logfile"))
        {
            logfile = vm["logfile"].as<std::string>();
        }
        if (vm.count("binary"))
        {
            binary = vm["binary"].as<std::string>();
        }
        if (vm.count("args"))
        {
            arguments = vm["args"].as<std::vector<std::string>>();
        }

        if (binary.empty())
        {
            std::cout << std::endl << "ZCPM/CPM" << std::endl << std::endl;
            std::cout << desc << std::endl;
            return nullptr;
        }
    }
    catch (const std::exception& e)
    {
        std::println(stderr, "Exception: {}", e.what());
        return nullptr;
    }

    // File sink for general logging (optionally with 'trace' messages included)
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logfile, true);
    file_sink->set_level(tracing ? spdlog::level::trace : spdlog::level::info);
    file_sink->set_pattern("[%H:%M:%S.%e %l] %v");
    // stderr sink for warnings and above
    auto err_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    err_sink->set_level(spdlog::level::warn);
    err_sink->set_pattern("[%^%l%$] %v");
    // Combine sinks
    auto logger = std::make_shared<spdlog::logger>("zcpm", spdlog::sinks_init_list{ file_sink, err_sink });
    spdlog::set_default_logger(logger);

    // Create the terminal emulation of choice
    std::unique_ptr<terminal::Terminal> p_terminal;
    switch (terminal)
    {
    case terminal::Type::PLAIN: p_terminal = std::make_unique<terminal::Plain>(rows, columns); break;
    case terminal::Type::VT100: p_terminal = std::make_unique<terminal::Vt100>(rows, columns, keymap_file_name); break;
    case terminal::Type::TELEVIDEO: p_terminal = std::make_unique<terminal::Televideo>(rows, columns, keymap_file_name); break;
    }

    // Put it all together
    auto p_machine(std::make_unique<zcpm::System>(std::move(p_terminal), config));

    // The BDOS/CCP binary is built from Z80 source code which was reconstructed from a CP/M 2.2 disassembly plus a
    // tweak or two. The assembled binary is what is loaded here. ZCPM intercepts calls to the BIOS from the BDOS.
    if (!p_machine->load_binary(bdos_file_base, bdos_file_name))
    {
        p_machine.reset();
        spdlog::error("Failed to load base memory image");
        return nullptr;
    }

    // Based on the current binary image, work out where the BIOS appears to start and set vectors and initialise data structures.
    try
    {
        p_machine->setup_bios(fbase, wboot);
    }
    catch (const std::exception& e)
    {
        p_machine.reset();
        spdlog::error("Exception: {}", e.what());
        return nullptr;
    }

    if (!p_machine->load_binary(0x0100, binary)) // CP/M binaries are ALWAYS loaded at 0x0100
    {
        p_machine.reset();
        spdlog::error("Failed to load binary");
        return nullptr;
    }

    p_machine->load_fcb(arguments);

    p_machine->reset();

    // Call the BDOS initialisation code so that it can set up its data structures before things are started for real.
    p_machine->setup_bdos();

    p_machine->reset();

    return p_machine;
}

} // namespace zcpm
