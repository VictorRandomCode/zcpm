cmake_minimum_required(VERSION 3.16)

option(USE_TIDY "Enable clang's static analyser" OFF)
option(USE_SANITISERS "Enable ASAN and UBSAN" OFF)
option(USE_PROFILE "Enable profiling" OFF)
option(USE_ANALYSER "Enable GCC10 static analyser" OFF)

# Typical invocations:
#   cmake -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ ../zcpm
# If needed, append e.g. -DUSE_SANITISERS=OFF or -DUSE_TIDY=ON or -DUSE_PROFILE=ON
# Or to use gcc 10 analyser:
#   cmake -DUSE_ANALYSER=ON ../zcpm

project(zcpm_project)

set(CMAKE_CXX_STANDARD 20)

# Generate a build commands json file to help some external tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optional: enable clang-tidy; can be slow and noisy.  We do this globally, not per-project
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
    # clang's sanitisers seem lacking in macOS default clang ("AppleClang"), but are ok in macOS downloaded clang ("Clang")
    if (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
      message("Sanitisers are disabled for Apple's clang (use releases.llvm.org instead)")
    else()
      message("Sanitisers are enabled for '${PROJECT_NAME}'")
      # Note that enabling these does slow down both compilation and execution.  But according
      # to the docco for these, we should usually keep these enabled in most cases, it's worth it.
      # However, they can clash with some tools e.g. valgrind, gdb, etc.
      add_compile_options(-fsanitize=address -fsanitize=undefined)
      add_link_options(-fsanitize=address -fsanitize=undefined)
    endif()
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
  add_compile_options(-Wall -Wextra -Wold-style-cast -Werror ${OPTIONS})

endfunction()


add_subdirectory(builder)
add_subdirectory(terminal)
add_subdirectory(core)
add_subdirectory(debugger)
add_subdirectory(runner)
