cmake_minimum_required(VERSION 3.30)

project(zcpm_project VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
include(CheckIPOSupported)

option(USE_SANITISERS "Enable ASAN and UBSAN" OFF)
option(USE_PROFILE "Enable profiling" OFF)

# Typical invocations:
#   cmake -DCMAKE_PREFIX_PATH=~/local ../zcpm
# or (for example) gcc via brew on macOS:
#   cmake -DCMAKE_PREFIX_PATH=~/local -DCMAKE_C_COMPILER=gcc-15 -DCMAKE_CXX_COMPILER=g++-15 ../zcpm
# If needed, append e.g. -DUSE_SANITISERS=OFF or -DUSE_PROFILE=ON

set(Boost_NO_WARN_NEW_VERSIONS 1)
set(BOOST_VERSION 1.88.0)

# Generate a build commands json file to help some external tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# If supported, enable LTO for all targets
cmake_policy(SET CMP0069 NEW) 
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
if (ipo_supported)
  message(STATUS "IPO/LTO enabled for all targets")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
  # The output here can be quite verbose, enable if it is really needed
  # message(STATUS "IPO/LTO not supported: <${ipo_output}>")
  message(STATUS "IPO/LTO not supported")
endif()

# spdlog is used for logging
FetchContent_Declare(spdlog
  SYSTEM
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.16.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

include(functions.cmake)

# Define an interface-only target that has the preferred set of compiler flags. All targets should depend on this. This
# approach is cleaner than setting compiler flags globally which would leak into builds of third-party 'fetchcontent'
# libraries.
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
  -Wall
  -Wextra
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Woverloaded-virtual
  -Wmisleading-indentation
  -pedantic
  -Werror
)

add_subdirectory(builder)
add_subdirectory(terminal)
add_subdirectory(core)
add_subdirectory(debugger)
add_subdirectory(runner)
add_subdirectory(tests)
