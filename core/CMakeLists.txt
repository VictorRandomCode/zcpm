set(LIBNAME core)

find_package(Boost ${BOOST_VERSION} CONFIG COMPONENTS log REQUIRED)

# Needs pthread to support boost::log
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

add_library(${LIBNAME}
  bdos.cpp
  bdos.hpp
  bios.cpp
  bios.hpp
  config.hpp
  debugaction.cpp
  debugaction.hpp
  disk.cpp
  disk.hpp
  fcb.cpp
  fcb.hpp
  handlers.hpp
  hardware.cpp
  hardware.hpp
  idebuggable.hpp
  imemory.hpp
  instructions.hpp
  processor.cpp
  processor.hpp
  processordata.hpp
  registers.hpp
  symboltable.cpp
  symboltable.hpp
  system.cpp
  system.hpp
)

target_include_directories(${LIBNAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})

# Export the headers of this library so that users of this library see them
target_include_directories(${LIBNAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(${LIBNAME}
  PUBLIC
  ${Boost_LIBRARIES}
  PRIVATE
  Threads::Threads
  terminal
)
