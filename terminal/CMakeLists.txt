set(LIBNAME terminal)

find_package(Boost ${BOOST_VERSION} CONFIG COMPONENTS log REQUIRED)

# Needs pthread to support boost::log
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

# Find NCurses (via Curses)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses)

add_library(${LIBNAME}
  keymap.cpp
  keymap.hpp
  plain.cpp
  plain.hpp
  televideo.cpp
  televideo.hpp
  terminal.cpp
  terminal.hpp
  type.cpp
  type.hpp
  vt100.cpp
  vt100.hpp
)

# Make sure Boost doesn't try to use the (removed) std::unary_function in boost::hash via boost/algorithm/string.hpp
target_compile_definitions(${LIBNAME} PUBLIC BOOST_NO_CXX98_FUNCTION_BASE)

target_include_directories(${LIBNAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(${LIBNAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(${LIBNAME}
  PUBLIC
  ${Boost_LIBRARIES}
  PRIVATE
  ncurses
)
