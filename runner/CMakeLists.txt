set(EXENAME runner)

find_package(Boost ${BOOST_VERSION} CONFIG COMPONENTS log REQUIRED)

add_executable(${EXENAME}
  main.cpp
)

target_include_directories(${EXENAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(${EXENAME}
  PRIVATE
  builder
  terminal
  core
  ${Boost_LIBRARIES}
)
