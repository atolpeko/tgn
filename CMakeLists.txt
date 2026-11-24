cmake_minimum_required(VERSION 3.21...3.31)
project(tgn
    VERSION 1.1.0
    DESCRIPTION "Telegram notification tool"
    LANGUAGES CXX
)

# Toolchain floor. Linux and macOS only, GCC or Clang.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
    message(FATAL_ERROR
      "GCC 11 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION})")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR
      "Clang 14 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION})")
  endif()
else()
  message(FATAL_ERROR
    "Unsupported compiler '${CMAKE_CXX_COMPILER_ID}': use GCC 11+ or Clang 14+")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
  Debug Release RelWithDebInfo MinSizeRel)

option(TGN_WERROR "Treat compiler warnings as errors" OFF)
option(TGN_LTO "Link-time optimisation in optimised builds" ON)
option(TGN_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(CURL 7.68 REQUIRED)
message(STATUS "Found CURL: ${CURL_LIBRARIES} (version \"${CURL_VERSION_STRING}\")")

find_package(CLI11 2.1 REQUIRED)
message(STATUS "Found CLI11: ${CLI11_DIR} (version \"${CLI11_VERSION}\")")

set(SOURCES
    src/main.cpp
    src/cli.cpp
    src/limits.cpp
    src/http.cpp
    src/telegram.cpp
    src/watch.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

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

target_compile_options(${PROJECT_NAME} PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wswitch-enum
  -Wconversion
  -Wsign-conversion
  -Wformat=2
  -Wnull-dereference
  -Wdouble-promotion
  -Wimplicit-fallthrough
  -Wcast-qual
  -Wundef
  -Wnon-virtual-dtor
)

if(TGN_WERROR)
  target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_options(${PROJECT_NAME} PRIVATE
    -fstack-protector-strong
    -fPIE
    -D_GLIBCXX_ASSERTIONS
  )
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>
  )
  target_link_options(${PROJECT_NAME} PRIVATE
    -pie
    -Wl,-z,relro,-z,now
  )
endif()

if(TGN_SANITIZE)
  target_compile_options(${PROJECT_NAME} PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=all
  )
  target_link_options(${PROJECT_NAME} PRIVATE
    -fsanitize=address,undefined
  )
endif()

if(TGN_LTO AND NOT TGN_SANITIZE)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT tgn_ipo_supported OUTPUT tgn_ipo_error LANGUAGES CXX)
  if(tgn_ipo_supported)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
  else()
    message(STATUS "LTO unavailable, building without it: ${tgn_ipo_error}")
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES README.md LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH "")
