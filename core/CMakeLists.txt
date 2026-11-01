find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(launcher_core STATIC ${CORE_SOURCES})
target_include_directories(launcher_core PUBLIC include)
target_link_libraries(launcher_core PUBLIC
    cpr::cpr nlohmann_json::nlohmann_json OpenSSL::Crypto
    spdlog::spdlog libzip::zip)

add_executable(launcher_core_cli bindings/rpc_main.cpp)
target_link_libraries(launcher_core_cli PRIVATE launcher_core)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
if(TEST_SOURCES)
  add_executable(core_tests ${TEST_SOURCES})
  target_link_libraries(core_tests PRIVATE launcher_core GTest::gtest_main)
  add_test(NAME core_tests COMMAND core_tests)
endif()