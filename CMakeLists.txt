cmake_minimum_required(VERSION 3.16)
project(tgn
    VERSION 1.0.0
    DESCRIPTION "Telegram notification tool"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)
message(STATUS "Found CURL: ${CURL_LIBRARIES} (version \"${CURL_VERSION_STRING}\")")

find_package(CLI11 REQUIRED)
message(STATUS "Found CLI11: ${CLI11_DIR} (version \"${CLI11_VERSION}\")")

set(SOURCES
    src/main.cpp
    src/cli.cpp
    src/limits.cpp
    src/http.cpp
    src/telegram.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# The version shown by --version follows project(VERSION);
configure_file(src/version.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/tgn_version.hpp
               @ONLY)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  CURL::libcurl
  CLI11::CLI11
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wswitch-enum
    -Wconversion
    -Wsign-conversion
  )
endif()
