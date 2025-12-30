#pragma once

#include <memory>

#include <zcpm/core/system.hpp>

// Based on command line arguments, set up the logger and then construct and return a ready-to-use machine instance.
namespace zcpm
{
std::unique_ptr<System> build_machine(int argc, char** argv);
}
