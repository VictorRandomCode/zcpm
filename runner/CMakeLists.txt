project(runner)

SetupCompiler("-g;-Wno-unused-parameter")

# Find Boost (Refer https://cmake.org/cmake/help/latest/module/FindBoost.html for details)
set(Boost_USE_STATIC_LIBS ON)
# Temporarily disable Boost's CMake, see https://stackoverflow.com/a/58085634
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.71.0 COMPONENTS log REQUIRED)

set(CLISOURCE
  main.cpp
  )

set(CLIHEADER
  )

add_executable(${PROJECT_NAME} ${CLISOURCE} ${CLIHEADER})

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} builder console core ${Boost_LIBRARIES})
