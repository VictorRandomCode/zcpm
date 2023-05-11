cmake_minimum_required(VERSION 3.16)

include(FetchContent)

option(USE_TIDY "Enable clang's static analyser" OFF)
option(USE_SANITISERS "Enable ASAN and UBSAN" OFF)
option(USE_PROFILE "Enable profiling" OFF)
option(USE_ANALYSER "Enable GCC11 static analyser" OFF)

# Typical invocations:
#   cmake -DCMAKE_PREFIX_PATH=~/local -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ ../zcpm
# or for gcc via brew on macOS:
#   cmake -DCMAKE_PREFIX_PATH=~/local -D CMAKE_C_COMPILER=gcc-13 -D CMAKE_CXX_COMPILER=g++-13 ../zcpm
# If needed, append e.g. -DUSE_SANITISERS=OFF or -DUSE_TIDY=ON or -DUSE_PROFILE=ON
# Or to use gcc analyser:
#   cmake -DCMAKE_PREFIX_PATH=~/local -DUSE_ANALYSER=ON ../zcpm

project(zcpm_project)

set(CMAKE_CXX_STANDARD 20)

# Generate a build commands json file to help some external tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional: enable clang-tidy; can be slow and noisy.  This is done globally, not per-project
if (USE_TIDY)
  find_program(CLANG_TIDY_EXE NAMES "clang-tidy" DOC "path to clang-tidy executable")
  if (CLANG_TIDY_EXE)
    message("clang tidy is configured using ${CLANG_TIDY_EXE}; be patient when building!")
    # Refer to .clang-tidy for selected options (and possibly in subdirectories)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message("*** Note: clang-tidy was requested but no clang-tidy binary was found")
  endif()
else()
  message("Static analysis is not enabled")
endif()


function(SetupCompiler OPTIONS)

  # Enable sanitisers of interest.
  if (USE_SANITISERS)
    message("Sanitisers are enabled for '${PROJECT_NAME}'")
    # Note that enabling these does slow down both compilation and execution.  But according
    # to the docco for these, we should usually keep these enabled in most cases, it's worth it.
    # However, they can clash with some tools e.g. valgrind, gdb, etc.
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
  else()
    message("Sanitisers are not enabled for '${PROJECT_NAME}'")
  endif()

  # Enable profiling, which is supported only on GCC.
  if (USE_PROFILE)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      message("Profiling is being enabled for '${PROJECT_NAME}'")
      add_compile_options(-pg)
      add_link_options(-pg)
    else()
      message("Profiling is only supported for GCC, not ${CMAKE_CXX_COMPILER_ID}")
    endif()
  else()
    message("Profiling is not enabled for '${PROJECT_NAME}'")
  endif()

  # Enable analyser, but only with GCC 10 and later.
  if (USE_ANALYSER)
    if ((CMAKE_CXX_COMPILER_ID MATCHES "GNU") AND (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 10.0))
      message("Analyser is being enabled for '${PROJECT_NAME}'")
      add_compile_options(-fanalyzer)
    else()
      message("Analyser is only supported for GCC 10+, not ${CMAKE_CXX_COMPILER_ID}")
    endif()
  else()
    message("Analyser is not enabled for '${PROJECT_NAME}'")
  endif()

  # Set up a common minimal set of compilation options plus whatever the caller has asked for
  # (The caller-supplied options might actually be turning off something that we turn on here!)
  add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wmisleading-indentation -pedantic -Werror ${OPTIONS})

endfunction()

# ZCPM makes use of fmtlib until std::format is more widely available
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.0.0
)
FetchContent_MakeAvailable(fmt)

add_subdirectory(builder)
add_subdirectory(terminal)
add_subdirectory(core)
add_subdirectory(debugger)
add_subdirectory(runner)
add_subdirectory(tests)
