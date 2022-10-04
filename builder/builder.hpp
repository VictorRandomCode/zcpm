#pragma once

#include <zcpm/core/system.hpp>

#include <memory>

// Based on command line arguments, set up the logger and then construct
// and return a ready-to-use machine instance.
namespace zcpm
{
    std::unique_ptr<zcpm::System> build_machine(int argc, char** argv);
}
