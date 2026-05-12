cmake_minimum_required(VERSION 3.5)
project(vibmon-gateway VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#CPM bootstrap
set(CPM_DOWNLOAD_VERSION 0.40.2)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake")
  message(STATUS "Downloading CPM.cmake v${CPM_DOWNLOAD_VERSION}")
  file(DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake"
  )
endif()
include(cmake/CPM.cmake)

#FFTW3
# Prefer system package (fast); fall back to building from source via CPM.
find_package(PkgConfig QUIET)
set(_fftw3_system FALSE)
if(PkgConfig_FOUND)
  pkg_check_modules(FFTW3 QUIET fftw3)
  if(FFTW3_FOUND)
    set(_fftw3_system TRUE)
    add_library(fftw3_iface INTERFACE)
    target_link_libraries(fftw3_iface INTERFACE ${FFTW3_LIBRARIES})
    target_include_directories(fftw3_iface INTERFACE ${FFTW3_INCLUDE_DIRS})
  endif()
endif()

if(NOT _fftw3_system)
  message(STATUS "FFTW3 not found via pkg-config — building from source (this takes ~1 min)")
  CPMAddPackage(
    NAME fftw3
    URL "https://fftw.org/fftw-3.3.10.tar.gz"
    URL_HASH MD5=8ccbf6a5ea78a16dbc3e1306e234cc5c
    OPTIONS
      "CMAKE_POLICY_VERSION_MINIMUM 3.5"
      "BUILD_SHARED_LIBS OFF"
      "ENABLE_FLOAT OFF"
      "ENABLE_THREADS ON"
      "WITH_COMBINED_THREADS ON"
      "ENABLE_AVX OFF"
      "ENABLE_SSE2 OFF"
  )
  add_library(fftw3_iface INTERFACE)
  target_link_libraries(fftw3_iface INTERFACE fftw3)
  if(fftw3_SOURCE_DIR)
    target_include_directories(fftw3_iface INTERFACE "${fftw3_SOURCE_DIR}/api")
  endif()
endif()

#SQLite3
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  message(STATUS "SQLite3 not found — fetching amalgamation via CPM")
  CPMAddPackage(
    NAME sqlite3
    URL "https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip"
    DOWNLOAD_ONLY YES
  )
  if(sqlite3_ADDED)
    add_library(sqlite3_amalgam STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
    target_include_directories(sqlite3_amalgam PUBLIC "${sqlite3_SOURCE_DIR}")
    target_compile_definitions(sqlite3_amalgam PRIVATE
      SQLITE_THREADSAFE=1
      SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    )
    add_library(SQLite::SQLite3 ALIAS sqlite3_amalgam)
  endif()
endif()

#cpp-httplib
CPMAddPackage(
  NAME httplib
  GITHUB_REPOSITORY yhirose/cpp-httplib
  GIT_TAG v0.18.5
  OPTIONS "HTTPLIB_COMPILE OFF"
)

#nlohmann/json
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  VERSION 3.11.3
  OPTIONS
    "JSON_BuildTests OFF"
    "JSON_Install OFF"
)

#OpenSSL (for WebSocket SHA-1 handshake)
find_package(OpenSSL QUIET)

#vibmon-gateway executable
add_executable(vibmon-gateway
  src/main.cpp
  src/serial_reader.cpp
  src/frame_decoder.cpp
  src/fft_worker.cpp
  src/db_writer.cpp
  src/http_server.cpp
  src/websocket_server.cpp
)

target_include_directories(vibmon-gateway PRIVATE include src)

target_link_libraries(vibmon-gateway PRIVATE
  fftw3_iface
  SQLite::SQLite3
  httplib::httplib
  nlohmann_json::nlohmann_json
  pthread
)

if(OpenSSL_FOUND)
  target_link_libraries(vibmon-gateway PRIVATE OpenSSL::Crypto)
  target_compile_definitions(vibmon-gateway PRIVATE HAVE_OPENSSL=1)
endif()

#systemd sd_notify integration (optional)
if(PkgConfig_FOUND)
  pkg_check_modules(SYSTEMD libsystemd QUIET)
  if(SYSTEMD_FOUND)
    target_link_libraries(vibmon-gateway PRIVATE ${SYSTEMD_LIBRARIES})
    target_include_directories(vibmon-gateway PRIVATE ${SYSTEMD_INCLUDE_DIRS})
    target_compile_definitions(vibmon-gateway PRIVATE HAVE_SYSTEMD=1)
  endif()
endif()

target_compile_options(vibmon-gateway PRIVATE
  -Wall -Wextra -Wpedantic
  $<$<CONFIG:Release>:-O2>
  $<$<CONFIG:Debug>:-g -O0 -fsanitize=address,undefined>
)
target_link_options(vibmon-gateway PRIVATE
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

#install
include(GNUInstallDirs)
install(TARGETS vibmon-gateway RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()