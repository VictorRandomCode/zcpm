project(tests)

SetupCompiler("-g;-Wno-unused-parameter")

set (Boost_USE_STATIC_LIBS OFF)
find_package (Boost REQUIRED COMPONENTS unit_test_framework)
include_directories (${Boost_INCLUDE_DIRS})

# 'tests' is the target name
# 'test1.cpp tests2.cpp' are source files with tests
add_executable (tests test_processor.cpp)
#target_link_libraries (tests ${Boost_LIBRARIES})

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} builder core)
