// A program to allow us to run an arbitrary CP/M binary.  Very unfinished!

#include <cstdlib>
#include <memory>
#include <stdexcept>

#include <spdlog/spdlog.h>
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
        spdlog::error("Exception: {}", e.what());
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
        spdlog::error("Exception: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("Exception.");
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
