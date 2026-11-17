cmake_minimum_required(VERSION 3.20)
project(telegramnx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(TGNX_WARNINGS -Wall -Wextra -Wno-unused-parameter)

add_library(tgnx_core STATIC
    src/core/util.cpp
    src/core/sha.cpp
    src/core/sha512.cpp
    src/core/srp.cpp
    src/core/aes.cpp
    src/core/bigint.cpp
    src/core/rsa.cpp
    src/core/tl.cpp
    src/core/net.cpp
    src/core/mtproto.cpp
    src/core/auth.cpp
    src/core/rpc.cpp
    src/core/api.cpp
    src/core/client.cpp
    src/core/tl_parser.cpp
    src/core/tl_schema_gen.cpp
    src/platform/host_platform.cpp
)
target_include_directories(tgnx_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tgnx_core PRIVATE ${TGNX_WARNINGS})
find_package(ZLIB REQUIRED)
target_link_libraries(tgnx_core PUBLIC ZLIB::ZLIB)

add_executable(crypto_test tests/crypto_test.cpp)
target_link_libraries(crypto_test PRIVATE tgnx_core)
target_compile_options(crypto_test PRIVATE ${TGNX_WARNINGS})

enable_testing()
add_test(NAME crypto COMMAND crypto_test)

add_executable(api_test tests/api_test.cpp)
target_link_libraries(api_test PRIVATE tgnx_core)
target_compile_options(api_test PRIVATE ${TGNX_WARNINGS})
add_test(NAME api COMMAND api_test)

add_executable(parse_test tests/parse_test.cpp)
target_link_libraries(parse_test PRIVATE tgnx_core)
target_compile_options(parse_test PRIVATE ${TGNX_WARNINGS})
add_test(NAME parse COMMAND parse_test)

add_test(NAME switch_syntax
    COMMAND ${CMAKE_CXX_COMPILER} -std=c++17 -fsyntax-only
            -I${CMAKE_CURRENT_SOURCE_DIR}/src
            -I${CMAKE_CURRENT_SOURCE_DIR}/third_party
            -I${CMAKE_CURRENT_SOURCE_DIR}/tests/switch_stub
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/switch_main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/switch_platform.cpp)

add_library(tgnx_ui STATIC
    src/ui/canvas.cpp
    src/ui/font.cpp
    src/ui/app.cpp
    third_party/qrcodegen.cpp
)
target_include_directories(tgnx_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(tgnx_ui PUBLIC tgnx_core)
target_compile_options(tgnx_ui PRIVATE ${TGNX_WARNINGS})

add_executable(ui_shots tests/ui_shots.cpp)
target_link_libraries(ui_shots PRIVATE tgnx_ui)
target_compile_options(ui_shots PRIVATE ${TGNX_WARNINGS})

add_executable(live_phone tests/live_phone.cpp)
target_link_libraries(live_phone PRIVATE tgnx_core)
target_compile_options(live_phone PRIVATE ${TGNX_WARNINGS})

add_executable(live_client tests/live_client.cpp)
target_link_libraries(live_client PRIVATE tgnx_core)
target_compile_options(live_client PRIVATE ${TGNX_WARNINGS})

add_executable(live_qr tests/live_qr.cpp)
target_link_libraries(live_qr PRIVATE tgnx_core)
target_compile_options(live_qr PRIVATE ${TGNX_WARNINGS})

add_executable(live_config tests/live_config.cpp)
target_link_libraries(live_config PRIVATE tgnx_core)
target_compile_options(live_config PRIVATE ${TGNX_WARNINGS})

add_executable(live_auth tests/live_auth.cpp)
target_link_libraries(live_auth PRIVATE tgnx_core)
target_compile_options(live_auth PRIVATE ${TGNX_WARNINGS})
