cmake_minimum_required(VERSION 3.16)
project(chat_grpc_server LANGUAGES CXX)


# 编译选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE SIRIGN "默认Release" FORCE)
endif()

# 找库依赖
find_package(Protobuf REQUIRED)
include(CMakeFindDependencyMacro)
find_package(gRPC CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(jsoncpp REQUIRED)

# 统一路径
set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ChatSDK_LLM/SDK")
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proto")
set(PROTO_FILE "${PROTO_DIR}/ChatService.proto")

# 校验：SDK 源目录必须在
if(NOT EXISTS "${SDK_ROOT}/include")
    message(FATAL_ERROR "找不到 SDK include 目录: ${SDK_ROOT}/include")
endif()

#设置打印消息
message(STATUS "")
message(STATUS "==== 配置 chat_grpc_server ====")
message(STATUS "  SDK 路径:  ${SDK_ROOT}")
message(STATUS "  proto:     ${PROTO_FILE}")
message(STATUS "  构建类型:   ${CMAKE_BUILD_TYPE}")
message(STATUS "")

# 找SDK源文件
file(GLOB SDK_SOURCES CONFIGURE_DEPENDS
    "${SDK_ROOT}/src/*.cpp"
    "${SDK_ROOT}/util/*.cpp"
)

message(STATUS "找到 ${CMAKE_MATCH_COUNT} 个 SDK 源文件：")
foreach(src ${SDK_SOURCES})
    message(STATUS "  - ${src}")
endforeach()

# protoc自动生成gPRC代码
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

get_filename_component(PROTO_WE ${PROTO_FILE} NAME_WE)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_WE}.grpc.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_WE}.grpc.pb.h
    COMMAND protobuf::protoc
    ARGS --grpc_out ${CMAKE_CURRENT_BINARY_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         -I ${PROTO_DIR}
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE} gRPC::grpc_cpp_plugin
    COMMENT "Generating gRPC code from ${PROTO_FILE}"
)
set(GRPC_SRCS ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_WE}.grpc.pb.cc)
set(GRPC_HDRS ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_WE}.grpc.pb.h)


# 库1：chat_grpc_proto : protoc生成的代码
add_library(chat_grpc_proto STATIC
    ${PROTO_SRCS} ${PROTO_HDRS}
    ${GRPC_SRCS} ${GRPC_HDRS}
)

target_include_directories(chat_grpc_proto PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(chat_grpc_proto PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
)


# 库2: ai_chat_sdk
add_library(ai_chat_sdk STATIC ${SDK_SOURCES})

target_include_directories(ai_chat_sdk PUBLIC
    ${SDK_ROOT}/include
)

target_link_libraries(ai_chat_sdk PUBLIC
    spdlog::spdlog
    SQLite::SQLite3
    jsoncpp_lib
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# 让cpp-httplib支持openssl
target_compile_definitions(ai_chat_sdk PUBLIC
    CPPHTTPLIB_OPENSSL_SUPPORT
)

add_executable(chat_grpc_server src/main.cc)

target_link_libraries(chat_grpc_server PRIVATE
    chat_grpc_proto
    ai_chat_sdk
)

# =====================================================
#  警告与编译选项
# =====================================================
if(MSVC)
    target_compile_options(chat_grpc_server PRIVATE /W4 /permissive-)
else()
    target_compile_options(chat_grpc_server PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()


#设置输出目录
set_target_properties(chat_grpc_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
)

message(STATUS "==== 配置完成 ====")
message(STATUS "下一步:cmake --build build")
message(STATUS "")
