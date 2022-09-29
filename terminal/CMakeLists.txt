project(terminal)

SetupCompiler("-g;-Wno-unused-parameter")

# Find Boost (Refer https://cmake.org/cmake/help/latest/module/FindBoost.html for details)
set(Boost_USE_STATIC_LIBS ON)
# Temporarily disable Boost's CMake, see https://stackoverflow.com/a/58085634
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.71.0 COMPONENTS log REQUIRED)

# Needs pthread to support boost::log
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

# Find NCurses (via Curses)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses)

set(LIBSOURCE
  keymap.cpp
  plain.cpp
  televideo.cpp
  terminal.cpp
  type.cpp
  vt100.cpp
  )

set(LIBHEADER
  keymap.hpp
  plain.hpp
  televideo.hpp
  terminal.hpp
  type.hpp
  vt100.hpp
  )

add_library(${PROJECT_NAME} ${LIBSOURCE} ${LIBHEADER})

# Make sure Boost doesn't try to use the (removed) std::unary_function in boost::hash via boost/algorithm/string.hpp
target_compile_definitions(${PROJECT_NAME} PUBLIC BOOST_NO_CXX98_FUNCTION_BASE)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} ncurses fmt)
