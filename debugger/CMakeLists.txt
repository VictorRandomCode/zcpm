set(LIBNAME debugger)

# This library uses replxx
FetchContent_Declare(replxx
  GIT_REPOSITORY "https://github.com/AmokHuginnsson/replxx"
  GIT_TAG release-0.0.4
)
FetchContent_MakeAvailable(replxx)

find_package(Boost ${BOOST_VERSION} CONFIG REQUIRED)

add_executable(${LIBNAME}
  main.cpp
  writer.cpp
  writer.hpp
)

target_include_directories(${LIBNAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(${LIBNAME}
  PRIVATE
  builder
  terminal
  core
  replxx
  spdlog::spdlog
)
