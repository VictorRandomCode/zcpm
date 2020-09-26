project(builder)

SetupCompiler("-g;-Wno-unused-parameter")

# Find Boost (Refer https://cmake.org/cmake/help/latest/module/FindBoost.html for details)
set(Boost_USE_STATIC_LIBS ON)
# Temporarily disable Boost's CMake, see https://stackoverflow.com/a/58085634
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost 1.71.0 COMPONENTS program_options REQUIRED)

add_library(${PROJECT_NAME} builder.hpp builder.cpp)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})

# Export the headers of this library so that users of this library see them
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} terminal core)
