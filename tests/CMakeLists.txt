set(EXENAME tests)

find_package(Boost ${BOOST_VERSION} CONFIG REQUIRED COMPONENTS unit_test_framework)
include_directories(${Boost_INCLUDE_DIRS})

# 'tests' is the target name
# 'test1.cpp tests2.cpp' are source files with tests
add_executable(${EXENAME} test_processor.cpp)

target_include_directories(${EXENAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(${EXENAME}
  PRIVATE
  ${Boost_LIBRARIES}
  builder
  core
  spdlog::spdlog
)
