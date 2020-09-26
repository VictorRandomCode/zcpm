project(core)

# Normally we need to optimise this library for speed, unless we're trying
# to use the debugger to find some specific issue.
SetupCompiler("-Ofast;-Wno-unused-parameter")
#SetupCompiler("-g;-Wno-unused-parameter")

# Find Boost (Refer https://cmake.org/cmake/help/latest/module/FindBoost.html for details)
set(Boost_USE_STATIC_LIBS ON)
# Temporarily disable Boost's CMake, see https://stackoverflow.com/a/58085634
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.71.0 COMPONENTS log REQUIRED)

# Needs pthread to support boost::log
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

set(LIBSOURCE
  bdos.cpp
  bios.cpp
  debugaction.cpp
  disk.cpp
  fcb.cpp
  hardware.cpp
  processor.cpp
  symboltable.cpp
  system.cpp
  )

set(LIBHEADER
  bdos.hpp
  bios.hpp
  debugaction.hpp
  disk.hpp
  fcb.hpp
  handlers.hpp
  hardware.hpp
  idebuggable.hpp
  imemory.hpp
  instructions.hpp
  processor.hpp
  processordata.hpp
  registers.hpp
  symboltable.hpp
  system.hpp
  )

add_library(${PROJECT_NAME} ${LIBSOURCE} ${LIBHEADER})

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})

# Export the headers of this library so that users of this library see them
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} Threads::Threads terminal)
