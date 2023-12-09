project(debugger)

# Debugger needs replxx from GitHub
FetchContent_Declare(replxx
  GIT_REPOSITORY "https://github.com/AmokHuginnsson/replxx"
  GIT_TAG release-0.0.4
  )
FetchContent_MakeAvailable(replxx)

SetupCompiler("-g;-Wno-unused-parameter")

# Find Boost (Refer https://cmake.org/cmake/help/latest/module/FindBoost.html for details)
#set(Boost_USE_STATIC_LIBS ON)
# Temporarily disable Boost's CMake, see https://stackoverflow.com/a/58085634
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost ${BOOST_VERSION} COMPONENTS log REQUIRED)

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
