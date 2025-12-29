set(LIBNAME builder)

find_package(Boost ${BOOST_VERSION} CONFIG COMPONENTS program_options REQUIRED)

add_library(${LIBNAME}
  builder.hpp
  builder.cpp
)

target_include_directories(${LIBNAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})

# Export the headers of this library so that users of this library see them
target_include_directories(${LIBNAME} PUBLIC ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(${LIBNAME}
  PRIVATE
  ${Boost_LIBRARIES}
  core
  spdlog::spdlog
  terminal
)
