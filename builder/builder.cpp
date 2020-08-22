#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/program_options.hpp>

#include <zcpm/console/cursesconsole.hpp>
#include <zcpm/console/iconsole.hpp>
#include <zcpm/console/plainconsole.hpp>
#include <zcpm/core/system.hpp>

#include "builder.hpp"

namespace ZCPM
{

  std::unique_ptr<ZCPM::System> build_machine(int argc, char** argv)
  {
    // Command line parameters and their defaults
    std::string logfile = "zcpm.log";
    std::string bdos_file_name = "../bdos/bdos.bin"; // The file that provides a binary BDOS (and CCP etc)
    uint16_t bdos_file_base = 0xDC00;                // Where to load that binary image
    uint16_t wboot = 0xF203;                         // Address of WBOOT in loaded binary BDOS
    uint16_t fbase = 0xE406;                         // Address of FBASE in loaded binary BDOS
    bool use_curses = false;                         // Should we use a curses-based console?
    bool memcheck = true;                            // Enable RAM read/write checks?
    std::string user_sym;                            // Filename of .lab file which lists symbols in user's executable
    std::string bdos_sym;                            // Filename of .lab file which lists symbols in BDOS implementation
    std::string binary;                              // The CP/M binary that we try to load and execute
    std::vector<std::string> arguments;

    try
    {
      namespace po = boost::program_options;
      po::options_description desc("Allowed options");
      desc.add_options()("help", "Displays this information")(
        "bdosfile", po::value<std::string>(), "Binary file that provides BDOS etc")(
        "bdosbase", po::value<uint16_t>(), "Base address for binary BDOS file")(
        "wboot", po::value<uint16_t>(), "Address of WBOOT in loaded binary BDOS")(
        "fbase", po::value<uint16_t>(), "Address of FBASE in loaded binary BDOS")(
        "curses", po::value<bool>(), "Use a curses-based console?")(
        "memcheck", po::value<bool>(), "Enable memory access checks?")(
        "usersym", po::value<std::string>(), "Optional symbol (.lab) file for user executable")(
        "bdossym", po::value<std::string>(), "Optional symbol (.lab) file for BDOS")(
        "logfile", po::value<std::string>(), "Name of logfile")(
        "binary", po::value<std::string>(), "CP/M binary input file to execute")(
        "args", po::value<std::vector<std::string>>(), "Parameters for binary");
      po::positional_options_description p;
      p.add("binary", 1) // First 'free' argument is the binary under test
        .add("args", -1) // Any subsequent 'free' arguments are parameters for the binary
        ;

      po::variables_map vm;
      po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
      po::notify(vm);

      if (vm.count("help"))
      {
        std::cout << "Program name/version information TODO" << std::endl;
        std::cout << desc << std::endl;
        return nullptr;
      }
      if (vm.count("bdosfile"))
      {
        bdos_file_name = vm["bdosfile"].as<std::string>();
      }
      if (vm.count("bdosbase"))
      {
        bdos_file_base = vm["bdosbase"].as<uint16_t>();
      }
      if (vm.count("wboot"))
      {
        wboot = vm["wboot"].as<uint16_t>();
      }
      if (vm.count("fbase"))
      {
        fbase = vm["fbase"].as<uint16_t>();
      }
      if (vm.count("curses"))
      {
        use_curses = vm["curses"].as<bool>();
      }
      if (vm.count("memcheck"))
      {
        memcheck = vm["memcheck"].as<bool>();
      }
      if (vm.count("usersym"))
      {
        user_sym = vm["usersym"].as<std::string>();
      }
      if (vm.count("bdossym"))
      {
        bdos_sym = vm["bdossym"].as<std::string>();
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
      std::cerr << "Exception: " << e.what() << std::endl;
      return nullptr;
    }

    // Set up logging
    boost::log::add_file_log(boost::log::keywords::file_name = logfile, boost::log::keywords::auto_flush = true);
    boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::trace);

    std::unique_ptr<Console::IConsole> p_console;
    if (use_curses)
    {
      p_console = std::make_unique<Console::Curses>();
    }
    else
    {
      p_console = std::make_unique<Console::Plain>();
    }

    auto p_machine(std::make_unique<ZCPM::System>(std::move(p_console), memcheck, bdos_sym, user_sym));

    // For the BDOS/CCP/BIOS binary, I'm using Z80 source code reconstructed from a CP/M 2.2
    // disassembly, making tweaks to it, and then assembling it and loading into into RAM, and
    // then intercepting the BIOS calls which it makes.
    if (!p_machine->load_binary(bdos_file_base, bdos_file_name))
    {
      std::cerr << "Failed to load base memory image" << std::endl;
      return nullptr;
    }

    // Based on the current binary image, work out where the BIOS appears to start
    // and then manipulate it for our own use, so we can intercept calls to it.  If
    // it things don't look sane (e.g. bad loaded address or incorrect WBOOT/FBASE,
    // this will throw an exception.
    try
    {
      p_machine->setup_bios(fbase, wboot);
    }
    catch (const std::exception& e)
    {
      std::cerr << "Exception: " << e.what() << std::endl;
      return nullptr;
    }

    if (!p_machine->load_binary(0x0100, binary)) // CP/M binaries are ALWAYS loaded at 0x0100
    {
      std::cerr << "Failed to load binary" << std::endl;
      return nullptr;
    }

    p_machine->load_fcb(arguments);

    p_machine->reset();

    // Call the BDOS initialisation code so that it can set up its data structures
    // before things are started for real.
    p_machine->setup_bdos();

    p_machine->reset();

    return p_machine;
  }

} // namespace ZCPM
