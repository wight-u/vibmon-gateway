include(FetchContent)

# Use a local copy of Catch2 via CPM (already included from parent)
CPMAddPackage(
  NAME Catch2
  GITHUB_REPOSITORY catchorg/Catch2
  VERSION 3.7.1
  OPTIONS "CATCH_INSTALL_DOCS OFF"
)

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(CTest)
include(Catch)

add_executable(test_cobs test_cobs.cpp)
target_include_directories(test_cobs PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(test_cobs PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(test_cobs DISCOVERY_MODE PRE_RUN)

add_executable(test_crc16 test_crc16.cpp)
target_include_directories(test_crc16 PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(test_crc16 PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(test_crc16 DISCOVERY_MODE PRE_RUN)

add_executable(test_ring_buffer test_ring_buffer.cpp)
target_include_directories(test_ring_buffer PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(test_ring_buffer PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(test_ring_buffer DISCOVERY_MODE PRE_RUN)
