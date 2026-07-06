cmake_minimum_required(VERSION 3.16)
project(ChatSDK_Test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 项目根目录
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SDK_DIR "${PROJECT_ROOT}/SDK")

# ─── 依赖：OpenSSL（httplib HTTPS 支持）─────────────
find_package(OpenSSL REQUIRED)

# ─── 依赖：spdlog ─────────────────────────────────────
find_package(spdlog REQUIRED)

# ─── 依赖：jsoncpp ─────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(JSONCPP IMPORTED_TARGET jsoncpp)
endif()
if(JSONCPP_FOUND)
    set(JSONCPP_LIB PkgConfig::JSONCPP)
else()
    find_library(JSONCPP_LIB jsoncpp REQUIRED)
endif()

# ─── 依赖：SQLite3 ─────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 IMPORTED_TARGET sqlite3)
endif()
if(SQLITE3_FOUND)
    set(SQLITE3_LIB PkgConfig::SQLITE3)
else()
    find_library(SQLITE3_LIB sqlite3 REQUIRED)
endif()

# ─── 依赖：GTest ──────────────────────────────────────
find_package(GTest REQUIRED)

# ─── 收集所有 SDK 源文件 ──────────────────────────────
set(SDK_SOURCES
    "${SDK_DIR}/C_source/ChatSDK.cpp"
    "${SDK_DIR}/C_source/LLMManager.cpp"
    "${SDK_DIR}/C_source/LLMProvider.cpp"
    "${SDK_DIR}/C_source/SessionManager.cpp"
    "${SDK_DIR}/C_source/DataManager.cpp"
    "${SDK_DIR}/C_source/UnifiedProvider.cpp"
    "${SDK_DIR}/C_source/log.cpp"
)

# ─── 测试可执行文件 ──────────────────────────────────
add_executable(test_ChatSDK
    ${SDK_SOURCES}
    test_ChatSDK.cpp
)

target_compile_definitions(test_ChatSDK PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

target_include_directories(test_ChatSDK
    PRIVATE
        "${SDK_DIR}/include"
        ${JSONCPP_INCLUDE_DIRS}
        ${SQLITE3_INCLUDE_DIRS}
        ${GTEST_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIRS}
        spdlog::spdlog
)

target_link_libraries(test_ChatSDK
    PRIVATE
        ${JSONCPP_LIB}
        ${SQLITE3_LIB}
        ${OPENSSL_LIBRARIES}
        spdlog::spdlog
        GTest::GTest
        GTest::Main
        pthread
)

# 自动发现测试
gtest_discover_tests(test_ChatSDK)
