// A program to allow us to run an arbitrary CP/M binary.  Very unfinished!

#include <boost/log/trivial.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <zcpm/builder/builder.hpp>
#include <zcpm/core/system.hpp>

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
        BOOST_LOG_TRIVIAL(trace) << "Exception: " << e.what();
    }

    if (!p_machine)
    {
        return EXIT_FAILURE;
    }

    auto ok = false;
    try
    {
        p_machine->run();
        ok = true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        BOOST_LOG_TRIVIAL(trace) << "Exception: " << e.what();
    }
    catch (...)
    {
        std::cerr << "Exception." << std::endl;
        BOOST_LOG_TRIVIAL(trace) << "Exception.";
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
