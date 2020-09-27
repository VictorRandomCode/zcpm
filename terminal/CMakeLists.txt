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
  vt100.cpp
  plain.cpp
  televideo.cpp
  type.cpp
  )

set(LIBHEADER
  iterminal.hpp
  vt100.hpp
  plain.hpp
  televideo.hpp
  type.hpp
  )

add_library(${PROJECT_NAME} ${LIBSOURCE} ${LIBHEADER})

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} ncurses)
