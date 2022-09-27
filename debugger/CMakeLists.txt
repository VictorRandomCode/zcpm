project(debugger)

SetupCompiler("-g;-Wno-unused-parameter")

# Find Boost (Refer https://cmake.org/cmake/help/latest/module/FindBoost.html for details)
#set(Boost_USE_STATIC_LIBS ON)
# Temporarily disable Boost's CMake, see https://stackoverflow.com/a/58085634
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.71.0 COMPONENTS log REQUIRED)

# Find replxx; for now it is assumed to be in ${HOME}/local
# TODO: make this be easily configurable
add_library(replxx STATIC IMPORTED)
set_target_properties(replxx PROPERTIES
  IMPORTED_LOCATION "$ENV{HOME}/local/lib/libreplxx.a"
  INTERFACE_INCLUDE_DIRECTORIES "$ENV{HOME}/local/include"
)

set(DBGSOURCE
  main.cpp
  writer.cpp
  )

set(DBGHEADER
  writer.hpp
  )

add_executable(${PROJECT_NAME} ${DBGSOURCE} ${DBGHEADER})

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} builder terminal core replxx)
